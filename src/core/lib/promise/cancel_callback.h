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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_CANCEL_CALLBACK_H
#define GRPC_SRC_CORE_LIB_PROMISE_CANCEL_CALLBACK_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/promise/detail/promise_like.h"

namespace grpc_core {

namespace cancel_callback_detail {

template <typename Fn>
class Handler {
 public:
  explicit Handler(Fn fn) : fn_(std::move(fn)) {}
  Handler(const Handler&) = delete;
  Handler& operator=(const Handler&) = delete;
  ~Handler() {
    if (!done_) {
      fn_();
    }
  }
  Handler(Handler&& other) noexcept
      : fn_(std::move(other.fn_)), done_(other.done_) {
    other.done_ = true;
  }
  Handler& operator=(Handler&& other) noexcept {
    fn_ = std::move(other.fn_);
    done_ = other.done_;
    other.done_ = true;
  }

  void Done() { done_ = true; }

 private:
  Fn fn_;
  bool done_ = false;
};

}  // namespace cancel_callback_detail

// Wrap main_fn so that it calls cancel_fn if the promise is destroyed prior to
// completion.
// Returns a promise with the same result type as main_fn.
template <typename MainFn, typename CancelFn>
auto OnCancel(MainFn main_fn, CancelFn cancel_fn) {
  return [on_cancel =
              cancel_callback_detail::Handler<CancelFn>(std::move(cancel_fn)),
          main_fn = promise_detail::PromiseLike<MainFn>(
              std::move(main_fn))]() mutable {
    auto r = main_fn();
    if (r.ready()) {
      on_cancel.Done();
    }
    return r;
  };
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_CANCEL_CALLBACK_H
