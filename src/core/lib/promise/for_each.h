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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_FOR_EACH_H
#define GRPC_SRC_CORE_LIB_PROMISE_FOR_EACH_H

#include <stdint.h>

#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/trace.h"

namespace grpc_core {

namespace for_each_detail {

// Done creates statuses for the end of the iteration. It's templated on the
// type of the result of the ForEach loop, so that we can introduce new types
// easily.
template <typename T>
struct Done;

template <>
struct Done<absl::Status> {
  static absl::Status Make(bool cancelled) {
    return cancelled ? absl::CancelledError() : absl::OkStatus();
  }
};

template <>
struct Done<StatusFlag> {
  static StatusFlag Make(bool cancelled) { return StatusFlag(!cancelled); }
};

template <typename T, typename SfinaeVoid = void>
struct NextValueTraits;

enum class NextValueType {
  kValue,
  kEndOfStream,
  kError,
};

template <typename T>
struct NextValueTraits<T, absl::void_t<typename T::value_type>> {
  using Value = typename T::value_type;

  static NextValueType Type(const T& t) {
    if (t.has_value()) return NextValueType::kValue;
    if (t.cancelled()) return NextValueType::kError;
    return NextValueType::kEndOfStream;
  }

  static Value& MutableValue(T& t) { return *t; }
};

template <typename T>
struct NextValueTraits<ValueOrFailure<absl::optional<T>>> {
  using Value = T;

  static NextValueType Type(const ValueOrFailure<absl::optional<T>>& t) {
    if (t.ok()) {
      if (t.value().has_value()) return NextValueType::kValue;
      return NextValueType::kEndOfStream;
    }
    return NextValueType::kError;
  }

  static Value& MutableValue(ValueOrFailure<absl::optional<T>>& t) {
    return **t;
  }
};

template <typename Reader, typename Action>
class ForEach {
 private:
  using ReaderNext = decltype(std::declval<Reader>().Next());
  using ReaderResult =
      typename PollTraits<decltype(std::declval<ReaderNext>()())>::Type;
  using ReaderResultValue = typename NextValueTraits<ReaderResult>::Value;
  using ActionFactory =
      promise_detail::RepeatedPromiseFactory<ReaderResultValue, Action>;
  using ActionPromise = typename ActionFactory::Promise;

 public:
  using Result =
      typename PollTraits<decltype(std::declval<ActionPromise>()())>::Type;
  ForEach(Reader reader, Action action, DebugLocation whence = {})
      : reader_(std::move(reader)),
        action_factory_(std::move(action)),
        whence_(whence) {
    Construct(&reader_next_, reader_.Next());
  }
  ~ForEach() {
    if (reading_next_) {
      Destruct(&reader_next_);
    } else {
      Destruct(&in_action_);
    }
  }

  ForEach(const ForEach&) = delete;
  ForEach& operator=(const ForEach&) = delete;
  ForEach(ForEach&& other) noexcept
      : reader_(std::move(other.reader_)),
        action_factory_(std::move(other.action_factory_)),
        whence_(other.whence_) {
    DCHECK(reading_next_);
    DCHECK(other.reading_next_);
    Construct(&reader_next_, std::move(other.reader_next_));
  }
  ForEach& operator=(ForEach&& other) noexcept {
    DCHECK(reading_next_);
    DCHECK(other.reading_next_);
    reader_ = std::move(other.reader_);
    action_factory_ = std::move(other.action_factory_);
    reader_next_ = std::move(other.reader_next_);
    whence_ = other.whence_;
    return *this;
  }

  Poll<Result> operator()() {
    if (reading_next_) return PollReaderNext();
    return PollAction();
  }

 private:
  struct InAction {
    InAction(ActionPromise promise, ReaderResult result)
        : promise(std::move(promise)), result(std::move(result)) {}
    ActionPromise promise;
    ReaderResult result;
  };

  std::string DebugTag() {
    return absl::StrCat(GetContext<Activity>()->DebugTag(), " FOR_EACH[0x",
                        reinterpret_cast<uintptr_t>(this), "@", whence_.file(),
                        ":", whence_.line(), "]: ");
  }

  Poll<Result> PollReaderNext() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%s PollReaderNext", DebugTag().c_str());
    }
    auto r = reader_next_();
    if (auto* p = r.value_if_ready()) {
      switch (NextValueTraits<ReaderResult>::Type(*p)) {
        case NextValueType::kValue: {
          if (grpc_trace_promise_primitives.enabled()) {
            gpr_log(GPR_INFO, "%s PollReaderNext: got value",
                    DebugTag().c_str());
          }
          Destruct(&reader_next_);
          auto action = action_factory_.Make(
              std::move(NextValueTraits<ReaderResult>::MutableValue(*p)));
          Construct(&in_action_, std::move(action), std::move(*p));
          reading_next_ = false;
          return PollAction();
        }
        case NextValueType::kEndOfStream: {
          if (grpc_trace_promise_primitives.enabled()) {
            gpr_log(GPR_INFO, "%s PollReaderNext: got end of stream",
                    DebugTag().c_str());
          }
          return Done<Result>::Make(false);
        }
        case NextValueType::kError: {
          if (grpc_trace_promise_primitives.enabled()) {
            gpr_log(GPR_INFO, "%s PollReaderNext: got error",
                    DebugTag().c_str());
          }
          return Done<Result>::Make(true);
        }
      }
    }
    return Pending();
  }

  Poll<Result> PollAction() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%s PollAction", DebugTag().c_str());
    }
    auto r = in_action_.promise();
    if (auto* p = r.value_if_ready()) {
      if (IsStatusOk(*p)) {
        Destruct(&in_action_);
        Construct(&reader_next_, reader_.Next());
        reading_next_ = true;
        return PollReaderNext();
      } else {
        return std::move(*p);
      }
    }
    return Pending();
  }

  GPR_NO_UNIQUE_ADDRESS Reader reader_;
  GPR_NO_UNIQUE_ADDRESS ActionFactory action_factory_;
  GPR_NO_UNIQUE_ADDRESS DebugLocation whence_;
  bool reading_next_ = true;
  union {
    ReaderNext reader_next_;
    InAction in_action_;
  };
};

}  // namespace for_each_detail

/// For each item acquired by calling Reader::Next, run the promise Action.
template <typename Reader, typename Action>
for_each_detail::ForEach<Reader, Action> ForEach(Reader reader, Action action,
                                                 DebugLocation whence = {}) {
  return for_each_detail::ForEach<Reader, Action>(std::move(reader),
                                                  std::move(action), whence);
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_FOR_EACH_H
