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
  using Arg = V;
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
struct AddOp {
  size_t promise_size;
  size_t promise_alignment;
  Op op;
};

template <typename Op>
struct Layout {
  size_t promise_size;
  size_t promise_alignment;
  std::vector<Op> ops;

  void MaybeAdd(absl::optional<AddOp<Op>> op) {
    if (!op.has_value()) return;
    promise_size = std::max(promise_size, op->promise_size);
    promise_alignment = std::max(promise_alignment, op->promise_alignment);
    ops.push_back(std::move(op->op));
  }
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

template <typename T>
class PipeTransformer {
 public:
  bool IsRunning() const { return promise_data_ != nullptr; }
  Poll<ResultOr<T>> Start(const Layout<FallibleOperator<T>>* layout, T input,
                          void* call_data);
  Poll<ResultOr<T>> Step(void* call_data);

 private:
  Poll<ResultOr<T>> InitStep(T input, void* call_data);
  Poll<ResultOr<T>> ContinueStep(void* call_data);

  void* promise_data_ = nullptr;
  const FallibleOperator<T>* ops_;
  const FallibleOperator<T>* end_ops_;
};

template <typename Op, typename FilterType,
          typename Op::Result (*impl)(typename FilterType::Call* call_data,
                                      FilterType* channel_data,
                                      typename Op::Arg value)>
AddOp<Op> MakeInstantaneous(FilterType* channel_data, size_t call_offset) {
  return AddOp<Op>{
      0, 0,
      Op{
          channel_data,
          call_offset,
          [](void* promise_data, void* call_data, void* channel_data,
             auto value) {
            return Poll<typename Op::Result>{
                impl(static_cast<typename FilterType::Call*>(call_data),
                     static_cast<FilterType*>(channel_data), std::move(value))};
          },
          nullptr,
          nullptr,
      }};
}

template <typename FilterType>
absl::optional<AddOp<FallibleOperator<ClientMetadataHandle>>>
MakeClientInitialMetadataOp(FilterType* channel_data, size_t call_offset,
                            const NoInterceptor* p) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnClientInitialMetadata);
  return absl::nullopt;
}

template <typename FilterType>
absl::optional<AddOp<FallibleOperator<ClientMetadataHandle>>>
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

template <typename FilterType>
absl::optional<AddOp<FallibleOperator<ServerMetadataHandle>>>
MakeServerInitialMetadataOp(FilterType* channel_data, size_t call_offset,
                            void (FilterType::Call::*)(ServerMetadata&)) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnServerInitialMetadata);
  struct Impl {
    static ResultOr<ServerMetadataHandle> fn(
        typename FilterType::Call* call_data, FilterType* channel_data,
        ServerMetadataHandle value) {
      call_data->OnServerInitialMetadata(*value);
      return {std::move(value), nullptr};
    }
  };
  return MakeInstantaneous<FallibleOperator<ServerMetadataHandle>, FilterType,
                           &Impl::fn>(channel_data, call_offset);
}

template <typename FilterType>
absl::optional<AddOp<FallibleOperator<MessageHandle>>>
MakeClientToServerMessageOp(FilterType* channel_data, size_t call_offset,
                            void (FilterType::Call::*)(Message&)) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnClientToServerMessage);
  struct Impl {
    static ResultOr<MessageHandle> fn(typename FilterType::Call* call_data,
                                      FilterType* channel_data,
                                      MessageHandle value) {
      call_data->OnClientToServerMessage(*value);
      return {std::move(value), nullptr};
    }
  };
  return MakeInstantaneous<FallibleOperator<MessageHandle>, FilterType,
                           &Impl::fn>(channel_data, call_offset);
}

template <typename FilterType>
absl::optional<AddOp<FallibleOperator<MessageHandle>>>
MakeServerToClientMessageOp(FilterType* channel_data, size_t call_offset,
                            void (FilterType::Call::*)(Message&)) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnServerToClientMessage);
  struct Impl {
    static ResultOr<MessageHandle> fn(typename FilterType::Call* call_data,
                                      FilterType* channel_data,
                                      MessageHandle value) {
      call_data->OnServerToClientMessage(*value);
      return {std::move(value), nullptr};
    }
  };
  return MakeInstantaneous<FallibleOperator<MessageHandle>, FilterType,
                           &Impl::fn>(channel_data, call_offset);
}

template <typename FilterType>
absl::optional<AddOp<InfallibleOperator<ServerMetadataHandle>>>
MakeServerTrailingMetadataOp(FilterType* channel_data, size_t call_offset,
                             void (FilterType::Call::*)(ServerMetadata&)) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnServerTrailingMetadata);
  struct Impl {
    static ServerMetadataHandle fn(typename FilterType::Call* call_data,
                                   FilterType* channel_data,
                                   ServerMetadataHandle value) {
      call_data->OnServerTrailingMetadata(*value);
      return value;
    }
  };
  return MakeInstantaneous<InfallibleOperator<ServerMetadataHandle>, FilterType,
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
      data_.client_initial_metadata.MaybeAdd(
          filters_detail::MakeClientInitialMetadataOp(
              filter, call_offset, &FilterType::OnClientInitialMetadata));
      data_.server_initial_metadata.MaybeAdd(
          filters_detail::MakeServerInitialMetadataOp(
              filter, call_offset, &FilterType::OnServerInitialMetadata));
      data_.client_to_server_messages.MaybeAdd(
          filters_detail::MakeClientToServerMessageOp(
              filter, call_offset, &FilterType::OnClientToServerMessage));
      data_.server_to_client_messages.MaybeAdd(
          filters_detail::MakeServerToClientMessageOp(
              filter, call_offset, &FilterType::OnServerToClientMessage));
      data_.server_trailing_metadata.MaybeAdd(
          filters_detail::MakeServerTrailingMetadataOp(
              filter, call_offset, &FilterType::OnServerTrailingMetadata));
    }

   private:
    size_t OffsetForNextFilter(size_t alignment, size_t size);

    size_t current_call_offset_ = 0;
    size_t min_alignment_ = 1;
    filters_detail::StackData data_;
  };

  auto PushClientInitialMetadata(ClientMetadataHandle md);
  auto PullClientInitialMetadata();

 private:
  class PipeState {
   public:
    void BeginPush();
    void AbandonPush();
    Poll<StatusFlag> PollPush();
    Poll<StatusFlag> PollPullValue();
    void AckPullValue();

   private:
    enum class ValueState : uint8_t {
      // Nothing sending nor receiving
      kIdle,
      // Sent, but not yet received
      kQueued,
      // Trying to receive, but not yet sent
      kWaiting,
      // Ready to start processing, but not yet started
      // (we have the value to send through the pipe, the reader is waiting,
      // but it's not yet been polled)
      kReady,
      // Processing through filters
      kProcessing,
      // Closed sending
      kClosed,
      // Closed due to failure
      kError
    };
    IntraActivityWaiter wait_send_;
    IntraActivityWaiter wait_recv_;
    ValueState state_ = ValueState::kIdle;
  };

  template <PipeState(Filters::*state_ptr), void*(Filters::*push_ptr),
            typename T,
            filters_detail::Layout<filters_detail::FallibleOperator<T>>(
                filters_detail::StackData::*layout_ptr)>
  class PipePromise {
   public:
    class Push {
     public:
      Push(Filters* filters, T x) : filters_(filters), value_(std::move(x)) {
        state().BeginPush();
        push_slot() = this;
      }
      ~Push() {
        if (filters_ != nullptr) {
          state().AbandonPush();
          push_slot() = nullptr;
        }
      }

      Push(const Push&) = delete;
      Push& operator=(const Push&) = delete;
      Push(Push&& other)
          : filters_(std::exchange(other.filters_, nullptr)),
            value_(std::move(other.value_)) {
        if (filters_ != nullptr) {
          GPR_DEBUG_ASSERT(push_slot() == &other);
          push_slot() = this;
        }
      }

      Push& operator=(Push&&) = delete;

      Poll<StatusFlag> operator()() { return state().PollPush(); }

      T TakeValue() { return std::move(value_); }

     private:
      PipeState& state() { return filters_->*state_ptr; }
      void*& push_slot() { return filters_->*push_ptr; }

      Filters* filters_;
      T value_;
    };

    class Pull {
     public:
      explicit Pull(Filters* filters) : filters_(filters) {}

      Poll<ValueOrFailure<T>> operator()() {
        if (transformer_.IsRunning()) {
          return FinishPipeTransformer(transformer_.Step(filters_->call_data_));
        }
        auto p = state().PollPullValue();
        auto* r = p.value_if_ready();
        if (r == nullptr) return Pending{};
        if (!r->ok()) {
          filters_->CancelDueToFailedPipeOperation();
          return Failure{};
        }
        return FinishPipeTransformer(
            transformer_.Start(push()->TakeValue(), filters_->call_data_));
      }

     private:
      PipeState& state() { return filters_->*state_ptr; }
      Push* push() { return static_cast<Push*>(filters_->*push_ptr); }

      Poll<ValueOrFailure<T>> FinishPipeTransformer(
          Poll<filters_detail::ResultOr<T>> p) {
        auto* r = p.value_if_ready();
        if (r == nullptr) return Pending{};
        GPR_DEBUG_ASSERT(!transformer_.IsRunning());
        state().AckPullValue();
        if (r->ok != nullptr) return std::move(r->ok);
        filters_->Cancel(std::move(r->error));
        return Failure{};
      }

      Filters* filters_;
      filters_detail::PipeTransformer<T> transformer_;
    };
  };

  void CancelDueToFailedPipeOperation();
  void Cancel(ServerMetadataHandle error);

  const RefCountedPtr<Stack> stack_;

  PipeState client_initial_metadata_state_;
  PipeState server_initial_metadata_state_;
  PipeState client_to_server_message_state_;
  PipeState server_to_client_message_state_;

  void* call_data_;
  void* client_initial_metadata_ = nullptr;
  void* server_initial_metadata_ = nullptr;
  void* client_to_server_message_ = nullptr;
  void* server_to_client_message_ = nullptr;

  using ClientInitialMetadataPromises =
      PipePromise<&Filters::client_initial_metadata_state_,
                  &Filters::client_initial_metadata_, ClientMetadataHandle,
                  &filters_detail::StackData::client_initial_metadata>;
  using ServerInitialMetadataPromises =
      PipePromise<&Filters::server_initial_metadata_state_,
                  &Filters::server_initial_metadata_, ServerMetadataHandle,
                  &filters_detail::StackData::server_initial_metadata>;
  using ClientToServerMessagePromises =
      PipePromise<&Filters::client_to_server_message_state_,
                  &Filters::client_to_server_message_, MessageHandle,
                  &filters_detail::StackData::client_to_server_messages>;
  using ServerToClientMessagePromises =
      PipePromise<&Filters::server_to_client_message_state_,
                  &Filters::server_to_client_message_, MessageHandle,
                  &filters_detail::StackData::server_to_client_messages>;
};

inline auto Filters::PushClientInitialMetadata(ClientMetadataHandle md) {
  return [p = ClientInitialMetadataPromises::Push{
              this, std::move(md)}]() mutable { return p(); };
}

inline auto Filters::PullClientInitialMetadata() {
  return ClientInitialMetadataPromises::Pull{this};
}

}  // namespace grpc_core

#endif  // GRPC_FILTER_RUNNER_H
