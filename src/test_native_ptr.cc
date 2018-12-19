// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// // vim: ts=8 sw=2 smarttab
#include <iostream>
#include <memory>

namespace {

struct Parent;
using ParentRef = std::shared_ptr<Parent>;

struct Parent: public std::enable_shared_from_this<Parent>
{
  int content = 0;
  Parent(int c) : content(c) {
    std::cout << "Parent " << content << " csted" << std::endl;
  }
  Parent(const Parent& other) = delete;
  Parent(Parent&& other) = delete;
  /* no need
  Parent(const Parent& other) : content(other.content) {
    std::cout << "Parent " << content << " Copyed" << std::endl;
    logger_.info("Parent {} Copyed!", content);
  }
  Parent(Parent&& other) : content(other.content) {
    logger_.info("Parent {} moved", content);
  }
  */
  virtual ~Parent() {
    std::cout << "Parent " << content << " dsted" << std::endl;
  }
};

} // anonymous namespace

int main(int argc, char** argv) {
}
