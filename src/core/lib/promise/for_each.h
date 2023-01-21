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

#include <grpc/support/port_platform.h>

#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/types/variant.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace for_each_detail {

// Done creates statuses for the end of the iteration. It's templated on the
// type of the result of the ForEach loop, so that we can introduce new types
// easily.
template <typename T>
struct Done;

template <>
struct Done<absl::Status> {
  static absl::Status Make() { return absl::OkStatus(); }
};

template <typename Reader, typename Action>
class ForEach {
 private:
  using ReaderNext = decltype(std::declval<Reader>().Next());
  using ReaderResult =
      typename PollTraits<decltype(std::declval<ReaderNext>()())>::Type;
  using ReaderResultValue = typename ReaderResult::value_type;
  using ActionFactory =
      promise_detail::RepeatedPromiseFactory<ReaderResultValue, Action>;
  using ActionPromise = typename ActionFactory::Promise;

 public:
  using Result =
      typename PollTraits<decltype(std::declval<ActionPromise>()())>::Type;
  ForEach(Reader reader, Action action)
      : reader_(std::move(reader)), action_factory_(std::move(action)) {
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
        action_factory_(std::move(other.action_factory_)) {
    GPR_DEBUG_ASSERT(reading_next_);
    GPR_DEBUG_ASSERT(other.reading_next_);
    Construct(&reader_next_, std::move(other.reader_next_));
  }
  ForEach& operator=(ForEach&& other) noexcept {
    GPR_DEBUG_ASSERT(reading_next_);
    GPR_DEBUG_ASSERT(other.reading_next_);
    reader_ = std::move(other.reader_);
    action_factory_ = std::move(other.action_factory_);
    reader_next_ = std::move(other.reader_next_);
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

  Poll<Result> PollReaderNext() {
    auto r = reader_next_();
    if (auto* p = absl::get_if<kPollReadyIdx>(&r)) {
      if (p->has_value()) {
        Destruct(&reader_next_);
        auto action = action_factory_.Make(std::move(**p));
        Construct(&in_action_, std::move(action), std::move(*p));
        reading_next_ = false;
        return PollAction();
      } else {
        return Done<Result>::Make();
      }
    }
    return Pending();
  }

  Poll<Result> PollAction() {
    auto r = in_action_.promise();
    if (auto* p = absl::get_if<kPollReadyIdx>(&r)) {
      if (p->ok()) {
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
  bool reading_next_ = true;
  union {
    ReaderNext reader_next_;
    InAction in_action_;
  };
};

}  // namespace for_each_detail

/// For each item acquired by calling Reader::Next, run the promise Action.
template <typename Reader, typename Action>
for_each_detail::ForEach<Reader, Action> ForEach(Reader reader, Action action) {
  return for_each_detail::ForEach<Reader, Action>(std::move(reader),
                                                  std::move(action));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_FOR_EACH_H
