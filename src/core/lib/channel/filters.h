// Copyright 2024 gRPC authors.
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

#ifndef GRPC_FILTER_RUNNER_H
#define GRPC_FILTER_RUNNER_H

#include <cstdint>
#include <memory>

#include "channel_stack.h"
#include "promise_based_filter.h"

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

namespace filters_detail {

struct Filter {
  void* channel_data;
  size_t call_offset;
  void (*call_init)(void* call_data, void* channel_data);
  void (*call_destroy)(void* call_data);
};

// Zero-member wrapper to make sure that Call always has a constructor
// that takes a channel pointer (even if it's thrown away)
template <typename Derived, typename SfinaeVoid = void>
class CallWrapper;

template <typename Derived>
class CallWrapper<Derived, absl::void_t<decltype(typename Derived::Call(
                               std::declval<Derived*>()))>>
    : public Derived::Call {
 public:
  explicit CallWrapper(Derived* channel) : Derived::Call(channel) {}
};

template <typename Derived>
class CallWrapper<Derived, absl::void_t<decltype(typename Derived::Call())>>
    : public Derived::Call {
 public:
  explicit CallWrapper(Derived*) : Derived::Call() {}
};

template <typename FilterType>
Filter MakeFilter(void* channel_data, size_t call_offset) {
  static_assert(
      sizeof(typename FilterType::Call) == sizeof(CallWrapper<FilterType>),
      "CallWrapper must be the same size as Call");
  return Filter{
      channel_data,
      call_offset,
      [](void* call_data, void* channel_data) {
        new (call_data) CallWrapper<FilterType>(
            static_cast<FilterType*>(static_cast<FilterType*>(channel_data)));
      },
      [](void* call_data) {
        static_cast<CallWrapper<FilterType>*>(call_data)->~CallWrapper();
      },
  };
}

template <typename T>
struct ResultOr {
  T ok;
  ServerMetadataHandle error;
};

template <typename R, typename V>
struct Operator {
  using Result = R;
  void* channel_data;
  size_t call_offset;
  Poll<R> (*promise_init)(void* promise_data, void* call_data,
                          void* channel_data, V value);
  Poll<R> (*poll)(void* promise_data);
  void (*early_destroy)(void* promise_data);
};

template <typename T>
using FallibleOperator = Operator<ResultOr<T>, T>;
template <typename T>
using InfallibleOperator = Operator<T, T>;

struct Finalizer {
  void* channel_data;
  size_t call_offset;
  void (*final)(void* call_data, void* channel_data,
                const grpc_call_final_info* final_info);
};

template <typename Op>
struct Layout {
  size_t promise_size;
  size_t promise_alignment;
  std::vector<Op> ops;
};

struct StackData {
  std::vector<Filter> filters;
  Layout<FallibleOperator<ClientMetadataHandle>> client_initial_metadata;
  Layout<FallibleOperator<ServerMetadataHandle>> server_initial_metadata;
  Layout<FallibleOperator<MessageHandle>> client_to_server_messages;
  Layout<FallibleOperator<MessageHandle>> server_to_client_messages;
  Layout<InfallibleOperator<ServerMetadataHandle>> server_trailing_metadata;
  std::vector<Finalizer> finalizers;
};

enum class PipeState : uint8_t {
  // Waiting to be sent
  kPending,
  // Sent, but not yet received
  kQueued,
  // Trying to receive, but not yet sent
  kWaiting,
  // Processing through filters
  kProcessing,
  // Closed sending
  kClosed,
  // Closed due to failure
  kError
};

template <typename T>
class FallibleRunner {
 public:
  FallibleRunner(void* call_data, T* value,
                 const Layout<FallibleOperator<T>>& layout,
                 Latch<ServerMetadataHandle>& cancel_latch)
      : call_data_(call_data), layout_(&layout), cancel_latch_(&cancel_latch) {
    if (cancel_latch.is_set()) {
      state_ = State::kDoneError;
      return;
    }
    if (value->get() == nullptr) {
      state_ = State::kWaiting;
      promise_data_ = value;
      return;
    }
    state_ = Start(std::move(*value));
  }

  ~FallibleRunner() {
    switch (state_) {
      case State::kRunning:
        layout_->ops[index_].early_destroy(promise_data_);
        gpr_free_aligned(promise_data_);
        break;
      case State::kDoneOk: {
        T(static_cast<typename T::element_type>(promise_data_));
      } break;
      case State::kDoneError:
      case State::kDoneReturned:
      case State::kWaiting:
        break;
    }
  }

  FallibleRunner(const FallibleRunner&) = delete;
  FallibleRunner& operator=(const FallibleRunner&) = delete;
  FallibleRunner(FallibleRunner&& other) noexcept { MoveFrom(other); }
  FallibleRunner& operator=(FallibleRunner&& other) noexcept {
    MoveFrom(other);
    return *this;
  }

  Poll<ValueOrFailure<T>> operator()() {
    switch (state_) {
      case State::kRunning:
        break;
      case State::kDoneOk:
        state_ = State::kDoneReturned;
        return T(static_cast<typename T::element_type>(promise_data_));
      case State::kDoneError:
        state_ = State::kDoneReturned;
        return Failure{};
      case State::kDoneReturned:
        Crash("FallibleRunner called after completion");
      case State::kWaiting: {
        auto* value = static_cast<T*>(promise_data_);
        if (value->get() == nullptr) return Pending{};
        switch (Start(std::move(*value))) {
          case State::kRunning:
            break;
          case State::kDoneOk:
            state_ = State::kDoneReturned;
            return T(static_cast<typename T::element_type>(promise_data_));
          case State::kDoneError:
            state_ = State::kDoneReturned;
            return Failure{};
          case State::kDoneReturned:
            Crash("FallibleRunner::Start returned kReturned");
          case State::kWaiting:
            Crash("FallibleRunner::Start returned kWaiting");
        }
      }
    }
    const auto* ops = layout_->ops.data();
    auto r = ops[index_].poll(promise_data_);
    while (true) {
      auto* p = r.value_if_ready();
      if (p == nullptr) return Pending();
      if (p->ok == nullptr) {
        if (!cancel_latch_->is_set()) cancel_latch_->Set(std::move(p->error));
        gpr_free_aligned(promise_data_);
        state_ = State::kDoneReturned;
        return Failure{};
      }
      auto value = std::move(r.ok);
      ++index_;
      if (index_ == count_) {
        gpr_free_aligned(promise_data_);
        state_ = State::kDoneReturned;
        return std::move(value);
      }
      r = ops[index_].promise_init(promise_data_, call_data_,
                                   ops[index_].channel_data, std::move(value));
    }
  }

 private:
  enum class State : uint8_t {
    kWaiting,
    kRunning,
    kDoneOk,
    kDoneError,
    kDoneReturned,
  };

  void MoveFrom(FallibleRunner&& other) noexcept {
    call_data_ = other.call_data_;
    promise_data_ = other.promise_data_;
    layout_ = other.layout_;
    cancel_latch_ = other.cancel_latch_;
    index_ = other.index_;
    count_ = other.count_;
    state_ = std::exchange(other.state_, State::kDoneReturned);
  }

  State Start(T value) {
    if (count_ == 0) GPR_DEBUG_ASSERT(promise_size == 0);
    const auto* ops = layout_->ops.data();
    if (layout_->promise_size == 0) {
      for (size_t i = 0; i < count_; i++) {
        auto r = ops[i].promise_init(nullptr, call_data_, ops[i].channel_data);
        auto* p = r.value_if_ready();
        GPR_DEBUG_ASSERT(p != nullptr);
        if (p->ok != nullptr) {
          value = std::move(r.ok);
        } else {
          cancel_latch_->Set(std::move(p->error));
          return State::kDoneError;
        }
      }
      promise_data_ = value->release();
      return State::kDoneOk;
    }
    promise_data_ =
        gpr_malloc_aligned(layout_->promise_size, layout_->promise_alignment);
    for (size_t i = 0; i < count_; i++) {
      auto r =
          ops[i].promise_init(promise_data_, call_data_, ops[i].channel_data);
      auto* p = r.value_if_ready();
      if (p == nullptr) {
        index_ = i;
        return State::kRunning;
      }
      if (p->ok != nullptr) {
        value = std::move(r.ok);
      } else {
        cancel_latch_->Set(std::move(p->error));
        return State::kDoneError;
      }
    }
    gpr_free_aligned(promise_data_);
    promise_data_ = value->release();
    return State::kDoneOk;
  }

  void* call_data_;
  void* promise_data_;
  const Layout<FallibleOperator<T>>* layout_;
  Latch<ServerMetadataHandle>* cancel_latch_;
  uint8_t index_;
  uint8_t count_;
  State state_;
};

template <typename Op, typename FilterType,
          typename Op::Result (*impl)(typename FilterType::Call* call_data,
                                      FilterType* channel_data,
                                      ClientMetadataHandle value)>
Op MakeInstantaneous(FilterType* channel_data, size_t call_offset) {
  return Op{
      channel_data,
      call_offset,
      [](void* promise_data, void* call_data, void* channel_data, auto value) {
        return Poll<typename Op::Result>{
            impl(static_cast<typename FilterType::Call*>(call_data),
                 static_cast<FilterType*>(channel_data), std::move(value))};
      },
      nullptr,
      nullptr,
  };
}

template <typename FilterType>
absl::optional<FallibleOperator<ClientMetadataHandle>>
MakeClientInitialMetadataOp(FilterType* channel_data, size_t call_offset,
                            const NoInterceptor* p) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnClientInitialMetadata);
  return absl::nullopt;
}

template <typename FilterType>
absl::optional<FallibleOperator<ClientMetadataHandle>>
MakeClientInitialMetadataOp(FilterType* channel_data, size_t call_offset,
                            void (FilterType::Call::*)(ClientMetadata&)) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnClientInitialMetadata);
  struct Impl {
    static ResultOr<ClientMetadataHandle> fn(
        typename FilterType::Call* call_data, FilterType* channel_data,
        ClientMetadataHandle value) {
      call_data->OnClientInitialMetadata(*value);
      return {std::move(value), nullptr};
    }
  };
  return MakeInstantaneous<FallibleOperator<ClientMetadataHandle>, FilterType,
                           &Impl::fn>(channel_data, call_offset);
}

}  // namespace filters_detail

// Execution environment for a stack of filters
class Filters {
 public:
  class Stack : public RefCounted<Stack> {
   private:
    friend class Filters;
    const filters_detail::StackData data_;
  };

  class StackBuilder {
   public:
    template <typename FilterType>
    void Add(FilterType* filter) {
      const size_t call_offset =
          OffsetForNextFilter(alignof(typename FilterType::Call),
                              sizeof(typename FilterType::Call));
      data_.filters.push_back(
          filters_detail::MakeFilter<FilterType>(filter, call_offset));
    }

   private:
    size_t OffsetForNextFilter(size_t alignment, size_t size);

    size_t current_call_offset_ = 0;
    size_t min_alignment_ = 1;
    filters_detail::StackData data_;
  };

 private:
  RefCountedPtr<Stack> stack_;

  filters_detail::PipeState client_initial_metadata_state_ =
      filters_detail::PipeState::kPending;
  filters_detail::PipeState server_initial_metadata_state_ =
      filters_detail::PipeState::kPending;
  filters_detail::PipeState client_to_server_message_state_ =
      filters_detail::PipeState::kPending;
  filters_detail::PipeState server_to_client_message_state_ =
      filters_detail::PipeState::kPending;
  filters_detail::PipeState server_trailing_metadata_state_ =
      filters_detail::PipeState::kPending;

  ClientMetadataHandle* client_initial_metadata_;
  ServerMetadataHandle* server_initial_metadata_;
  MessageHandle* client_to_server_message_;
  MessageHandle* server_to_client_message_;
  ServerMetadataHandle* server_trailing_metadata_;
};

}  // namespace grpc_core

#endif  // GRPC_FILTER_RUNNER_H
