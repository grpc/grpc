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

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
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
struct Layout {
  size_t promise_size;
  size_t promise_alignment;
  std::vector<Op> ops;

  void Add(size_t filter_promise_size, size_t filter_promise_alignment, Op op) {
    if (!op.has_value()) return;
    promise_size = std::max(promise_size, filter_promise_size);
    promise_alignment = std::max(promise_alignment, filter_promise_alignment);
    ops.push_back(op);
  }

  void Reverse() { std::reverse(ops.begin(), ops.end()); }
};

struct StackData {
  size_t call_data_alignment = 0;
  size_t call_data_size = 0;
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
  PipeTransformer() = default;
  ~PipeTransformer();
  PipeTransformer(const PipeTransformer&) = delete;
  PipeTransformer& operator=(const PipeTransformer&) = delete;
  PipeTransformer(PipeTransformer&& other) {
    GPR_DEBUG_ASSERT(other.promise_data_ == nullptr);
  }
  PipeTransformer& operator=(PipeTransformer&& other) {
    GPR_DEBUG_ASSERT(other.promise_data_ == nullptr);
    GPR_DEBUG_ASSERT(promise_data_ == nullptr);
    return *this;
  }
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

template <typename T>
class InfalliblePipeTransformer {
 public:
  InfalliblePipeTransformer() = default;
  ~InfalliblePipeTransformer();
  InfalliblePipeTransformer(const InfalliblePipeTransformer&) = delete;
  InfalliblePipeTransformer& operator=(const InfalliblePipeTransformer&) =
      delete;
  InfalliblePipeTransformer(InfalliblePipeTransformer&& other) {
    GPR_DEBUG_ASSERT(other.promise_data_ == nullptr);
  }
  InfalliblePipeTransformer& operator=(InfalliblePipeTransformer&& other) {
    GPR_DEBUG_ASSERT(other.promise_data_ == nullptr);
    GPR_DEBUG_ASSERT(promise_data_ == nullptr);
    return *this;
  }
  bool IsRunning() const { return promise_data_ != nullptr; }
  Poll<T> Start(const Layout<InfallibleOperator<T>>* layout, T input,
                void* call_data);
  Poll<T> Step(void* call_data);

 private:
  Poll<T> InitStep(T input, void* call_data);
  Poll<T> ContinueStep(void* call_data);

  void* promise_data_ = nullptr;
  const InfallibleOperator<T>* ops_;
  const InfallibleOperator<T>* end_ops_;
};

template <typename Op, typename FilterType,
          typename Op::Result (*impl)(typename FilterType::Call* call_data,
                                      FilterType* channel_data,
                                      typename Op::Arg value)>
void AddInstantaneous(FilterType* channel_data, size_t call_offset,
                      Layout<Op>& layout) {
  layout.Add(
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
      });
}

template <typename FilterType, typename T, typename FunctionImpl,
          FunctionImpl impl, typename SfinaeVoid = void>
struct AddOpImpl;

template <typename FilterType, typename T, const NoInterceptor* which>
struct AddOpImpl<FilterType, T, const NoInterceptor*, which> {
  static void Add(FilterType*, size_t, Layout<FallibleOperator<T>>&) {}
  static void Add(FilterType*, size_t, Layout<InfallibleOperator<T>>&) {}
};

template <typename FilterType, typename T, void (FilterType::Call::*impl)(T)>
struct AddOpImpl<FilterType, T, void (FilterType::Call::*)(T), impl> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    to.Add(0, 0,
           FallibleOperator<T>{
               channel_data,
               call_offset,
               [](void*, void* call_data, void*, T value) -> Poll<ResultOr<T>> {
                 (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                     std::move(value));
                 return ResultOr<T>{std::move(value), nullptr};
               },
               nullptr,
               nullptr,
           });
  }
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<InfallibleOperator<T>>& to) {
    to.Add(0, 0,
           FallibleOperator<T>{
               channel_data,
               call_offset,
               [](void*, void* call_data, void*, T value) -> Poll<T> {
                 (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                     std::move(value));
                 return std::move(value);
               },
               nullptr,
               nullptr,
           });
  }
};

template <typename FilterType, typename T,
          void (FilterType::Call::*impl)(T, FilterType*)>
struct AddOpImpl<FilterType, T, void (FilterType::Call::*)(T, FilterType*),
                 impl> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    to.Add(0, 0,
           FallibleOperator<T>{
               channel_data,
               call_offset,
               [](void*, void* call_data, void* channel_data,
                  T value) -> Poll<ResultOr<T>> {
                 (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                     std::move(value), static_cast<FilterType*>(channel_data));
                 return ResultOr<T>{std::move(value), nullptr};
               },
               nullptr,
               nullptr,
           });
  }
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<InfallibleOperator<T>>& to) {
    to.Add(
        0, 0,
        FallibleOperator<T>{
            channel_data,
            call_offset,
            [](void*, void* call_data, void* channel_data, T value) -> Poll<T> {
              (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                  std::move(value), static_cast<FilterType*>(channel_data));
              return std::move(value);
            },
            nullptr,
            nullptr,
        });
  }
};

template <typename FilterType, typename T,
          absl::Status (FilterType::Call::*impl)(T)>
struct AddOpImpl<FilterType, T, absl::Status (FilterType::Call::*)(T), impl> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    to.Add(
        0, 0,
        FallibleOperator<T>{
            channel_data,
            call_offset,
            [](void*, void* call_data, void*, T value) -> Poll<ResultOr<T>> {
              auto r =
                  (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                      std::move(value));
              if (r.ok()) {
                return ResultOr<T>{std::move(value), nullptr};
              }
              return ResultOr<T>{nullptr, ServerMetadataFromStatus(r.status())};
            },
            nullptr,
            nullptr,
        });
  }
};

template <typename FilterType, typename T,
          absl::Status (FilterType::Call::*impl)(T, FilterType*)>
struct AddOpImpl<FilterType, T,
                 absl::Status (FilterType::Call::*)(T, FilterType*), impl> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    to.Add(
        0, 0,
        FallibleOperator<T>{
            channel_data,
            call_offset,
            [](void*, void* call_data, void* channel_data,
               T value) -> Poll<ResultOr<T>> {
              auto r =
                  (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                      std::move(value), static_cast<FilterType>(channel_data));
              if (r.ok()) {
                return ResultOr<T>{std::move(value), nullptr};
              }
              return ResultOr<T>{nullptr, ServerMetadataFromStatus(r.status())};
            },
            nullptr,
            nullptr,
        });
  }
};

template <typename FilterType, typename T,
          ServerMetadataHandle (FilterType::Call::*impl)(T)>
struct AddOpImpl<FilterType, T, ServerMetadataHandle (FilterType::Call::*)(T),
                 impl> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    to.Add(
        0, 0,
        FallibleOperator<T>{
            channel_data,
            call_offset,
            [](void*, void* call_data, void*, T value) -> Poll<ResultOr<T>> {
              auto r =
                  (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                      std::move(value));
              if (r == nullptr) {
                return ResultOr<T>{std::move(value), nullptr};
              }
              return ResultOr<T>{nullptr, std::move(r)};
            },
            nullptr,
            nullptr,
        });
  }
};

template <typename FilterType, typename T,
          ServerMetadataHandle (FilterType::Call::*impl)(T, FilterType*)>
struct AddOpImpl<FilterType, T,
                 ServerMetadataHandle (FilterType::Call::*)(T, FilterType*),
                 impl> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    to.Add(
        0, 0,
        FallibleOperator<T>{
            channel_data,
            call_offset,
            [](void*, void* call_data, void* channel_data,
               T value) -> Poll<ResultOr<T>> {
              auto r =
                  (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                      std::move(value), static_cast<FilterType>(channel_data));
              if (r == nullptr) {
                return ResultOr<T>{std::move(value), nullptr};
              }
              return ResultOr<T>{nullptr, std::move(r)};
            },
            nullptr,
            nullptr,
        });
  }
};

template <typename FunctionImpl, FunctionImpl impl, typename FilterType,
          typename T>
void AddOp(FilterType* channel_data, size_t call_offset,
           Layout<FallibleOperator<T>>& to) {
  AddOpImpl<FilterType, FunctionImpl, T, impl>::Add(channel_data, call_offset,
                                                    to);
}

template <typename FilterType, typename Impl>
void AddClientInitialMetadataOp(FilterType* channel_data, size_t call_offset,
                                Impl p, StackData& to) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnClientInitialMetadata);
  AddOp<Impl, &FilterType::OnClientInitialMetadata>(channel_data, call_offset,
                                                    to.client_initial_metadata);
}

template <typename FilterType, typename Impl>
void AddServerInitialMetadataOp(FilterType* channel_data, size_t call_offset,
                                Impl p, StackData& to) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnServerInitialMetadata);
  AddOp<Impl, &FilterType::OnServerInitialMetadata>(channel_data, call_offset,
                                                    to.server_initial_metadata);
}

template <typename FilterType, typename Impl>
void AddClientToServerMessageOp(FilterType* channel_data, size_t call_offset,
                                Impl p, StackData& to) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnClientToServerMessage);
  AddOp<Impl, &FilterType::OnClientToServerMessage>(
      channel_data, call_offset, to.client_to_server_messages);
}

template <typename FilterType, typename Impl>
void AddServerToClientMessageOp(FilterType* channel_data, size_t call_offset,
                                Impl p, StackData& to) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnServerToClientMessage);
  AddOp<Impl, &FilterType::OnServerToClientMessage>(
      channel_data, call_offset, to.server_to_client_messages);
}

template <typename FilterType, typename Impl>
void AddServerTrailingMetadataOp(FilterType* channel_data, size_t call_offset,
                                 Impl p, StackData& to) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnServerTrailingMetadata);
  AddOp<Impl, &FilterType::OnServerTrailingMetadata>(
      channel_data, call_offset, to.server_trailing_metadata);
}

template <typename FilterType>
void AddFinalizer(FilterType*, size_t, const NoInterceptor* p, StackData&) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnFinalize);
}

template <typename FilterType>
void AddFinalizer(FilterType* channel_data, size_t call_offset,
                  void (FilterType::Call::*p)(const grpc_call_final_info*),
                  StackData& to) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnFinalize);
  to.finalizers.push_back(Finalizer{
      channel_data,
      call_offset,
      [](void* call_data, void*, const grpc_call_final_info* final_info) {
        static_cast<typename FilterType::Call*>(call_data)->OnFinalize(
            final_info);
      },
  });
}

template <typename FilterType>
void AddFinalizer(FilterType* channel_data, size_t call_offset,
                  void (FilterType::Call::*p)(const grpc_call_final_info*,
                                              FilterType*),
                  StackData& to) {
  GPR_DEBUG_ASSERT(p == &FilterType::OnFinalize);
  to.finalizers.push_back(Finalizer{
      channel_data,
      call_offset,
      [](void* call_data, void* channel_data,
         const grpc_call_final_info* final_info) {
        static_cast<typename FilterType::Call*>(call_data)->OnFinalize(
            final_info, static_cast<FilterType*>(channel_data));
      },
  });
}

}  // namespace filters_detail

// Execution environment for a stack of filters
class CallFilters {
 public:
  class StackBuilder;

  class Stack : public RefCounted<Stack> {
   private:
    friend class CallFilters;
    friend class StackBuilder;
    explicit Stack(filters_detail::StackData data) : data_(std::move(data)) {}
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
      filters_detail::AddClientInitialMetadataOp(
          filter, call_offset, &FilterType::OnClientInitialMetadata, data_);
      filters_detail::AddServerInitialMetadataOp(
          filter, call_offset, &FilterType::OnServerInitialMetadata, data_);
      filters_detail::AddClientToServerMessageOp(
          filter, call_offset, &FilterType::OnClientToServerMessage, data_);
      filters_detail::AddServerToClientMessageOp(
          filter, call_offset, &FilterType::OnServerToClientMessage, data_);
      filters_detail::AddServerTrailingMetadataOp(
          filter, call_offset, &FilterType::OnServerTrailingMetadata, data_);
      filters_detail::AddFinalizer(filter, call_offset, &FilterType::OnFinalize,
                                   data_);
    }

    RefCountedPtr<Stack> Build();

   private:
    size_t OffsetForNextFilter(size_t alignment, size_t size);

    filters_detail::StackData data_;
  };

  explicit CallFilters(RefCountedPtr<Stack> stack);
  ~CallFilters();

  GRPC_MUST_USE_RESULT auto PushClientInitialMetadata(ClientMetadataHandle md);
  GRPC_MUST_USE_RESULT auto PullClientInitialMetadata();
  GRPC_MUST_USE_RESULT auto PushServerInitialMetadata(ServerMetadataHandle md);
  GRPC_MUST_USE_RESULT auto PullServerInitialMetadata();
  GRPC_MUST_USE_RESULT auto PushClientToServerMessage(MessageHandle message);
  GRPC_MUST_USE_RESULT auto PullClientToServerMessage();
  GRPC_MUST_USE_RESULT auto PushServerToClientMessage(MessageHandle message);
  GRPC_MUST_USE_RESULT auto PullServerToClientMessage();
  void PushServerTrailingMetadata(ServerMetadataHandle md) {
    GPR_ASSERT(md != nullptr);
    if (server_trailing_metadata_ != nullptr) return;
    server_trailing_metadata_ = std::move(md);
    server_trailing_metadata_waiter_.Wake();
  }
  GRPC_MUST_USE_RESULT auto PullServerTrailingMetadata();
  void Finalize(const grpc_call_final_info* final_info);

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

  template <PipeState(CallFilters::*state_ptr), void*(CallFilters::*push_ptr),
            typename T,
            filters_detail::Layout<filters_detail::FallibleOperator<T>>(
                filters_detail::StackData::*layout_ptr)>
  class PipePromise {
   public:
    class Push {
     public:
      Push(CallFilters* filters, T x)
          : filters_(filters), value_(std::move(x)) {
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

      CallFilters* filters_;
      T value_;
    };

    class Pull {
     public:
      explicit Pull(CallFilters* filters) : filters_(filters) {}

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
        filters_->PushServerTrailingMetadata(std::move(r->error));
        return Failure{};
      }

      CallFilters* filters_;
      filters_detail::PipeTransformer<T> transformer_;
    };
  };

  class PullServerTrailingMetadata {};

  void CancelDueToFailedPipeOperation();

  const RefCountedPtr<Stack> stack_;

  PipeState client_initial_metadata_state_;
  PipeState server_initial_metadata_state_;
  PipeState client_to_server_message_state_;
  PipeState server_to_client_message_state_;
  IntraActivityWaiter server_trailing_metadata_waiter_;

  void* const call_data_;
  void* client_initial_metadata_ = nullptr;
  void* server_initial_metadata_ = nullptr;
  void* client_to_server_message_ = nullptr;
  void* server_to_client_message_ = nullptr;
  ServerMetadataHandle server_trailing_metadata_;

  using ClientInitialMetadataPromises =
      PipePromise<&CallFilters::client_initial_metadata_state_,
                  &CallFilters::client_initial_metadata_, ClientMetadataHandle,
                  &filters_detail::StackData::client_initial_metadata>;
  using ServerInitialMetadataPromises =
      PipePromise<&CallFilters::server_initial_metadata_state_,
                  &CallFilters::server_initial_metadata_, ServerMetadataHandle,
                  &filters_detail::StackData::server_initial_metadata>;
  using ClientToServerMessagePromises =
      PipePromise<&CallFilters::client_to_server_message_state_,
                  &CallFilters::client_to_server_message_, MessageHandle,
                  &filters_detail::StackData::client_to_server_messages>;
  using ServerToClientMessagePromises =
      PipePromise<&CallFilters::server_to_client_message_state_,
                  &CallFilters::server_to_client_message_, MessageHandle,
                  &filters_detail::StackData::server_to_client_messages>;
};

inline auto CallFilters::PushClientInitialMetadata(ClientMetadataHandle md) {
  GPR_ASSERT(md != nullptr);
  return [p = ClientInitialMetadataPromises::Push{
              this, std::move(md)}]() mutable { return p(); };
}

inline auto CallFilters::PullClientInitialMetadata() {
  return ClientInitialMetadataPromises::Pull{this};
}

inline auto CallFilters::PushServerInitialMetadata(ServerMetadataHandle md) {
  GPR_ASSERT(md != nullptr);
  return [p = ServerInitialMetadataPromises::Push{
              this, std::move(md)}]() mutable { return p(); };
}

inline auto CallFilters::PullServerInitialMetadata() {
  return ServerInitialMetadataPromises::Pull{this};
}

inline auto CallFilters::PushClientToServerMessage(MessageHandle message) {
  GPR_ASSERT(message != nullptr);
  return [p = ClientToServerMessagePromises::Push{
              this, std::move(message)}]() mutable { return p(); };
}

inline auto CallFilters::PullClientToServerMessage() {
  return ClientToServerMessagePromises::Pull{this};
}

inline auto CallFilters::PushServerToClientMessage(MessageHandle message) {
  GPR_ASSERT(message != nullptr);
  return [p = ServerToClientMessagePromises::Push{
              this, std::move(message)}]() mutable { return p(); };
}

inline auto CallFilters::PullServerToClientMessage() {
  return ServerToClientMessagePromises::Pull{this};
}

inline auto CallFilters::PullServerTrailingMetadata() {
  return [this,
          pipe = filters_detail::InfalliblePipeTransformer<
              ServerMetadataHandle>()]() mutable -> Poll<ServerMetadataHandle> {
    if (pipe.IsRunning()) {
      return pipe.Step(call_data_);
    }
    if (server_trailing_metadata_ == nullptr) return Pending{};
    return pipe.Start(&stack_->data_.server_trailing_metadata,
                      std::move(server_trailing_metadata_), call_data_);
  };
}

}  // namespace grpc_core

#endif  // GRPC_FILTER_RUNNER_H
