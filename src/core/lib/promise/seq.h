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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_SEQ_H
#define GRPC_SRC_CORE_LIB_PROMISE_SEQ_H

#include <grpc/support/port_platform.h>
#include <stdlib.h>

#include <utility>

#include "src/core/lib/promise/detail/basic_seq.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/detail/seq_state.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/util/debug_location.h"

namespace grpc_core {

// Seq Promise combinator.
//
// Seq stands for sequence.
//
// Input :
// 1. The seq combinator needs minimum one promise as input.
// 2. The first input to seq combinator is a promise.
// 3. The remainng inputs to seq combinator are functors which return promises.
// The input type of the Nth functor should be the return value of the (N-1)th
// promise.
//
// Return :
// Polling the Seq Promise combinator returns Poll<T> where T is the type
// returned by the last promise in the list of input promises.
//
// Polling the Seq combinator works in the following way :
// Run the first promise. If it returns Pending{}, nothing else is executed.
// If the first promise returns a value, pass this result to the second functor,
// and run the returned promise. If it returns Pending{}, nothing else is
// executed. If it returns a value, pass this result to the third, and run the
// returned promise. etc. Return the final value.
//
// If any of the promises in the Seq chain returns a failure status, Seq will
// still proceed with the execution of the remaining promises. If you want the
// execution to stop when a failure status is received, use the TrySeq
// combinator instead.
//
// Promises in the Seq combinator are run in order, serially and on the same
// thread.
//
// Example :
// TEST(SeqTest, ThreeTypedPendingThens) {
//   std::string execution_order;
//   bool pending_a = true;
//   bool pending_b = true;
//   bool pending_c = true;
//   bool pending_d = true;
//   struct A { int a_ = -1; };
//   struct B { int b_ = -1; };
//   struct C { int c_ = -1; };
//   struct D { int d_ = -1; };
//
//   auto initial = [&execution_order, &pending_a]() -> Poll<A> {
//     absl::StrAppend(&execution_order, "0");
//     if (pending_a) {
//       absl::StrAppend(&execution_order, "P");
//       return Pending{};
//     }
//     absl::StrAppend(&execution_order, "a");
//     return A{100};
//   };
//
//   auto next1 = [&execution_order, &pending_b](A a) {
//     absl::StrAppend(&execution_order, "1");
//     return [&execution_order, &pending_b, a]() -> Poll<B> {
//       EXPECT_EQ(a.a_, 100);
//       if (pending_b) {
//         absl::StrAppend(&execution_order, "P");
//         return Pending{};
//       }
//       absl::StrAppend(&execution_order, "b");
//       return B{200};
//     };
//   };
//
//   auto next2 = [&execution_order, &pending_c](B b) {
//     absl::StrAppend(&execution_order, "2");
//     return [&execution_order, &pending_c, b]() -> Poll<C> {
//       EXPECT_EQ(b.b_, 200);
//       if (pending_c) {
//         absl::StrAppend(&execution_order, "P");
//         return Pending{};
//       }
//       absl::StrAppend(&execution_order, "c");
//       return C{300};
//     };
//   };
//
//   auto next3 = [&execution_order, &pending_d](C c) {
//     absl::StrAppend(&execution_order, "3");
//     return [&execution_order, &pending_d, c]() -> Poll<D> {
//       EXPECT_EQ(c.c_, 300);
//       if (pending_d) {
//         absl::StrAppend(&execution_order, "P");
//         return Pending{};
//       }
//       absl::StrAppend(&execution_order, "d");
//       return D{400};
//     };
//   };
//
//   auto seq_combinator = Seq(initial, next1, next2, next3);
//
//   auto retval = seq_combinator();
//   EXPECT_TRUE(retval.pending());
//   EXPECT_STREQ(execution_order.c_str(), "0P");
//
//   execution_order.clear();
//   pending_a = false;
//   retval = seq_combinator();
//   EXPECT_TRUE(retval.pending());
//   EXPECT_STREQ(execution_order.c_str(), "0a1P");
//
//   execution_order.clear();
//   pending_b = false;
//   retval = seq_combinator();
//   EXPECT_TRUE(retval.pending());
//   EXPECT_STREQ(execution_order.c_str(), "b2P");
//
//   execution_order.clear();
//   pending_c = false;
//   retval = seq_combinator();
//   EXPECT_TRUE(retval.pending());
//   EXPECT_STREQ(execution_order.c_str(), "c3P");
//
//   execution_order.clear();
//   pending_d = false;
//   retval = seq_combinator();
//   EXPECT_TRUE(retval.ready());
//   EXPECT_EQ(retval.value().d_, 400);
//   EXPECT_STREQ(execution_order.c_str(), "d");
// }

namespace promise_detail {

template <typename T>
struct SeqTraits {
  using UnwrappedType = T;
  using WrappedType = T;
  template <typename Next>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static auto CallFactory(Next* next,
                                                               T&& value) {
    return next->Make(std::forward<T>(value));
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static bool IsOk(const T&) {
    return true;
  }
  static const char* ErrorString(const T&) { abort(); }
  template <typename R>
  static R ReturnValue(T&&) {
    abort();
  }
  template <typename Result, typename PriorResult, typename RunNext>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static Poll<Result>
  CheckResultAndRunNext(PriorResult prior, RunNext run_next) {
    return run_next(std::move(prior));
  }
};

template <typename P, typename... Fs>
class Seq {
 public:
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION explicit Seq(P&& promise,
                                                    Fs&&... factories,
                                                    DebugLocation whence)
      : state_(std::forward<P>(promise), std::forward<Fs>(factories)...,
               whence) {}

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto operator()() {
    return state_.PollOnce();
  }

 private:
  SeqState<SeqTraits, P, Fs...> state_;
};

template <typename Iter, typename Factory, typename Argument>
using SeqIter = BasicSeqIter<SeqTraits, Iter, Factory, Argument>;

}  // namespace promise_detail

template <typename F>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline F Seq(F functor) {
  return functor;
}

template <typename F0, typename F1>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline promise_detail::Seq<F0, F1> Seq(
    F0 f0, F1 f1, DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1>(std::move(f0), std::move(f1), whence);
}

template <typename F0, typename F1, typename F2>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline promise_detail::Seq<F0, F1, F2> Seq(
    F0 f0, F1 f1, F2 f2, DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2>(std::move(f0), std::move(f1),
                                         std::move(f2), whence);
}

template <typename F0, typename F1, typename F2, typename F3>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline promise_detail::Seq<F0, F1, F2, F3>
Seq(F0 f0, F1 f1, F2 f2, F3 f3, DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline promise_detail::Seq<F0, F1, F2, F3,
                                                                F4>
Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4>(std::move(f0), std::move(f1),
                                                 std::move(f2), std::move(f3),
                                                 std::move(f4), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline promise_detail::Seq<F0, F1, F2, F3,
                                                                F4, F5>
Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION
    promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6>
    Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6,
        DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6, typename F7>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION
    promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7>
    Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7,
        DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), std::move(f7), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6, typename F7, typename F8>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION
    promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8>
    Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8,
        DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), std::move(f7), std::move(f8), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6, typename F7, typename F8, typename F9>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION
    promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9>
    Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9,
        DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), std::move(f7), std::move(f8), std::move(f9),
      whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6, typename F7, typename F8, typename F9,
          typename F10>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION
    promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10>
    Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9,
        F10 f10, DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), std::move(f7), std::move(f8), std::move(f9),
      std::move(f10), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6, typename F7, typename F8, typename F9,
          typename F10, typename F11>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION
    promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11>
    Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9,
        F10 f10, F11 f11, DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), std::move(f7), std::move(f8), std::move(f9),
      std::move(f10), std::move(f11), whence);
}

// Execute a sequence of operations of unknown length.
// Asynchronously:
//   for (element in (begin, end)) {
//     argument = wait_for factory(element, argument);
//   }
//   return argument;
template <typename Iter, typename Factory, typename Argument>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto SeqIter(Iter begin, Iter end,
                                                  Argument argument,
                                                  Factory factory) {
  return promise_detail::SeqIter<Iter, Factory, Argument>(
      begin, end, std::move(factory), std::move(argument));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_SEQ_H
