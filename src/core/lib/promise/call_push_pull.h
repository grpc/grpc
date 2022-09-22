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

#ifndef GRPC_CORE_LIB_PROMISE_CALL_PUSH_PULL_H
#define GRPC_CORE_LIB_PROMISE_CALL_PUSH_PULL_H

#include <grpc/support/port_platform.h>

#include <assert.h>

#include <type_traits>

#include "absl/types/variant.h"

#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace promise_detail {

template <typename FMain, typename FPush, typename FPull>
class CallPushPull {
 public:
  CallPushPull(FMain f_main, FPush f_push, FPull f_pull)
      : push_(std::move(f_push)), pull_(std::move(f_pull)) {
    Construct(&main_, std::move(f_main));
  }

  CallPushPull(const CallPushPull&) = delete;
  CallPushPull& operator=(const CallPushPull&) = delete;
  CallPushPull(CallPushPull&& other) noexcept
      : done_(other.done_),
        push_(std::move(other.push_)),
        pull_(std::move(other.pull_)) {
    assert(!done_.is_set(kDoneMain));
    Construct(&main_, std::move(other.main_));
  }

  CallPushPull& operator=(CallPushPull&& other) noexcept {
    assert(!done_.is_set(kDoneMain));
    done_ = other.done_;
    assert(!done_.is_set(kDoneMain));
    push_ = std::move(other.push_);
    main_ = std::move(other.main_);
    pull_ = std::move(other.pull_);
    return *this;
  }

  ~CallPushPull() {
    if (done_.is_set(kDoneMain)) {
      Destruct(&result_);
    } else {
      Destruct(&main_);
    }
  }

  using Result =
      typename PollTraits<decltype(std::declval<PromiseLike<FMain>>()())>::Type;

  Poll<Result> operator()() {
    if (!done_.is_set(kDonePush)) {
      auto p = push_();
      if (auto* status = absl::get_if<kPollReadyIdx>(&p)) {
        if (IsStatusOk(*status)) {
          done_.set(kDonePush);
        } else {
          return Result(std::move(*status));
        }
      }
    }
    if (!done_.is_set(kDoneMain)) {
      auto p = main_();
      if (auto* status = absl::get_if<kPollReadyIdx>(&p)) {
        if (IsStatusOk(*status)) {
          done_.set(kDoneMain);
          Destruct(&main_);
          Construct(&result_, std::move(*status));
        } else {
          return std::move(*status);
        }
      }
    }
    if (!done_.is_set(kDonePull)) {
      auto p = pull_();
      if (auto* status = absl::get_if<kPollReadyIdx>(&p)) {
        if (IsStatusOk(*status)) {
          done_.set(kDonePull);
        } else {
          return Result(std::move(*status));
        }
      }
    }
    if (done_.all()) return std::move(result_);
    return Pending{};
  }

 private:
  enum { kDonePull = 0, kDoneMain = 1, kDonePush = 2 };
  BitSet<3> done_;
  GPR_NO_UNIQUE_ADDRESS PromiseLike<FPush> push_;
  union {
    PromiseLike<FMain> main_;
    Result result_;
  };
  GPR_NO_UNIQUE_ADDRESS PromiseLike<FPull> pull_;
};

}  // namespace promise_detail

// For promises representing calls a common pattern emerges:
// There's a process pushing data down the stack, a process handling the main
// call part, and a process pulling data back up the stack.
//
// This can reasonably be represented by the right combinations of TryJoins and
// Maps, but since the structure is fundamental to the domain we introduce
// this simple helper to make it easier to write the common case.
//
// It takes three promises: the main call, the push and the pull.
// When polling, the push is polled first, then the main call (descending the
// stack), then the pull (as we ascend once more).
//
// This strategy minimizes repolls.
template <typename FMain, typename FPush, typename FPull>
promise_detail::CallPushPull<FMain, FPush, FPull> CallPushPull(FMain f_main,
                                                               FPush f_push,
                                                               FPull f_pull) {
  return promise_detail::CallPushPull<FMain, FPush, FPull>(
      std::move(f_main), std::move(f_push), std::move(f_pull));
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_CALL_PUSH_PULL_H
