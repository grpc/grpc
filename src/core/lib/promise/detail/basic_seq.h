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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_DETAIL_BASIC_SEQ_H
#define GRPC_SRC_CORE_LIB_PROMISE_DETAIL_BASIC_SEQ_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {
namespace promise_detail {

// Models a sequence of unknown size
// At each element, the accumulator A and the current value V is passed to some
// function of type IterTraits::Factory as f(V, IterTraits::Argument); f is
// expected to return a promise that resolves to IterTraits::Wrapped.
template <class IterTraits>
class BasicSeqIter {
 private:
  using Traits = typename IterTraits::Traits;
  using Iter = typename IterTraits::Iter;
  using Factory = typename IterTraits::Factory;
  using Argument = typename IterTraits::Argument;
  using IterValue = typename IterTraits::IterValue;
  using StateCreated = typename IterTraits::StateCreated;
  using State = typename IterTraits::State;
  using Wrapped = typename IterTraits::Wrapped;

 public:
  BasicSeqIter(Iter begin, Iter end, Factory f, Argument arg)
      : cur_(begin), end_(end), f_(std::move(f)) {
    if (cur_ == end_) {
      Construct(&result_, std::move(arg));
    } else {
      Construct(&state_, f_(*cur_, std::move(arg)));
    }
  }

  ~BasicSeqIter() {
    if (cur_ == end_) {
      Destruct(&result_);
    } else {
      Destruct(&state_);
    }
  }

  BasicSeqIter(const BasicSeqIter& other) = delete;
  BasicSeqIter& operator=(const BasicSeqIter&) = delete;

  BasicSeqIter(BasicSeqIter&& other) noexcept
      : cur_(other.cur_), end_(other.end_), f_(std::move(other.f_)) {
    if (cur_ == end_) {
      Construct(&result_, std::move(other.result_));
    } else {
      Construct(&state_, std::move(other.state_));
    }
  }
  BasicSeqIter& operator=(BasicSeqIter&& other) noexcept {
    cur_ = other.cur_;
    end_ = other.end_;
    if (cur_ == end_) {
      Construct(&result_, std::move(other.result_));
    } else {
      Construct(&state_, std::move(other.state_));
    }
    return *this;
  }

  Poll<Wrapped> operator()() {
    if (cur_ == end_) {
      return std::move(result_);
    }
    return PollNonEmpty();
  }

 private:
  Poll<Wrapped> PollNonEmpty() {
    Poll<Wrapped> r = state_();
    if (r.pending()) return r;
    return Traits::template CheckResultAndRunNext<Wrapped>(
        std::move(r.value()), [this](Wrapped arg) -> Poll<Wrapped> {
          auto next = cur_;
          ++next;
          if (next == end_) {
            return std::move(arg);
          }
          cur_ = next;
          state_.~State();
          Construct(&state_,
                    Traits::template CallSeqFactory(f_, *cur_, std::move(arg)));
          return PollNonEmpty();
        });
  }

  Iter cur_;
  const Iter end_;
  GPR_NO_UNIQUE_ADDRESS Factory f_;
  union {
    GPR_NO_UNIQUE_ADDRESS State state_;
    GPR_NO_UNIQUE_ADDRESS Argument result_;
  };
};

}  // namespace promise_detail
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_DETAIL_BASIC_SEQ_H
