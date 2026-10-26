// Copyright 2021 gRPC authors.
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

#include "src/core/lib/promise/seq.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/race.h"
#include "src/proto/grpc/channelz/v2/promise.upb.h"
#include "upb/mem/arena.hpp"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

namespace {
class TestDestruct {
 public:
  TestDestruct(std::string* execution_order, std::string destruct_token)
      : execution_order_(execution_order),
        destruct_token_(std::move(destruct_token)) {}

  TestDestruct(const TestDestruct&) = delete;
  TestDestruct& operator=(const TestDestruct&) = delete;

  TestDestruct(TestDestruct&& other) noexcept = default;
  TestDestruct& operator=(TestDestruct&& other) noexcept = default;

  ~TestDestruct() {
    if (execution_order_ != nullptr) {
      absl::StrAppend(execution_order_, destruct_token_);
    }
  }

 private:
  std::string* execution_order_;
  std::string destruct_token_;
};
}  // namespace

TEST(SeqTest, Immediate) {
  std::string execution_order;
  Poll<int> result = Seq([&execution_order] {
    absl::StrAppend(&execution_order, "1");
    return 100;
  })();
  EXPECT_EQ(result, Poll<int>(100));
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TEST(SeqTest, OneThen) {
  std::string execution_order;
  auto initial = [&execution_order,
                  test_destructor_invocation1 =
                      std::make_unique<int>(1)]() -> Poll<std::string> {
    absl::StrAppend(&execution_order, "1");
    return "Hello";
  };
  auto then = [&execution_order,
               test_destructor_invocation2 =
                   std::make_unique<int>(2)](std::string initial_output) {
    absl::StrAppend(&execution_order, "2");
    return [test_destructor_invocation3 = std::make_unique<int>(3),
            &execution_order, initial_output]() -> Poll<int> {
      absl::StrAppend(&execution_order, "3");
      return initial_output.length() + 4;
    };
  };
  auto result = Seq(std::move(initial), std::move(then))();
  EXPECT_TRUE(result.ready());
  EXPECT_EQ(result.value(), 9);
  EXPECT_STREQ(execution_order.c_str(), "123");
}

TEST(SeqTest, TestPending) {
  std::string execution_order;
  bool return_pending = true;
  auto initial = [&execution_order, &return_pending,
                  test_destructor_invocation1 =
                      std::make_unique<int>(1)]() -> Poll<int> {
    absl::StrAppend(&execution_order, "1");
    if (return_pending) return Pending{};
    return 100;
  };

  auto then = [test_destructor_invocation2 = std::make_unique<int>(2),
               &execution_order](int i) {
    absl::StrAppend(&execution_order, "2");
    return [i, test_destructor_invocation3 = std::make_unique<int>(3),
            &execution_order]() -> Poll<int> {
      absl::StrAppend(&execution_order, "3");
      return i + 4;
    };
  };

  auto seq_combinator = Seq(std::move(initial), std::move(then));
  auto result = seq_combinator();
  EXPECT_EQ(result, Poll<int>(Pending{}));
  EXPECT_STREQ(execution_order.c_str(), "1");

  execution_order.clear();
  return_pending = false;
  result = seq_combinator();
  EXPECT_EQ(result, Poll<int>(104));
  EXPECT_STREQ(execution_order.c_str(), "123");
}

TEST(SeqTest, ThreeTypedPendingThens) {
  std::string execution_order;
  bool pending_a = true;
  bool pending_b = true;
  bool pending_c = true;
  bool pending_d = true;

  struct A {
    int a_ = -1;
  };
  struct B {
    int b_ = -1;
  };
  struct C {
    int c_ = -1;
  };
  struct D {
    int d_ = -1;
  };

  auto initial = [&execution_order, &pending_a]() -> Poll<A> {
    absl::StrAppend(&execution_order, "0");
    if (pending_a) {
      absl::StrAppend(&execution_order, "P");
      return Pending{};
    }
    absl::StrAppend(&execution_order, "a");
    return A{100};
  };

  auto next1 = [&execution_order, &pending_b](A a) {
    absl::StrAppend(&execution_order, "1");
    return [&execution_order, &pending_b, a]() -> Poll<B> {
      EXPECT_EQ(a.a_, 100);
      if (pending_b) {
        absl::StrAppend(&execution_order, "P");
        return Pending{};
      }
      absl::StrAppend(&execution_order, "b");
      return B{200};
    };
  };

  auto next2 = [&execution_order, &pending_c](B b) {
    absl::StrAppend(&execution_order, "2");
    return [&execution_order, &pending_c, b]() -> Poll<C> {
      EXPECT_EQ(b.b_, 200);
      if (pending_c) {
        absl::StrAppend(&execution_order, "P");
        return Pending{};
      }
      absl::StrAppend(&execution_order, "c");
      return C{300};
    };
  };

  auto next3 = [&execution_order, &pending_d](C c) {
    absl::StrAppend(&execution_order, "3");
    return [&execution_order, &pending_d, c]() -> Poll<D> {
      EXPECT_EQ(c.c_, 300);
      if (pending_d) {
        absl::StrAppend(&execution_order, "P");
        return Pending{};
      }
      absl::StrAppend(&execution_order, "d");
      return D{400};
    };
  };

  auto seq_combinator = Seq(std::move(initial), std::move(next1),
                            std::move(next2), std::move(next3));

  auto retval = seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "0P");

  execution_order.clear();
  pending_a = false;
  retval = seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "0a1P");

  execution_order.clear();
  pending_b = false;
  retval = seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "b2P");

  execution_order.clear();
  pending_c = false;
  retval = seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "c3P");

  execution_order.clear();
  pending_d = false;
  retval = seq_combinator();
  EXPECT_TRUE(retval.ready());
  EXPECT_EQ(retval.value().d_, 400);
  EXPECT_STREQ(execution_order.c_str(), "d");
}

// This does not compile, but is useful for testing error messages generated
// TEST(SeqTest, MisTypedThen) {
// struct A {};
// struct B {};
// auto initial = [] { return A{}; };
// auto next = [](B) { return []() { return B{}; }; };
// Seq(initial, next)().take();
//}
//

TEST(SeqTest, TwoThens) {
  auto initial = [] { return std::string("a"); };
  auto next1 = [](std::string i) { return [i]() { return i + "b"; }; };
  auto next2 = [](std::string i) { return [i]() { return i + "c"; }; };
  EXPECT_EQ(Seq(std::move(initial), std::move(next1), std::move(next2))(),
            Poll<std::string>("abc"));
}

TEST(SeqTest, ThreeThens) {
  EXPECT_EQ(
      Seq([test_destructor_invocation1 =
               std::make_unique<int>(1)] { return std::string("a"); },
          [test_destructor_invocation2 =
               std::make_unique<int>(2)](std::string i) {
            return [i, y = std::make_unique<int>(2)]() { return i + "b"; };
          },
          [test_destructor_invocation3 =
               std::make_unique<int>(1)](std::string i) {
            return [i, y = std::make_unique<int>(3)]() { return i + "c"; };
          },
          [test_destructor_invocation4 =
               std::make_unique<int>(1)](std::string i) {
            return [i, y = std::make_unique<int>(4)]() { return i + "d"; };
          })(),
      Poll<std::string>("abcd"));
}

TEST(SeqTest, ToProto) {
  auto x = Seq([]() { return 42; },
               [polled = false](int i) mutable -> Poll<int> {
                 if (!polled) {
                   polled = true;
                   return Pending{};
                 }
                 return i + 1;
               },
               [](int i) { return i; });
  EXPECT_TRUE(promise_detail::kHasToProtoMethod<decltype(x)>)
      << TypeName<decltype(x)>();
  auto validate_proto = [](grpc_channelz_v2_Promise* promise_proto,
                           int current_step) {
    ASSERT_TRUE(grpc_channelz_v2_Promise_has_seq_promise(promise_proto));
    const auto* seq_promise =
        grpc_channelz_v2_Promise_seq_promise(promise_proto);
    size_t num_steps;
    const auto* const* steps =
        grpc_channelz_v2_Promise_Seq_steps(seq_promise, &num_steps);
    EXPECT_EQ(num_steps, 3);
    for (size_t i = 0; i < num_steps; i++) {
      if (i == static_cast<size_t>(current_step)) {
        EXPECT_TRUE(
            grpc_channelz_v2_Promise_SeqStep_has_polling_promise(steps[i]));
      } else {
        EXPECT_FALSE(
            grpc_channelz_v2_Promise_SeqStep_has_polling_promise(steps[i]));
      }
    }
  };
  upb::Arena arena;
  auto* promise_proto = grpc_channelz_v2_Promise_new(arena.ptr());
  PromiseAsProto(x, promise_proto, arena.ptr());
  validate_proto(promise_proto, 0);
  x();
  promise_proto = grpc_channelz_v2_Promise_new(arena.ptr());
  PromiseAsProto(x, promise_proto, arena.ptr());
  validate_proto(promise_proto, 1);
  x();
  promise_proto = grpc_channelz_v2_Promise_new(arena.ptr());
  PromiseAsProto(x, promise_proto, arena.ptr());
  validate_proto(promise_proto, 2);
}

struct Big {
  int x[256];
  void YesItIsUnused() const {}
};

TEST(SeqTest, SaneSizes) {
  auto x = Big();
  auto p1 = Seq(
      [x] {
        x.YesItIsUnused();
        return 1;
      },
      [](int) {
        auto y = Big();
        return [y]() {
          y.YesItIsUnused();
          return 2;
        };
      });
  LOG(INFO) << "sizeof(Big): " << sizeof(Big);  // Was 1024
  LOG(INFO) << "sizeof(p1): " << sizeof(p1);    // Was 1048
  EXPECT_GE(sizeof(p1), sizeof(Big));
  EXPECT_LT(sizeof(p1), 1.05 * sizeof(Big));  // Watchout for size bloat!
}

TEST(SeqIterTest, Accumulate) {
  std::vector<int> v{1, 2, 3, 4, 5};
  EXPECT_EQ(SeqIter(v.begin(), v.end(), 0,
                    [](int cur, int next) {
                      return [cur, next]() { return cur + next; };
                    })(),
            Poll<int>(15));
}

TEST(SeqTest, NestedSeqWithPending) {
  std::string execution_order;
  bool pending = true;

  auto nested_seq = Seq(
      [&execution_order]() {
        absl::StrAppend(&execution_order, "1");
        return 10;
      },
      [&execution_order, &pending](int outer_val) {
        return Seq(
            [&execution_order, outer_val]() {
              absl::StrAppend(&execution_order, "2");
              return outer_val * 2;  // Passes 20 to the next step
            },
            [&execution_order, &pending](int inner_val) -> Poll<int> {
              if (pending) {
                absl::StrAppend(&execution_order, "P");
                return Pending{};
              }
              absl::StrAppend(&execution_order, "3");
              return inner_val + 5;  // Passes 25 when pending is set to false
            });
      },
      [&execution_order](int nested_result) {
        absl::StrAppend(&execution_order, "4");
        return nested_result * 2;  // Returns 50 finally
      });

  // First poll: Hits the inner pending state
  auto result1 = nested_seq();
  EXPECT_TRUE(result1.pending());
  EXPECT_STREQ(execution_order.c_str(), "12P");

  execution_order.clear();
  pending = false;

  // Second poll: Resumes from inner Step 2
  auto result2 = nested_seq();
  EXPECT_TRUE(result2.ready());
  EXPECT_STREQ(execution_order.c_str(), "34");
  EXPECT_EQ(result2.value(), 50);
}

// Test that a single pending promise in a seq is destroyed correctly.
TEST(SeqTest, DestructionOrderWithSinglePendingPromise) {
  std::string execution_order;

  // We use an inner scope here because the `Seq` combinator holds onto the
  // final promise internally. Forcing `seq_promise` out of scope destroys it,
  // which in turn destroys the final promise so we can verify its
  // destruction.
  {
    auto seq_promise = Seq(
        [&execution_order]() -> Poll<absl::Status> {
          absl::StrAppend(&execution_order, "A");
          return absl::OkStatus();
        },
        [step2_factory_destruct = TestDestruct(&execution_order, "~B"),
         &execution_order]() {
          absl::StrAppend(&execution_order, "B");
          return [&execution_order, wait_for = 2,
                  step2_promise_destruct = TestDestruct(
                      &execution_order, "~C")]() mutable -> Poll<absl::Status> {
            absl::StrAppend(&execution_order, "C");
            if (wait_for-- > 0) {
              return Pending{};
            }
            absl::StrAppend(&execution_order, "D");
            return absl::OkStatus();
          };
        });

    // Nothing has been executed yet
    EXPECT_STREQ(execution_order.c_str(), "");

    // Poll 1:
    //   * Initial promise (A) is polled
    //   * Step 2 factory (B) is polled, creates a promise (C)
    //   * Step 2 Factory is destroyed (~B)
    //   * Inner promise (C) is polled
    auto result1 = seq_promise();
    EXPECT_TRUE(result1.pending());
    EXPECT_STREQ(execution_order.c_str(), "AB~BC");

    // Poll 2:
    //   * Inner promise (C) is polled second time
    //   * Returns Pending (C is still pending)
    auto result2 = seq_promise();
    EXPECT_TRUE(result2.pending());
    EXPECT_STREQ(execution_order.c_str(), "AB~BCC");

    // Poll 3:
    //   * Inner promise (C) is polled a third time
    //   * Returns Ready with OkStatus (D)
    auto result3 = seq_promise();
    EXPECT_TRUE(result3.ready());
    EXPECT_EQ(result3.value(), absl::OkStatus());
    EXPECT_STREQ(execution_order.c_str(), "AB~BCCCD");
  }

  // seq_promise is destroyed, which destroys the inner promise (~C)
  EXPECT_STREQ(execution_order.c_str(), "AB~BCCCD~C");
}

// Test that multiple pending promises in a seq are destroyed correctly.
TEST(SeqTest, DestructionOrderWithMultiplePendingPromises) {
  std::string execution_order;

  {
    auto seq_promise = Seq(
        [&execution_order]() -> Poll<absl::Status> {
          absl::StrAppend(&execution_order, "A");
          return absl::OkStatus();
        },
        [step2_factory_destruct = TestDestruct(&execution_order, "~B"),
         &execution_order]() {
          absl::StrAppend(&execution_order, "B");
          return
              [step2_promise_destruct = TestDestruct(&execution_order, "~C"),
               &execution_order, wait_for = 2]() mutable -> Poll<absl::Status> {
                absl::StrAppend(&execution_order, "C");
                if (wait_for-- > 0) {
                  return Pending{};
                }
                absl::StrAppend(&execution_order, "D");
                return absl::OkStatus();
              };
        },
        [step3_factory_destruct = TestDestruct(&execution_order, "~E"),
         &execution_order]() {
          absl::StrAppend(&execution_order, "E");
          return
              [step3_promise_destruct = TestDestruct(&execution_order, "~F"),
               &execution_order, wait_for = 2]() mutable -> Poll<absl::Status> {
                absl::StrAppend(&execution_order, "F");
                if (wait_for-- > 0) {
                  return Pending{};
                }
                absl::StrAppend(&execution_order, "G");
                return absl::OkStatus();
              };
        });

    // Nothing has been executed yet
    EXPECT_STREQ(execution_order.c_str(), "");

    // Poll 1:
    //   * Initial promise (A) is polled
    //   * Step 2 factory (B) is polled, creates a promise (C)
    //   * Step 2 Factory is destroyed (~B)
    //   * Inner 2 promise (C) is polled
    auto result1 = seq_promise();
    EXPECT_TRUE(result1.pending());
    EXPECT_STREQ(execution_order.c_str(), "AB~BC");

    // Poll 2:
    //   * Inner 2 promise (C) is polled second time
    //   * Returns Pending (C is still pending)
    auto result2 = seq_promise();
    EXPECT_TRUE(result2.pending());
    EXPECT_STREQ(execution_order.c_str(), "AB~BCC");

    // Poll 3:
    //   * Inner 2 promise (C) is polled third time
    //   * It resolves returning OkStatus (D)
    //   * Inner 2 factory destroyed (~C)
    //   * Step 3 factory (E) is polled
    //   * Step 3 factory is destroyed (~E)
    //   * Inner 3 promise (F) is polled
    auto result3 = seq_promise();
    EXPECT_TRUE(result3.pending());
    EXPECT_STREQ(execution_order.c_str(), "AB~BCCCD~CE~EF");

    // Poll 4:
    //   * Inner 3 polled again (F)
    auto result4 = seq_promise();
    EXPECT_TRUE(result4.pending());
    EXPECT_STREQ(execution_order.c_str(), "AB~BCCCD~CE~EFF");

    // Poll 5:
    //   * Inner 3 promise (F) is polled again
    //   * It resolves returning OkStatus (G)
    auto result5 = seq_promise();
    EXPECT_TRUE(result5.ready());
    EXPECT_EQ(result5.value(), absl::OkStatus());
    EXPECT_STREQ(execution_order.c_str(), "AB~BCCCD~CE~EFFFG");
  }

  // Scope closed: seq_promise is destroyed, which destroys the final inner
  // promise (~F)
  EXPECT_STREQ(execution_order.c_str(), "AB~BCCCD~CE~EFFFG~F");
}

// Test that a seq with a map is destroyed correctly.
TEST(SeqTest, DestructorWithMapIfAndRace) {
  std::string execution_order;

  {
    auto seq_promise = Seq(
        // Step 1: Simple promise
        [&execution_order]() -> Poll<absl::Status> {
          absl::StrAppend(&execution_order, "A");
          return absl::OkStatus();
        },
        // Step 2: Factory returning a Map
        [step2_factory_destruct = TestDestruct(&execution_order, "~B"),
         &execution_order]() {
          absl::StrAppend(&execution_order, "B");
          return Map(
              [inner_destruct = TestDestruct(&execution_order, "~C"),
               &execution_order, wait_for = 1]() mutable -> Poll<absl::Status> {
                absl::StrAppend(&execution_order, "C");
                if (wait_for-- > 0) {
                  return Pending{};
                }
                return absl::OkStatus();
              },
              // The callback function processing the result
              [fn_destruct = TestDestruct(&execution_order, "~D"),
               &execution_order](absl::Status status) {
                absl::StrAppend(&execution_order, "D");
                return status;
              });
        },
        // Step 3: Factory returning an If
        [&execution_order]() {
          return If(
              // Condition
              true,
              // True branch WITH destructible object
              [if_true_destruct = TestDestruct(&execution_order, "~E"),
               &execution_order]() {
                return [&execution_order]() -> Poll<absl::Status> {
                  absl::StrAppend(&execution_order, "E");
                  return absl::OkStatus();
                };
              },
              // False branch
              [if_false_destruct = TestDestruct(&execution_order, "~X"),
               &execution_order]() {
                return [&execution_order]() -> Poll<absl::Status> {
                  // This promise is not expected to be polled.
                  absl::StrAppend(&execution_order, "UNPOLLED");
                  return absl::CancelledError();
                };
              });
        },
        [race_destruct = TestDestruct(&execution_order, "~F"),
         &execution_order]() {
          absl::StrAppend(&execution_order, "F");
          return Race(
              [&execution_order,
               destructA = TestDestruct(&execution_order,
                                        "~R1")]() -> Poll<absl::Status> {
                absl::StrAppend(&execution_order, "R1");
                return absl::OkStatus();  // Wins the race
              },
              [&execution_order,
               destructB = TestDestruct(&execution_order,
                                        "~R2")]() -> Poll<absl::Status> {
                absl::StrAppend(&execution_order, "R2");
                return Pending{};
              },
              [&execution_order,
               destructC = TestDestruct(&execution_order,
                                        "~R3")]() -> Poll<absl::Status> {
                absl::StrAppend(&execution_order, "R3");
                return Pending{};
              });
        });

    // Nothing has been executed yet
    EXPECT_STREQ(execution_order.c_str(), "");

    // Poll 1:
    // * Step 1 runs (A)
    // * Step 2 Factory runs (B), creates a Map with a pending inner promise (C)
    // * Factory is immediately destroyed (~B)
    // * Map inner promise is polled (C)
    auto result1 = seq_promise();
    EXPECT_TRUE(result1.pending());
    EXPECT_STREQ(execution_order.c_str(), "AB~BC");

    // Poll 2:
    // * Map inner promise is polled again (C) and resolves
    // * Map callback runs (D)
    // * Callback is destroyed (~D)
    // * Inner Map is destroyed (~C)
    // * False branch is immediately destroyed (~X)
    // * If condition is evaluated, and the true branch runs (E)
    // * True branch is destroyed (~E)
    // * Step 4 factory runs (F)
    // * Step 4 factory is destroyed (~F)
    // * Race is polled, Branch 1 runs (R1) and returns Ready
    auto result2 = seq_promise();
    EXPECT_TRUE(result2.ready());
    EXPECT_EQ(result2.value(), absl::OkStatus());
    EXPECT_STREQ(execution_order.c_str(), "AB~BCCD~D~C~X~EEF~FR1");
  }

  // Scope closed: seq_promise is destroyed, cleaning up remaining resources.
  // Final step (Race) is destroyed, which destroys the unpolled promises.
  EXPECT_NE(execution_order.find("~R1"), std::string::npos);
  EXPECT_NE(execution_order.find("~R2"), std::string::npos);
  EXPECT_NE(execution_order.find("~R3"), std::string::npos);
}
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
