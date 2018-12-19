// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// // vim: ts=8 sw=2 smarttab
#include <memory>

#include <seastar/core/app-template.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>
#include <seastar/util/log.hh>

namespace {

auto logger_ = seastar::logger("main");

struct Parent;
using ParentRef = seastar::shared_ptr<Parent>;
using ParentFRef = seastar::foreign_ptr<ParentRef>;

struct Parent: public seastar::enable_shared_from_this<Parent>
{
  int content = 0;
  Parent(int c) : content(c) {
    logger_.info("Parent {} csted", content);
  }
  Parent(const Parent& other) = delete;
  Parent(Parent&& other) = delete;
  /*
  Parent(const Parent& other) : content(other.content) {
    logger_.info("Parent {} Copyed!", content);
  }
  Parent(Parent&& other) : content(other.content) {
    logger_.info("Parent {} moved", content);
  }
  */
  virtual ~Parent() {
    logger_.info("Parent {} dsted", content);
  }
};

struct Child;
using ChildRef = seastar::shared_ptr<Child>;
using ChildFRef = seastar::foreign_ptr<ChildRef>;

struct Child: public Parent
{
  int c_content = 1000;
  Child(int c, int cc) : Parent(c), c_content(cc) {}
  Child(const Child& other) : Parent(other.content), c_content(other.c_content) {}
  Child(Child&& other) : Parent(other.content), c_content(other.c_content) {}

  // TODO: how to implement this?
  // ChildRef make_shared();

};

seastar::future<> init() {
  using namespace std::chrono_literals;
  logger_.info("Seastar started!");
  // simple shared_ptr, cannot be destructed at the original core
  ParentRef p_base = seastar::make_shared<Parent>(1);
  // foreign_ptr can ensure that the object is destructed at the original core
  ParentRef p_base1 = seastar::make_shared<Parent>(2);
  ParentFRef fp_base1 = seastar::make_foreign(p_base1);
  // shared_ptr<child> can be created even if it is inherited by enable_shared_from_this<Parent>
  ChildRef p_child3 = seastar::make_shared<Child>(3, 1000);
  // foreign_ptr<child> can be created from the shared_ptr<child>
  ChildFRef fp_child3 = seastar::make_foreign(p_child3);
  // shared_ptr<parent> can be created from the shared_ptr<child>
  ParentRef p_base3 = ParentRef(p_child3);
  // foreign_ptr<parent> can be created from the shared_ptr<child>
  ParentFRef fp_base3 = seastar::make_foreign(p_child3->shared_from_this());
  ParentFRef fp_base3_ = seastar::make_foreign(p_base3);

  logger_.info("--- 1 ---");
  return seastar::sleep(100ms)
    .then([p_base1] {
      logger_.info("--- 2 ---");
    }).then([p_base = std::move(p_base),
             fp_base1 = std::move(fp_base1),
             fp_base3 = std::move(fp_base3)]() mutable {
	  logger_.info("--- 3 ---");
	  seastar::smp::submit_to(6, [p_base = std::move(p_base),
                                         fp_base1 = std::move(fp_base1),
                                         fp_base3 = std::move(fp_base3)]() mutable {
		  logger_.info("--- 3.1 ---", seastar::engine().cpu_id());
		  return seastar::sleep(100ms)
			.then([p_base = std::move(p_base),
                   fp_base1 = std::move(fp_base1),
                   fp_base3 = std::move(fp_base3)] {
			  logger_.info("--- 3.2 ---", seastar::engine().cpu_id());
			});
		});
    }).then([] {
      logger_.info("--- 4 ---");
      return seastar::sleep(500ms);
    }).finally([fp_base3_ = std::move(fp_base3_)]{
      logger_.info("--- 5 ---");
    });
}

} // anonymous namespace

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, init);
}
