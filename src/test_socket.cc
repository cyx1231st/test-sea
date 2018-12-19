// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// // vim: ts=8 sw=2 smarttab
#include <iostream>
#include <string>
#include <vector>

#include <seastar/core/app-template.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/reactor.hh>
#include <seastar/util/log.hh>

const int PORT = 1234;
const seastar::socket_address address = seastar::make_ipv4_address({PORT});

auto logger_ = seastar::logger("main");

template <typename T>
seastar::net::packet make_static_packet(const T& value) {
  return { reinterpret_cast<const char*>(&value), sizeof(value) };
}

class Conn {
  seastar::connected_socket socket;
  const bool is_server;

 public:
  unsigned long id = 0;
  seastar::input_stream<char> in;
  seastar::output_stream<char> out;
  seastar::socket_address paddr;
  int data = 7872;

  unsigned long get_id() {
    if (id == 0) {
      return reinterpret_cast<unsigned long>(this);
    } else {
      return id;
    }
  }

  std::string get_role() {
    if (is_server) {
      return "server";
    } else {
      return "client";
    }
  }

  explicit Conn(seastar::connected_socket&& fd, const seastar::socket_address& paddr, bool is_server)
      : socket(std::move(fd)), is_server(is_server), in(socket.input()), out(socket.output()), paddr(paddr) {}

  seastar::future<> close() {
    logger_.info("Conn [{}:{}] closing...", get_id(), get_role());
    return seastar::when_all(in.close(), out.close())
      .discard_result()
      .finally([this] {
        logger_.info("Conn [{}:{}] closed", get_id(), get_role());
      });
  }
};

struct Msgr {
  seastar::gate gate;
  seastar::server_socket *listener = nullptr;
  int conn_id = 0;

  int get_id() {
    return ++conn_id;
  }

  seastar::future<> start() {
    seastar::with_gate(gate, [this] {
        seastar::listen_options lo;
        lo.reuse_address = true;
        return seastar::do_with(seastar::listen(address, lo), [this] (auto& listener_) {
            listener = &listener_;
            return seastar::keep_doing([this] () {
                return listener->accept().then([this](seastar::connected_socket fd,
                                                  seastar::socket_address paddr) {
                    accept(std::move(fd), paddr);
                  });
              }).handle_exception([] (auto eptr) {
                if (seastar::smp::main_thread()) {
                  logger_.info("[0/all] Msgr::listener::accept() aborted: {}", eptr);
                }
              });
          });
      });
    return seastar::now();
  }

  seastar::future<> stop() {
    assert(listener);
    listener->abort_accept();
    return gate.close();
  }

  void accept(seastar::connected_socket&& fd, const seastar::socket_address& paddr) {
    seastar::with_gate(gate, [&fd, &paddr, this] {
        return seastar::do_with(Conn(std::move(fd), paddr, true), [this](auto& conn) {
            logger_.info("Conn [{}:{}] accepted", conn.get_id(), conn.get_role());
            return conn.in.read_exactly(sizeof(unsigned long))
              .then([&conn](auto buf) {
                if (buf.size() < sizeof(unsigned long)) {
                  logger_.info("Conn [{}:{}] dispatch_server() got short read: {}.", conn.get_id(), conn.get_role(), buf.size());
                  throw sizeof(int)-buf.size();
                }
                auto ident = reinterpret_cast<const unsigned long *>(buf.get());
                logger_.info("Conn [{}:{}] server got id: {}", conn.get_id(), conn.get_role(), *ident);
                conn.id = *ident;
              }).then([&conn, this] {
                return dispatch_server(conn);
              });
          }).handle_exception([](auto eptr) {
            logger_.info("Msgr::accept() aborted: {}", eptr);
          });
      });
  }

  void connect() {
    logger_.info("--------------------------------------");
    seastar::with_gate(gate, [this] {
        return seastar::connect(address).then([this](seastar::connected_socket fd) {
             return seastar::do_with(Conn(std::move(fd), address, false), [this](auto& conn) {
                 conn.id = get_id();
                 logger_.info("Conn [{}:{}] connected", conn.get_id(), conn.get_role());
                 auto fut = dispatch_client(conn);
                 logger_.info("Conn [{}:{}] send {}", conn.get_id(), conn.get_role(), conn.id);
                 return conn.out.write(make_static_packet(conn.id))
                   .then([&conn] {
                     return conn.out.flush();
                   }).then([&conn, this] {
                     return do_connect(conn);
                   }).then([&conn, fut=std::move(fut)]() mutable {
                     return std::move(fut);
                   }).handle_exception([&conn] (auto eptr) {
                     logger_.info("Conn [{}:{}] connect() failed: {}", conn.get_id(), conn.get_role(), eptr);
                   });
               });
          }).handle_exception([](auto eptr) {
            logger_.info("Msgr::connect() failed: {}", eptr);
          });
      });
  }

  /*
   * 1. [c]-data ->[s]
   *    [c]<data+1-[s]
   *          close[s]
   *    [c]close
   * 2. [c]close
   *           read[s]
   *     short_read[s]
   *          close[s]
   * 3.       close[s]
   *    [c]write
   *    [c]broken_pipe(32)
   *    [c]in_close
   *    [c]out_close
   *    [c]broken_pipe(32)
   * 4. [c]close
   *    [c]close
   *          close[s]
   * 5. [c]close
   *    [c]write
   *    [c]write
   *    [c]out_close
   *    [c]broken_pipe(32)
   *    [c]read
   *    [c]short_read
   *          close[s]
   */

  seastar::future<> do_connect(Conn& conn) {
    using namespace std::chrono_literals;
    if (conn_id == 1) {
      logger_.info("Conn [{}:{}] send {}", conn.get_id(), conn.get_role(), conn.data);
      return conn.out.write(make_static_packet(conn.data))
        .then([&conn] {
          return conn.out.flush();
        });
    } else if (conn_id == 2) {
      return conn.close();
    } else if (conn_id == 3) {
      return seastar::keep_doing([&conn] () {
          return seastar::sleep(100ms).then([&conn] {
              logger_.info("Conn [{}:{}] send {}", conn.get_id(), conn.get_role(), conn.data);
              return conn.out.write(make_static_packet(conn.data));
            }).then([&conn] {
              return conn.out.flush();
            });
        }).handle_exception([&conn](auto eptr) {
          logger_.info("Conn [{}:{}] write failed: {}", conn.get_id(), conn.get_role(), eptr);
          logger_.info("Conn [{}:{}] in closing...", conn.get_id(), conn.get_role());
          return conn.in.close();
        }).then([&conn] {
          logger_.info("Conn [{}:{}] in closed", conn.get_id(), conn.get_role());
          logger_.info("Conn [{}:{}] out closing...", conn.get_id(), conn.get_role());
          return conn.out.close();
        }).handle_exception([&conn](auto eptr) {
          logger_.info("Conn [{}:{}] out should be already closed: {}", conn.get_id(), conn.get_role(), eptr);
        });
    } else if (conn_id == 4) {
      return conn.close().then([&conn] {
          return conn.close();
        });
    } else if (conn_id == 5) {
      return conn.close().then([&conn] {
          logger_.info("Conn [{}:{}] send {}", conn.get_id(), conn.get_role(), conn.data);
          return conn.out.write(make_static_packet(conn.data));
        }).then([&conn] {
          return conn.out.flush();
        }).then([&conn] {
          logger_.info("Conn [{}:{}] send {}", conn.get_id(), conn.get_role(), conn.data);
          return conn.out.write(make_static_packet(conn.data));
        }).then([&conn] {
          return conn.out.flush();
        }).then([&conn] {
          // conn.out should be closed again, or there will be assertion error:
          // seastar::output_stream<CharType>::~output_stream() [with CharType = char]: Assertion `!_in_batch' failed.
          logger_.info("Conn [{}:{}] out closing...", conn.get_id(), conn.get_role());
          return conn.out.close();
        }).handle_exception([&conn](auto eptr) {
          logger_.info("Conn [{}:{}] out should be already closed: {}", conn.get_id(), conn.get_role(), eptr);
        }).then([&conn] {
          return conn.in.read_exactly(sizeof(int));
        }).then([&conn](auto buf) {
          logger_.info("Conn [{}:{}] short read: {}", conn.get_id(), conn.get_role(), buf.size());
        });
    } else {
      logger_.error("Conn [{}:{}] do_connect() wrong conn id.", conn.get_id(), conn.get_role());
      return seastar::now();
    }
  }

  seastar::future<> dispatch_server(Conn& conn) {
    if (conn_id == 1) {
      return conn.in.read_exactly(sizeof(int))
        .then([&conn](auto buf) {
          int msg = *reinterpret_cast<const int *>(buf.get());
          logger_.info("Conn [{}:{}] dispatch_server() got: {}", conn.get_id(), conn.get_role(), msg);
          conn.data = msg + 1;
          logger_.info("Conn [{}:{}] dispatch_server() send {}", conn.get_id(), conn.get_role(), conn.data);
          return conn.out.write(make_static_packet(conn.data));
        }).then([&conn] {
          return conn.out.flush();
        }).then([&conn] {
          return conn.close();
        });
    } else if (conn_id == 2) {
      return seastar::keep_doing([&conn] () {
          return conn.in.read_exactly(sizeof(int))
           .then([&conn](auto buf) {
             if (buf.size() < sizeof(int)) {
               logger_.info("Conn [{}:{}] dispatch_server() short read: {}", conn.get_id(), conn.get_role(), buf.size());
               throw sizeof(int)-buf.size();
             }
             int msg = *reinterpret_cast<const int *>(buf.get());
             logger_.info("Conn [{}:{}] server got msg: {}", conn.get_id(), conn.get_role(), msg);
           });
        }).handle_exception([&conn](auto eptr) {
          logger_.info("Conn [{}:{}] dispatch_server() got except: {}", conn.get_id(), conn.get_role(), eptr);
          return conn.close();
        });
    } else if (conn_id == 3) {
      return conn.close();
    } else if (conn_id == 4) {
      return conn.close();
    } else if (conn_id == 5) {
      return conn.close();
    } else {
      logger_.error("Conn [{}:{}] dispatch_server() wrong conn id.", conn.get_id(), conn.get_role());
      return seastar::now();
    }
  }

  seastar::future<> dispatch_client(Conn& conn) {
    if (conn_id == 1) {
      return conn.in.read_exactly(sizeof(int))
        .then([&conn](auto buf) {
          int msg = *reinterpret_cast<const int *>(buf.get());
          logger_.info("Conn [{}:{}] dispatch_client() got: {}", conn.get_id(), conn.get_role(), msg);
        }).then([&conn] {
          return conn.close();
        });
    } else if (conn_id == 2) {
      return seastar::now();
    } else if (conn_id == 3) {
      return seastar::now();
    } else if (conn_id == 4) {
      return seastar::now();
    } else if (conn_id == 5) {
      return seastar::now();
    } else {
      logger_.error("Conn [{}:{}] dispatch_client() wrong conn id.", conn.get_id(), conn.get_role());
      return seastar::now();
    }
  }
};
seastar::sharded<Msgr> shd_msgr;

void run() {
  shd_msgr.local().connect();
  using namespace std::chrono_literals;
  seastar::sleep(500ms).then([] {
      shd_msgr.local().connect();
      return seastar::sleep(500ms);
    }).then([] {
      shd_msgr.local().connect();
      return seastar::sleep(500ms);
    }).then([] {
      shd_msgr.local().connect();
      return seastar::sleep(700ms);
    }).then([] {
      shd_msgr.local().connect();
    });
}

seastar::future<> init() {
  logger_.info("Seastar started!");
  return shd_msgr.start().then([] {
      return shd_msgr.invoke_on_all([](Msgr& msgr) {
          return msgr.start();
        });
    }).then([] {
      logger_.info("Msgr will shutdown after 5s...");
      run();
      using namespace std::chrono_literals;
      return seastar::sleep(5s);
    }).then([] {
      logger_.info("Stopping msgr (abort listening)...");
      return shd_msgr.stop();
    }).finally([] {
      logger_.info("Seastar stopped!");
    });
}

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, init);
}
