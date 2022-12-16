// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <coroutine>
#include <optional>
#include <thread>

#include <gtest/gtest.h>

#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {
namespace {

// A wrapper around a coroutine that can be used as a promise.
template <typename T>
class Async {
 public:
  struct promise_type {
    using Handle = std::coroutine_handle<promise_type>;
    Poll<T>* returned_value;

    Async<T> get_return_object() { return Async<T>{my_handle()}; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void unhandled_exception() {}
    void return_value(T value) { *returned_value = std::move(value); }

    Handle my_handle() { return Handle::from_promise(*this); }
  };

  explicit Async(typename promise_type::Handle coroutine)
      : coroutine_(coroutine) {}

  Async(const Async&) = delete;
  Async& operator=(const Async&) = delete;
  Async(Async&& t) noexcept : coroutine_(std::move(t.coroutine_)) {}
  Async& operator=(Async&& t) noexcept {
    std::swap(coroutine_, t.coroutine_);
    return *this;
  }
  ~Async() {
    if (coroutine_) {
      coroutine_.destroy();
    }
  }

  Poll<T> operator()() {
    Poll<T> returned = Pending{};
    coroutine_.promise().returned_value = &returned;
    coroutine_.resume();
    coroutine_.promise().returned_value = nullptr;
    return returned;
  }

 private:
  typename promise_type::Handle coroutine_;
};

Async<int> TestFunction() {
  printf("TestFunction.0\n");
  co_await std::suspend_always{};
  printf("TestFunction.1\n");
  co_return 42;
}

TEST(Cpp20Test, CoroutinesArePromises) {
  auto f = TestFunction();
  printf("initial poll\n");
  EXPECT_EQ(f(), Poll<int>(Pending{}));
  printf("final poll\n");
  EXPECT_EQ(f(), Poll<int>(42));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
