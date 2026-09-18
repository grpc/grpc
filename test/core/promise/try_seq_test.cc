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

#include "src/core/lib/promise/try_seq.h"

#include <stdlib.h>

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/proto/grpc/channelz/v2/promise.upb.h"
#include "test/core/promise/poll_matcher.h"
#include "upb/mem/arena.hpp"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

TEST(TrySeqTestBasic, ThreeTypedPendingThens) {
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
    return [&execution_order, &pending_c, b]() -> Poll<absl::StatusOr<C>> {
      EXPECT_EQ(b.b_, 200);
      if (pending_c) {
        absl::StrAppend(&execution_order, "P");
        return Pending{};
      }
      absl::StrAppend(&execution_order, "Fail");
      return absl::StatusOr<C>();
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

  auto try_seq_combinator = TrySeq(std::move(initial), std::move(next1),
                                   std::move(next2), std::move(next3));

  auto retval = try_seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "0P");

  execution_order.clear();
  pending_a = false;
  retval = try_seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "0a1P");

  execution_order.clear();
  pending_b = false;
  retval = try_seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "b2P");

  execution_order.clear();
  pending_c = false;
  retval = try_seq_combinator();
  EXPECT_TRUE(retval.ready());
  EXPECT_STREQ(execution_order.c_str(), "Fail");
}

struct AbslStatusTraits {
  template <typename T>
  using Promise = std::function<Poll<absl::StatusOr<T>>()>;

  template <typename T>
  static Promise<T> instant_ok(T x) {
    return [x] { return absl::StatusOr<T>(x); };
  }

  static auto instant_ok_status() {
    return [] { return absl::OkStatus(); };
  }

  template <typename T>
  static Promise<T> instant_fail() {
    return [] { return absl::StatusOr<T>(); };
  }

  template <typename T>
  static Poll<absl::StatusOr<T>> instant_crash() {
    abort();
  }

  template <typename T>
  static Poll<absl::StatusOr<T>> ok(T x) {
    return absl::StatusOr<T>(x);
  }

  static Poll<absl::Status> ok_status() { return absl::OkStatus(); }

  template <typename T>
  static Poll<absl::StatusOr<T>> fail() {
    return absl::StatusOr<T>();
  }
};

struct ValueOrFailureTraits {
  template <typename T>
  using Promise = std::function<Poll<ValueOrFailure<T>>()>;

  template <typename T>
  static Promise<T> instant_ok(T x) {
    return [x] { return ValueOrFailure<T>(x); };
  }

  static auto instant_ok_status() {
    return [] { return StatusFlag(true); };
  }

  template <typename T>
  static Promise<T> instant_fail() {
    return [] { return Failure{}; };
  }

  template <typename T>
  static Poll<ValueOrFailure<T>> instant_crash() {
    abort();
  }

  template <typename T>
  static Poll<ValueOrFailure<T>> ok(T x) {
    return ValueOrFailure<T>(x);
  }

  static Poll<StatusFlag> ok_status() { return Success{}; }

  template <typename T>
  static Poll<ValueOrFailure<T>> fail() {
    return Failure{};
  }
};

template <typename T>
class TrySeqTest : public ::testing::Test {};

using Traits = ::testing::Types<AbslStatusTraits, ValueOrFailureTraits>;
TYPED_TEST_SUITE(TrySeqTest, Traits);

TYPED_TEST(TrySeqTest, SucceedAndThen) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(TypeParam::instant_ok(1),
                   [&execution_order](int i) {
                     absl::StrAppend(&execution_order, "1");
                     return TypeParam::instant_ok(i + 1);
                   })(),
            TypeParam::ok(2));
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TYPED_TEST(TrySeqTest, SucceedDirectlyAndThenDirectly) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(
                [&execution_order] {
                  absl::StrAppend(&execution_order, "1");
                  return 1;
                },
                [&execution_order](int i) {
                  absl::StrAppend(&execution_order, "2");
                  return [i, &execution_order]() {
                    absl::StrAppend(&execution_order, "3");
                    return i + 1;
                  };
                })(),
            Poll<absl::StatusOr<int>>(2));
  EXPECT_STREQ(execution_order.c_str(), "123");
}

TYPED_TEST(TrySeqTest, SucceedAndThenChangeType) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(TypeParam::instant_ok(42),
                   [&execution_order](int i) {
                     absl::StrAppend(&execution_order, "1");
                     return TypeParam::instant_ok(std::to_string(i));
                   })(),
            TypeParam::ok(std::string("42")));
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TYPED_TEST(TrySeqTest, FailAndThen) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(TypeParam::template instant_fail<int>(),
                   [&execution_order](int) {
                     absl::StrAppend(&execution_order, "1");
                     return TypeParam::template instant_crash<double>();
                   })(),
            TypeParam::template fail<double>());
  EXPECT_STREQ(execution_order.c_str(), "");
}

TYPED_TEST(TrySeqTest, RawSucceedAndThen) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(TypeParam::instant_ok_status(),
                   [&execution_order] {
                     absl::StrAppend(&execution_order, "1");
                     return TypeParam::instant_ok_status();
                   })(),
            TypeParam::ok_status());
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TYPED_TEST(TrySeqTest, RawFailAndThen) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(
                [&execution_order] {
                  absl::StrAppend(&execution_order, "1");
                  return absl::CancelledError();
                },
                [&execution_order]() {
                  absl::StrAppend(&execution_order, "2");
                  return [&execution_order]() -> Poll<absl::Status> {
                    absl::StrAppend(&execution_order, "3");
                    abort();
                  };
                })(),
            Poll<absl::Status>(absl::CancelledError()));
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TYPED_TEST(TrySeqTest, RawSucceedAndThenValue) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(
                [&execution_order] {
                  absl::StrAppend(&execution_order, "1");
                  return absl::OkStatus();
                },
                [&execution_order] {
                  absl::StrAppend(&execution_order, "2");
                  return [&execution_order]() {
                    absl::StrAppend(&execution_order, "3");
                    return absl::StatusOr<int>(42);
                  };
                })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>(42)));
  EXPECT_STREQ(execution_order.c_str(), "123");
}

TYPED_TEST(TrySeqTest, NestedTrySeq) {
  std::string execution_order;
  // Happy path
  auto success_promise =
      TrySeq(TypeParam::instant_ok(1), [&execution_order](int i) {
        absl::StrAppend(&execution_order, "1");
        EXPECT_EQ(i, 1);
        return TrySeq(TypeParam::instant_ok(i + 1), [&execution_order](int j) {
          absl::StrAppend(&execution_order, "2");
          EXPECT_EQ(j, 2);
          return TypeParam::instant_ok(j + 1);
        });
      });

  EXPECT_EQ(success_promise(), TypeParam::ok(3));
  EXPECT_STREQ(execution_order.c_str(), "12");

  execution_order.clear();
  // Error path
  auto fail_promise = TrySeq(
      TypeParam::instant_ok(1),
      [&execution_order](int i) {
        absl::StrAppend(&execution_order, "1");
        EXPECT_EQ(i, 1);
        return TrySeq(TypeParam::template instant_fail<int>(),
                      [&execution_order](int) {
                        absl::StrAppend(&execution_order, "Unreachable1");
                        return TypeParam::template instant_crash<int>();
                      });
      },
      [&execution_order](int) {
        absl::StrAppend(&execution_order, "Unreachable2");
        return TypeParam::template instant_crash<int>();
      });

  EXPECT_EQ(fail_promise(), TypeParam::template fail<int>());
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TYPED_TEST(TrySeqTest, HeterogeneousChainOfPromises) {
  std::string execution_order;
  auto promise = TrySeq(
      TypeParam::instant_ok(1),
      // Step 1: Deep Nesting (TrySeq enclosing an If)
      [&execution_order](int i) {
        absl::StrAppend(&execution_order, "DeepTrySeqIn(", i, "),");
        return TrySeq(TypeParam::instant_ok(i * 10),  // passes 10
                      [&execution_order](int j) {
                        absl::StrAppend(&execution_order, "IfIn(", j, "),");
                        return If(
                            j == 10,
                            [&execution_order, j] {
                              absl::StrAppend(&execution_order, "IfTrue,");
                              return TypeParam::instant_ok(j + 5);  // passes 15
                            },
                            [&execution_order] {
                              absl::StrAppend(&execution_order,
                                              "UnreachableFalse,");
                              return TypeParam::template instant_fail<int>();
                            });
                      });
      },
      // Step 2: Race enclosing a Seq
      [&execution_order](int i) {
        absl::StrAppend(&execution_order, "RaceIn(", i, "),");  // i = 15
        return Race(
            // Branch 1: A promise that never resolves
            [&execution_order]() -> decltype(TypeParam::ok(1)) {
              absl::StrAppend(&execution_order, "RacePending,");
              return Pending{};
            },
            // Branch 2: A sequence that wins the race
            Seq(
                [&execution_order, i] {
                  absl::StrAppend(&execution_order, "RaceWin(Seq),");
                  return i * 2;  // 15 * 2 = 30
                },
                [&execution_order](int j) {
                  absl::StrAppend(&execution_order, "SeqInRaceOut(", j, "),");
                  return TypeParam::instant_ok(j);  // passes 30
                }));
      },
      // Step 3: Seq enclosing a Join of Map and Seq
      [&execution_order](int i) {
        absl::StrAppend(&execution_order, "SeqJoinIn(", i, "),");  // i = 30
        return Seq(
            Join(
                // Left side: Map combinator directly in Join
                Map(
                    [&execution_order] {
                      absl::StrAppend(&execution_order, "JoinLeft(Map),");
                      return 2;
                    },
                    [](int x) { return x; }),
                // Right side: Seq combinator directly in Join
                Seq(
                    [&execution_order] {
                      absl::StrAppend(&execution_order, "JoinRight(Seq),");
                      return 10;
                    },
                    [](int x) { return x; })),
            // Both sides correctly resolve to ints and converge as
            // std::tuple<int, int>
            [&execution_order, i](std::tuple<int, int> t) {
              absl::StrAppend(&execution_order, "SeqJoinOut,");
              return TypeParam::instant_ok(i + std::get<0>(t) + std::get<1>(t));
            });
      },
      // Step 4: Map wrapping a TrySeq
      // Proves Map can correctly pass a failable TypeParam status up the chain
      [&execution_order](int i) {
        absl::StrAppend(&execution_order, "MapWrapIn(", i, "),");  // i = 42
        return Map(TrySeq(TypeParam::instant_ok(i),
                          [&execution_order](int j) {
                            absl::StrAppend(&execution_order, "InnerTrySeq(", j,
                                            "),");
                            return TypeParam::instant_ok(j + 8);  // 42 + 8 = 50
                          }),
                   [&execution_order](auto failable_result) {
                     absl::StrAppend(&execution_order, "MapWrapOut,");
                     return failable_result;
                     // Passes the TypeParam payload to outer TrySeq
                   });
      },
      // Step 5: Loop (Iterative computation inside TrySeq)
      // Proves loop iteration works inside TrySeq chain
      [&execution_order](int i) {
        absl::StrAppend(&execution_order, "LoopIn(", i, "),");  // i = 50
        return Seq(Loop([&execution_order, count = 0, sum = i]() mutable {
                     return [&execution_order, &count, &sum]() -> LoopCtl<int> {
                       absl::StrAppend(&execution_order, "LoopIter(", count,
                                       "),");
                       if (count < 3) {
                         sum += 10;
                         count++;
                         return Continue{};
                       }
                       return sum;  // 50 + 10 + 10 + 10 = 80
                     };
                   }),
                   [&execution_order](int final_sum) {
                     absl::StrAppend(&execution_order, "LoopOut,");
                     return TypeParam::instant_ok(final_sum);
                   });
      });

  // Evaluate the final output of the entire mega-chain
  EXPECT_EQ(promise(), TypeParam::ok(80));

  // Verify the exact control flow across every nested combinator layer
  std::string expected_execution_order =
      "DeepTrySeqIn(1),IfIn(10),IfTrue,"
      "RaceIn(15),RacePending,RaceWin(Seq),SeqInRaceOut(30),"
      "SeqJoinIn(30),JoinLeft(Map),JoinRight(Seq),SeqJoinOut,"
      "MapWrapIn(42),InnerTrySeq(42),MapWrapOut,"
      "LoopIn(50),LoopIter(0),LoopIter(1),LoopIter(2),LoopIter(3),LoopOut,";
  EXPECT_STREQ(execution_order.c_str(), expected_execution_order.c_str());
}

TYPED_TEST(TrySeqTest, HeterogeneousChainWithPending) {
  std::string execution_order;
  bool pending_step2 = true;
  bool pending_step3 = true;
  bool pending_step4 = true;

  auto promise = TrySeq(
      TypeParam::instant_ok(1),
      // Step 2: Join of Map and a Race that suspends across polls
      [&execution_order, &pending_step2](int i) {
        absl::StrAppend(&execution_order, "Step2In(", i, "),");
        return Seq(
            Join(Map(
                     [&execution_order] {
                       absl::StrAppend(&execution_order, "JoinLeft,");
                       return 10;
                     },
                     [](int x) { return x; }),
                 // Right branch of Join (A Race that suspends initially)
                 Race(
                     [&execution_order, &pending_step2]() -> Poll<int> {
                       absl::StrAppend(&execution_order, "Race1,");
                       if (pending_step2) {
                         absl::StrAppend(&execution_order, "P,");
                         return Pending{};
                       }
                       absl::StrAppend(&execution_order, "Ready,");
                       return 20;
                     },
                     [&execution_order]() -> Poll<int> {
                       absl::StrAppend(&execution_order, "Race2,");
                       return Pending{};  // Always stays pending
                     })),
            // Convergence into next TypeParam value
            [&execution_order, i](std::tuple<int, int> t) {
              absl::StrAppend(&execution_order, "Step2Out,");
              return TypeParam::instant_ok(i + std::get<0>(t) + std::get<1>(t));
            });
      },
      // Step 3: If branching into an asynchronous multi-stage Seq
      [&execution_order, &pending_step3](int i) {
        absl::StrAppend(&execution_order, "Step3In(", i, "),");  // i = 31
        return If(i == 31,
                  Seq(
                      [&execution_order, &pending_step3]() -> Poll<int> {
                        absl::StrAppend(&execution_order, "IfTruePending,");
                        if (pending_step3) {
                          absl::StrAppend(&execution_order, "P,");
                          return Pending{};
                        }
                        absl::StrAppend(&execution_order, "Ready,");
                        return 9;
                      },
                      [&execution_order, i](int val) {
                        absl::StrAppend(&execution_order, "IfTrueSeqOut,");
                        return TypeParam::instant_ok(i + val);  // 31 + 9 = 40
                      }),
                  [&execution_order] {
                    absl::StrAppend(&execution_order, "UnreachableFalse,");
                    return TypeParam::template instant_fail<int>();
                  });
      },
      // Step 4: Loop that yields Pending on an intermediate iteration
      [&execution_order, &pending_step4](int i) {
        absl::StrAppend(&execution_order, "Step4In(", i, "),");  // i = 40
        return Seq(Loop([&execution_order, &pending_step4, iter = 0,
                         sum = i]() mutable {
                     return [&execution_order, &pending_step4, &iter,
                             &sum]() -> Poll<LoopCtl<int>> {
                       absl::StrAppend(&execution_order, "LoopIter(", iter,
                                       "),");
                       if (iter == 0) {
                         iter++;
                         sum += 5;
                         return Continue{};
                       }
                       if (iter == 1 && pending_step4) {
                         absl::StrAppend(&execution_order, "LoopP,");
                         return Pending{};
                       }
                       if (iter == 1) {
                         iter++;
                         sum += 5;
                         return Continue{};
                       }
                       absl::StrAppend(&execution_order, "LoopDone,");
                       return sum;  // 40 + 5 + 5 = 50
                     };
                   }),
                   [&execution_order](int final_sum) {
                     absl::StrAppend(&execution_order, "Step4Out,");
                     return TypeParam::instant_ok(final_sum);  // passes 50
                   });
      },
      // Step 5: Map wrapping a nested TrySeq to produce the final typed result
      [&execution_order](int i) {
        absl::StrAppend(&execution_order, "Step5In(", i, "),");  // i = 50
        return Map(
            TrySeq(TypeParam::instant_ok(i),
                   [&execution_order](int j) {
                     absl::StrAppend(&execution_order, "InnerTrySeq(", j, "),");
                     return TypeParam::instant_ok(j + 10);  // 50 + 10 = 60
                   }),
            [&execution_order](auto result) {
              absl::StrAppend(&execution_order, "Step5Out,");
              return result;
            });
      });

  // --- FIRST POLL: Suspends in Step 2 (Race) ---
  auto p1 = promise();
  EXPECT_TRUE(p1.pending());
  EXPECT_STREQ(execution_order.c_str(), "Step2In(1),JoinLeft,Race1,P,Race2,");

  // Allow Step 2 to resolve
  execution_order.clear();
  pending_step2 = false;

  // --- SECOND POLL: Resumes Step 2, suspends in Step 3 (If's inner Seq) ---
  auto p2 = promise();
  EXPECT_TRUE(p2.pending());
  // Step 2 Join tunnels to Race without re-executing JoinLeft
  EXPECT_STREQ(execution_order.c_str(),
               "Race1,Ready,Step2Out,Step3In(31),IfTruePending,P,");

  // Allow Step 3 to resolve
  execution_order.clear();
  pending_step3 = false;

  // --- THIRD POLL: Resumes Step 3, suspends in Step 4 (Loop iteration 1) ---
  auto p3 = promise();
  EXPECT_TRUE(p3.pending());
  EXPECT_STREQ(execution_order.c_str(),
               "IfTruePending,Ready,IfTrueSeqOut,Step4In(40),LoopIter(0),"
               "LoopIter(1),LoopP,");

  // Allow Step 4 to resolve
  execution_order.clear();
  pending_step4 = false;

  // --- FOURTH POLL: Resumes Loop, completes Step 5 and entire chain ---
  auto p4 = promise();
  EXPECT_EQ(p4, TypeParam::ok(60));
  EXPECT_STREQ(execution_order.c_str(),
               "LoopIter(1),LoopIter(2),LoopDone,Step4Out,Step5In(50),"
               "InnerTrySeq(50),Step5Out,");
}

TEST(TrySeqTest, ToProto) {
  auto x = TrySeq([]() { return 42; },
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

TEST(TrySeqIterTest, Ok) {
  std::vector<int> v{1, 2, 3, 4, 5};
  EXPECT_EQ(TrySeqIter(v.begin(), v.end(), 0,
                       [](int elem, int accum) {
                         return [elem, accum]() -> absl::StatusOr<int> {
                           return elem + accum;
                         };
                       })(),
            Poll<absl::StatusOr<int>>(15));
}

TEST(TrySeqIterTest, ErrorAt3) {
  std::vector<int> v{1, 2, 3, 4, 5};
  EXPECT_EQ(TrySeqIter(v.begin(), v.end(), 0,
                       [](int elem, int accum) {
                         return [elem, accum]() -> absl::StatusOr<int> {
                           if (elem < 3) {
                             return elem + accum;
                           }
                           if (elem == 3) {
                             return absl::CancelledError();
                           }
                           abort();  // unreachable
                         };
                       })(),
            Poll<absl::StatusOr<int>>(absl::CancelledError()));
}

TEST(TrySeqContainer, Ok) {
  std::vector<std::unique_ptr<int>> v;
  v.emplace_back(std::make_unique<int>(1));
  v.emplace_back(std::make_unique<int>(2));
  v.emplace_back(std::make_unique<int>(3));
  int expect = 1;
  auto p = TrySeqContainer(std::move(v), Empty{},
                           [&expect](const std::unique_ptr<int>& i, Empty) {
                             EXPECT_EQ(*i, expect);
                             ++expect;
                           });
  EXPECT_THAT(p(), IsReady());
  EXPECT_EQ(expect, 4);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
