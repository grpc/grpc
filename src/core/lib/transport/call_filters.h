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

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_CALL_FILTERS_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_CALL_FILTERS_H

#include <cstdint>
#include <memory>
#include <type_traits>

#include "absl/log/check.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/dump_args.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/transport/call_final_info.h"
#include "src/core/lib/transport/message.h"
#include "src/core/lib/transport/metadata.h"

// CallFilters tracks a list of filters that are attached to a call.
// At a high level, a filter (for the purposes of this module) is a class
// that has a Call member class, and a set of methods that are called
// for each major event in the lifetime of a call.
//
// The Call member class must have the following members:
// - OnClientInitialMetadata  - $VALUE_TYPE = ClientMetadata
// - OnServerInitialMetadata  - $VALUE_TYPE = ServerMetadata
// - OnServerToClientMessage  - $VALUE_TYPE = Message
// - OnClientToServerMessage  - $VALUE_TYPE = Message
// - OnClientToServerHalfClose - no value
// - OnServerTrailingMetadata - $VALUE_TYPE = ServerMetadata
// - OnFinalize               - special, see below
// These members define an interception point for a particular event in
// the call lifecycle.
//
// The type of these members matters, and is selectable by the class
// author. For $INTERCEPTOR_NAME in the above list:
// - static const NoInterceptor $INTERCEPTOR_NAME:
//   defines that this filter does not intercept this event.
//   there is zero runtime cost added to handling that event by this filter.
// - void $INTERCEPTOR_NAME($VALUE_TYPE&):
//   the filter intercepts this event, and can modify the value.
//   it never fails.
// - absl::Status $INTERCEPTOR_NAME($VALUE_TYPE&):
//   the filter intercepts this event, and can modify the value.
//   it can fail, in which case the call will be aborted.
// - ServerMetadataHandle $INTERCEPTOR_NAME($VALUE_TYPE&)
//   the filter intercepts this event, and can modify the value.
//   the filter can return nullptr for success, or a metadata handle for
//   failure (in which case the call will be aborted).
//   useful for cases where the exact metadata returned needs to be customized.
// - void $INTERCEPTOR_NAME($VALUE_TYPE&, FilterType*):
//   the filter intercepts this event, and can modify the value.
//   it can access the channel via the second argument.
//   it never fails.
// - absl::Status $INTERCEPTOR_NAME($VALUE_TYPE&, FilterType*):
//   the filter intercepts this event, and can modify the value.
//   it can access the channel via the second argument.
//   it can fail, in which case the call will be aborted.
// - ServerMetadataHandle $INTERCEPTOR_NAME($VALUE_TYPE&, FilterType*)
//   the filter intercepts this event, and can modify the value.
//   it can access the channel via the second argument.
//   the filter can return nullptr for success, or a metadata handle for
//   failure (in which case the call will be aborted).
//   useful for cases where the exact metadata returned needs to be customized.
// It's also acceptable to return a promise that resolves to the
// relevant return type listed above.
//
// Finally, OnFinalize is added to intecept call finalization.
// It must have one of the signatures:
// - static const NoInterceptor OnFinalize:
//   the filter does not intercept call finalization.
// - void OnFinalize(const grpc_call_final_info*):
//   the filter intercepts call finalization.
// - void OnFinalize(const grpc_call_final_info*, FilterType*):
//   the filter intercepts call finalization.
//
// The constructor of the Call object can either take a pointer to the channel
// object, or not take any arguments.
//
// *THIS MODULE* holds no opinion on what members the channel part of the
// filter should or should not have, but does require that it have a stable
// pointer for the lifetime of a call (ownership is expected to happen
// elsewhere).

namespace grpc_core {

// Tag type to indicate no interception.
// This is used to indicate that a filter does not intercept a particular
// event.
// In C++14 we declare these as (for example):
//   static const NoInterceptor OnClientInitialMetadata;
// and out-of-line provide the definition:
//   const MyFilter::Call::NoInterceptor
//   MyFilter::Call::OnClientInitialMetadata;
// In C++17 and later we can use inline variables instead:
//   inline static const NoInterceptor OnClientInitialMetadata;
struct NoInterceptor {};

namespace filters_detail {

// One call filter constructor
// Contains enough information to allocate and initialize the
// call data for one filter.
struct FilterConstructor {
  // Pointer to corresponding channel data for this filter
  void* channel_data;
  // Offset of the call data for this filter within the call data memory
  // allocation
  size_t call_offset;
  // Initialize the call data for this filter
  void (*call_init)(void* call_data, void* channel_data);
};

// One call filter destructor
struct FilterDestructor {
  // Offset of the call data for this filter within the call data memory
  // allocation
  size_t call_offset;
  // Destroy the call data for this filter
  void (*call_destroy)(void* call_data);
};

template <typename FilterType, typename = void>
struct CallConstructor {
  static void Construct(void* call_data, FilterType*) {
    new (call_data) typename FilterType::Call();
  }
};

template <typename FilterType>
struct CallConstructor<FilterType,
                       absl::void_t<decltype(typename FilterType::Call(
                           static_cast<FilterType*>(nullptr)))>> {
  static void Construct(void* call_data, FilterType* channel) {
    new (call_data) typename FilterType::Call(channel);
  }
};

// Result of a filter operation
// Can be either ok (if ok is non-null) or an error.
// Only one pointer can be set.
template <typename T>
struct ResultOr {
  ResultOr(T ok, ServerMetadataHandle error)
      : ok(std::move(ok)), error(std::move(error)) {
    CHECK((this->ok == nullptr) ^ (this->error == nullptr));
  }
  T ok;
  ServerMetadataHandle error;
};

// One filter operation metadata
// Given a value of type V, produces a promise of type R.
template <typename R, typename V>
struct Operator {
  using Result = R;
  using Arg = V;
  // Pointer to corresponding channel data for this filter
  void* channel_data;
  // Offset of the call data for this filter within the call data memory
  size_t call_offset;
  // Initialize the promise data for this filter, and poll once.
  // Return the result of the poll.
  // If the promise finishes, also destroy the promise data!
  Poll<R> (*promise_init)(void* promise_data, void* call_data,
                          void* channel_data, V value);
  // Poll the promise data for this filter.
  // If the promise finishes, also destroy the promise data!
  // Note that if the promise always finishes on the first poll, then supplying
  // this method is unnecessary (as it will never be called).
  Poll<R> (*poll)(void* promise_data);
  // Destroy the promise data for this filter for an in-progress operation
  // before the promise finishes.
  // Note that if the promise always finishes on the first poll, then supplying
  // this method is unnecessary (as it will never be called).
  void (*early_destroy)(void* promise_data);
};

struct HalfCloseOperator {
  // Pointer to corresponding channel data for this filter
  void* channel_data;
  // Offset of the call data for this filter within the call data memory
  size_t call_offset;
  void (*half_close)(void* call_data, void* channel_data);
};

void RunHalfClose(absl::Span<const HalfCloseOperator> ops, void* call_data);

// We divide operations into fallible and infallible.
// Fallible operations can fail, and that failure terminates the call.
// Infallible operations cannot fail.
// Fallible operations are used for client initial, and server initial metadata,
// and messages.
// Infallible operations are used for server trailing metadata.
// (This is because server trailing metadata occurs when the call is finished -
// and so we couldn't possibly become more finished - and also because it's the
// preferred representation of failure anyway!)

// An operation that could fail: takes a T argument, produces a ResultOr<T>
template <typename T>
using FallibleOperator = Operator<ResultOr<T>, T>;
// And one that cannot: takes a T argument, produces a T
template <typename T>
using InfallibleOperator = Operator<T, T>;

// One call finalizer
struct Finalizer {
  void* channel_data;
  size_t call_offset;
  void (*final)(void* call_data, void* channel_data,
                const grpc_call_final_info* final_info);
};

// A layout of operations for a given filter stack
// This includes which operations, how much memory is required, what alignment.
template <typename Op>
struct Layout {
  size_t promise_size = 0;
  size_t promise_alignment = 0;
  std::vector<Op> ops;

  void Add(size_t filter_promise_size, size_t filter_promise_alignment, Op op) {
    promise_size = std::max(promise_size, filter_promise_size);
    promise_alignment = std::max(promise_alignment, filter_promise_alignment);
    ops.push_back(op);
  }

  void Reverse() { std::reverse(ops.begin(), ops.end()); }
};

// AddOp and friends
// These are helpers to wrap a member function on a class into an operation
// and attach it to a layout.
// There's a generic wrapper function `AddOp` for each of fallible and
// infallible operations.
// There are then specializations of AddOpImpl for each kind of member function
// an operation could have.
// Each specialization has an `Add` member function for the kinds of operations
// it supports: some only support fallible, some only support infallible, some
// support both.

template <typename FilterType, typename T, typename FunctionImpl,
          FunctionImpl impl, typename SfinaeVoid = void>
struct AddOpImpl;

template <typename FunctionImpl, FunctionImpl impl, typename FilterType,
          typename T>
void AddOp(FilterType* channel_data, size_t call_offset,
           Layout<FallibleOperator<T>>& to) {
  AddOpImpl<FilterType, T, FunctionImpl, impl>::Add(channel_data, call_offset,
                                                    to);
}

template <typename FunctionImpl, FunctionImpl impl, typename FilterType,
          typename T>
void AddOp(FilterType* channel_data, size_t call_offset,
           Layout<InfallibleOperator<T>>& to) {
  AddOpImpl<FilterType, T, FunctionImpl, impl>::Add(channel_data, call_offset,
                                                    to);
}

template <typename FilterType>
void AddHalfClose(FilterType* channel_data, size_t call_offset,
                  void (FilterType::Call::*)(),
                  std::vector<HalfCloseOperator>& to) {
  to.push_back(
      HalfCloseOperator{channel_data, call_offset, [](void* call_data, void*) {
                          static_cast<typename FilterType::Call*>(call_data)
                              ->OnClientToServerHalfClose();
                        }});
}

template <typename FilterType>
void AddHalfClose(FilterType* channel_data, size_t call_offset,
                  void (FilterType::Call::*)(FilterType*),
                  std::vector<HalfCloseOperator>& to) {
  to.push_back(HalfCloseOperator{
      channel_data, call_offset, [](void* call_data, void* channel_data) {
        static_cast<typename FilterType::Call*>(call_data)
            ->OnClientToServerHalfClose(static_cast<FilterType*>(channel_data));
      }});
}

template <typename FilterType>
void AddHalfClose(FilterType*, size_t, const NoInterceptor*,
                  std::vector<HalfCloseOperator>&) {}

// const NoInterceptor $EVENT
// These do nothing, and specifically DO NOT add an operation to the layout.
// Supported for fallible & infallible operations.
template <typename FilterType, typename T, const NoInterceptor* which>
struct AddOpImpl<FilterType, T, const NoInterceptor*, which> {
  static void Add(FilterType*, size_t, Layout<FallibleOperator<T>>&) {}
  static void Add(FilterType*, size_t, Layout<InfallibleOperator<T>>&) {}
};

// void $INTERCEPTOR_NAME($VALUE_TYPE&)
template <typename FilterType, typename T,
          void (FilterType::Call::* impl)(typename T::element_type&)>
struct AddOpImpl<FilterType, T,
                 void (FilterType::Call::*)(typename T::element_type&), impl> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    to.Add(0, 0,
           FallibleOperator<T>{
               channel_data,
               call_offset,
               [](void*, void* call_data, void*, T value) -> Poll<ResultOr<T>> {
                 (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                     *value);
                 return ResultOr<T>{std::move(value), nullptr};
               },
               nullptr,
               nullptr,
           });
  }
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<InfallibleOperator<T>>& to) {
    to.Add(0, 0,
           InfallibleOperator<T>{
               channel_data,
               call_offset,
               [](void*, void* call_data, void*, T value) -> Poll<T> {
                 (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                     *value);
                 return std::move(value);
               },
               nullptr,
               nullptr,
           });
  }
};

// void $INTERCEPTOR_NAME($VALUE_TYPE&, FilterType*)
template <typename FilterType, typename T,
          void (FilterType::Call::* impl)(typename T::element_type&,
                                          FilterType*)>
struct AddOpImpl<
    FilterType, T,
    void (FilterType::Call::*)(typename T::element_type&, FilterType*), impl> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    to.Add(0, 0,
           FallibleOperator<T>{
               channel_data,
               call_offset,
               [](void*, void* call_data, void* channel_data,
                  T value) -> Poll<ResultOr<T>> {
                 (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                     *value, static_cast<FilterType*>(channel_data));
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
        InfallibleOperator<T>{
            channel_data,
            call_offset,
            [](void*, void* call_data, void* channel_data, T value) -> Poll<T> {
              (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                  *value, static_cast<FilterType*>(channel_data));
              return std::move(value);
            },
            nullptr,
            nullptr,
        });
  }
};

// $VALUE_HANDLE $INTERCEPTOR_NAME($VALUE_HANDLE, FilterType*)
template <typename FilterType, typename T,
          T (FilterType::Call::* impl)(T, FilterType*)>
struct AddOpImpl<FilterType, T, T (FilterType::Call::*)(T, FilterType*), impl> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    to.Add(
        0, 0,
        FallibleOperator<T>{
            channel_data,
            call_offset,
            [](void*, void* call_data, void* channel_data,
               T value) -> Poll<ResultOr<T>> {
              return ResultOr<T>{
                  (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                      std::move(value), static_cast<FilterType*>(channel_data)),
                  nullptr};
            },
            nullptr,
            nullptr,
        });
  }
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<InfallibleOperator<T>>& to) {
    to.Add(
        0, 0,
        InfallibleOperator<T>{
            channel_data,
            call_offset,
            [](void*, void* call_data, void* channel_data, T value) -> Poll<T> {
              (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                  *value, static_cast<FilterType*>(channel_data));
              return (
                  static_cast<typename FilterType::Call*>(call_data)->*impl)(
                  std::move(value), static_cast<FilterType*>(channel_data));
            },
            nullptr,
            nullptr,
        });
  }
};

// absl::Status $INTERCEPTOR_NAME($VALUE_TYPE&)
template <typename FilterType, typename T,
          absl::Status (FilterType::Call::* impl)(typename T::element_type&)>
struct AddOpImpl<FilterType, T,
                 absl::Status (FilterType::Call::*)(typename T::element_type&),
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
                      *value);
              if (r.ok()) return ResultOr<T>{std::move(value), nullptr};
              return ResultOr<T>{
                  nullptr, StatusCast<ServerMetadataHandle>(std::move(r))};
            },
            nullptr,
            nullptr,
        });
  }
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<InfallibleOperator<T>>& to) {
    to.Add(
        0, 0,
        InfallibleOperator<T>{
            channel_data,
            call_offset,
            [](void*, void* call_data, void*, T value) -> Poll<T> {
              auto r =
                  (static_cast<typename FilterType::Call*>(call_data)->*impl)(
                      *value);
              if (r.ok()) return std::move(value);
              return StatusCast<ServerMetadataHandle>(std::move(r));
            },
            nullptr,
            nullptr,
        });
  }
};

// absl::Status $INTERCEPTOR_NAME(const $VALUE_TYPE&)
template <typename FilterType, typename T,
          absl::Status (FilterType::Call::* impl)(
              const typename T::element_type&)>
struct AddOpImpl<
    FilterType, T,
    absl::Status (FilterType::Call::*)(const typename T::element_type&), impl> {
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
                      *value);
              if (r.ok()) return ResultOr<T>{std::move(value), nullptr};
              return ResultOr<T>{
                  nullptr, StatusCast<ServerMetadataHandle>(std::move(r))};
            },
            nullptr,
            nullptr,
        });
  }
};

// absl::Status $INTERCEPTOR_NAME($VALUE_TYPE&, FilterType*)
template <typename FilterType, typename T,
          absl::Status (FilterType::Call::* impl)(typename T::element_type&,
                                                  FilterType*)>
struct AddOpImpl<FilterType, T,
                 absl::Status (FilterType::Call::*)(typename T::element_type&,
                                                    FilterType*),
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
                      *value, static_cast<FilterType*>(channel_data));
              if (IsStatusOk(r)) return ResultOr<T>{std::move(value), nullptr};
              return ResultOr<T>{
                  nullptr, StatusCast<ServerMetadataHandle>(std::move(r))};
            },
            nullptr,
            nullptr,
        });
  }
};

// absl::Status $INTERCEPTOR_NAME(const $VALUE_TYPE&, FilterType*)
template <typename FilterType, typename T,
          absl::Status (FilterType::Call::* impl)(
              const typename T::element_type&, FilterType*)>
struct AddOpImpl<FilterType, T,
                 absl::Status (FilterType::Call::*)(
                     const typename T::element_type&, FilterType*),
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
                      *value, static_cast<FilterType*>(channel_data));
              if (IsStatusOk(r)) return ResultOr<T>{std::move(value), nullptr};
              return ResultOr<T>{
                  nullptr, StatusCast<ServerMetadataHandle>(std::move(r))};
            },
            nullptr,
            nullptr,
        });
  }
};

// absl::StatusOr<$VALUE_HANDLE> $INTERCEPTOR_NAME($VALUE_HANDLE, FilterType*)
template <typename FilterType, typename T,
          absl::StatusOr<T> (FilterType::Call::* impl)(T, FilterType*)>
struct AddOpImpl<FilterType, T,
                 absl::StatusOr<T> (FilterType::Call::*)(T, FilterType*),
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
                      std::move(value), static_cast<FilterType*>(channel_data));
              if (IsStatusOk(r)) return ResultOr<T>{std::move(*r), nullptr};
              return ResultOr<T>{
                  nullptr, StatusCast<ServerMetadataHandle>(std::move(r))};
            },
            nullptr,
            nullptr,
        });
  }
};

// ServerMetadataHandle $INTERCEPTOR_NAME($VALUE_TYPE&)
template <typename FilterType, typename T,
          ServerMetadataHandle (FilterType::Call::* impl)(
              typename T::element_type&)>
struct AddOpImpl<FilterType, T,
                 ServerMetadataHandle (FilterType::Call::*)(
                     typename T::element_type&),
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
                      *value);
              if (r == nullptr) return ResultOr<T>{std::move(value), nullptr};
              return ResultOr<T>{
                  nullptr, StatusCast<ServerMetadataHandle>(std::move(r))};
            },
            nullptr,
            nullptr,
        });
  }
};

// ServerMetadataHandle $INTERCEPTOR_NAME(const $VALUE_TYPE&)
template <typename FilterType, typename T,
          ServerMetadataHandle (FilterType::Call::* impl)(
              const typename T::element_type&)>
struct AddOpImpl<FilterType, T,
                 ServerMetadataHandle (FilterType::Call::*)(
                     const typename T::element_type&),
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
                      *value);
              if (r == nullptr) return ResultOr<T>{std::move(value), nullptr};
              return ResultOr<T>{
                  nullptr, StatusCast<ServerMetadataHandle>(std::move(r))};
            },
            nullptr,
            nullptr,
        });
  }
};

// ServerMetadataHandle $INTERCEPTOR_NAME($VALUE_TYPE&, FilterType*)
template <typename FilterType, typename T,
          ServerMetadataHandle (FilterType::Call::* impl)(
              typename T::element_type&, FilterType*)>
struct AddOpImpl<FilterType, T,
                 ServerMetadataHandle (FilterType::Call::*)(
                     typename T::element_type&, FilterType*),
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
                      *value, static_cast<FilterType*>(channel_data));
              if (r == nullptr) return ResultOr<T>{std::move(value), nullptr};
              return ResultOr<T>{
                  nullptr, StatusCast<ServerMetadataHandle>(std::move(r))};
            },
            nullptr,
            nullptr,
        });
  }
};

// ServerMetadataHandle $INTERCEPTOR_NAME(const $VALUE_TYPE&, FilterType*)
template <typename FilterType, typename T,
          ServerMetadataHandle (FilterType::Call::* impl)(
              const typename T::element_type&, FilterType*)>
struct AddOpImpl<FilterType, T,
                 ServerMetadataHandle (FilterType::Call::*)(
                     const typename T::element_type&, FilterType*),
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
                      *value, static_cast<FilterType*>(channel_data));
              if (r == nullptr) return ResultOr<T>{std::move(value), nullptr};
              return ResultOr<T>{
                  nullptr, StatusCast<ServerMetadataHandle>(std::move(r))};
            },
            nullptr,
            nullptr,
        });
  }
};

// PROMISE_RETURNING(absl::Status) $INTERCEPTOR_NAME($VALUE_TYPE&)
template <typename FilterType, typename T, typename R,
          R (FilterType::Call::* impl)(typename T::element_type&)>
struct AddOpImpl<
    FilterType, T, R (FilterType::Call::*)(typename T::element_type&), impl,
    absl::enable_if_t<std::is_same<absl::Status, PromiseResult<R>>::value>> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    class Promise {
     public:
      Promise(T value, typename FilterType::Call* call_data, FilterType*)
          : value_(std::move(value)), impl_((call_data->*impl)(*value_)) {}

      Poll<ResultOr<T>> PollOnce() {
        auto p = impl_();
        auto* r = p.value_if_ready();
        if (r == nullptr) return Pending{};
        T value = std::move(value_);
        this->~Promise();
        if (r->ok()) {
          return ResultOr<T>{std::move(value), nullptr};
        }
        return ResultOr<T>{nullptr, ServerMetadataFromStatus(*r)};
      }

     private:
      GPR_NO_UNIQUE_ADDRESS T value_;
      GPR_NO_UNIQUE_ADDRESS R impl_;
    };
    to.Add(sizeof(Promise), alignof(Promise),
           FallibleOperator<T>{
               channel_data,
               call_offset,
               [](void* promise_data, void* call_data, void* channel_data,
                  T value) -> Poll<ResultOr<T>> {
                 auto* promise = new (promise_data)
                     Promise(std::move(value),
                             static_cast<typename FilterType::Call*>(call_data),
                             static_cast<FilterType*>(channel_data));
                 return promise->PollOnce();
               },
               [](void* promise_data) {
                 return static_cast<Promise*>(promise_data)->PollOnce();
               },
               [](void* promise_data) {
                 static_cast<Promise*>(promise_data)->~Promise();
               },
           });
  }
};

// PROMISE_RETURNING(absl::Status) $INTERCEPTOR_NAME($VALUE_TYPE&, FilterType*)
template <typename FilterType, typename T, typename R,
          R (FilterType::Call::* impl)(typename T::element_type&, FilterType*)>
struct AddOpImpl<
    FilterType, T,
    R (FilterType::Call::*)(typename T::element_type&, FilterType*), impl,
    absl::enable_if_t<!std::is_same<R, absl::Status>::value &&
                      std::is_same<absl::Status, PromiseResult<R>>::value>> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    class Promise {
     public:
      Promise(T value, typename FilterType::Call* call_data,
              FilterType* channel_data)
          : value_(std::move(value)),
            impl_((call_data->*impl)(*value_, channel_data)) {}

      Poll<ResultOr<T>> PollOnce() {
        auto p = impl_();
        auto* r = p.value_if_ready();
        if (r == nullptr) return Pending{};
        T value = std::move(value_);
        this->~Promise();
        if (r->ok()) {
          return ResultOr<T>{std::move(value), nullptr};
        }
        return ResultOr<T>{nullptr, ServerMetadataFromStatus(*r)};
      }

     private:
      GPR_NO_UNIQUE_ADDRESS T value_;
      GPR_NO_UNIQUE_ADDRESS R impl_;
    };
    to.Add(sizeof(Promise), alignof(Promise),
           FallibleOperator<T>{
               channel_data,
               call_offset,
               [](void* promise_data, void* call_data, void* channel_data,
                  T value) -> Poll<ResultOr<T>> {
                 auto* promise = new (promise_data)
                     Promise(std::move(value),
                             static_cast<typename FilterType::Call*>(call_data),
                             static_cast<FilterType*>(channel_data));
                 return promise->PollOnce();
               },
               [](void* promise_data) {
                 return static_cast<Promise*>(promise_data)->PollOnce();
               },
               [](void* promise_data) {
                 static_cast<Promise*>(promise_data)->~Promise();
               },
           });
  }
};

// PROMISE_RETURNING(absl::StatusOr<$VALUE_HANDLE>)
// $INTERCEPTOR_NAME($VALUE_HANDLE, FilterType*)
template <typename FilterType, typename T, typename R,
          R (FilterType::Call::* impl)(T, FilterType*)>
struct AddOpImpl<FilterType, T, R (FilterType::Call::*)(T, FilterType*), impl,
                 absl::enable_if_t<std::is_same<absl::StatusOr<T>,
                                                PromiseResult<R>>::value>> {
  static void Add(FilterType* channel_data, size_t call_offset,
                  Layout<FallibleOperator<T>>& to) {
    class Promise {
     public:
      Promise(T value, typename FilterType::Call* call_data,
              FilterType* channel_data)
          : impl_((call_data->*impl)(std::move(value), channel_data)) {}

      Poll<ResultOr<T>> PollOnce() {
        auto p = impl_();
        auto* r = p.value_if_ready();
        if (r == nullptr) return Pending{};
        this->~Promise();
        if (r->ok()) return ResultOr<T>{std::move(**r), nullptr};
        return ResultOr<T>{nullptr, ServerMetadataFromStatus(r->status())};
      }

     private:
      GPR_NO_UNIQUE_ADDRESS R impl_;
    };
    to.Add(sizeof(Promise), alignof(Promise),
           FallibleOperator<T>{
               channel_data,
               call_offset,
               [](void* promise_data, void* call_data, void* channel_data,
                  T value) -> Poll<ResultOr<T>> {
                 auto* promise = new (promise_data)
                     Promise(std::move(value),
                             static_cast<typename FilterType::Call*>(call_data),
                             static_cast<FilterType*>(channel_data));
                 return promise->PollOnce();
               },
               [](void* promise_data) {
                 return static_cast<Promise*>(promise_data)->PollOnce();
               },
               [](void* promise_data) {
                 static_cast<Promise*>(promise_data)->~Promise();
               },
           });
  }
};

struct ChannelDataDestructor {
  void (*destroy)(void* channel_data);
  void* channel_data;
};

// StackData contains the main datastructures built up by this module.
// It's a complete representation of all the code that needs to be invoked
// to execute a call for a given set of filters.
// This structure is held at the channel layer and is shared between many
// in-flight calls.
struct StackData {
  // Overall size and alignment of the call data for this stack.
  size_t call_data_alignment = 1;
  size_t call_data_size = 0;
  // A complete list of filters for this call, so that we can construct the
  // call data for each filter.
  std::vector<FilterConstructor> filter_constructor;
  std::vector<FilterDestructor> filter_destructor;
  // For each kind of operation, a layout of the operations for this call.
  // (there's some duplicate data here, and that's ok: we want to avoid
  // pointer chasing as much as possible when executing a call)
  Layout<FallibleOperator<ClientMetadataHandle>> client_initial_metadata;
  Layout<FallibleOperator<ServerMetadataHandle>> server_initial_metadata;
  Layout<FallibleOperator<MessageHandle>> client_to_server_messages;
  std::vector<HalfCloseOperator> client_to_server_half_close;
  Layout<FallibleOperator<MessageHandle>> server_to_client_messages;
  Layout<InfallibleOperator<ServerMetadataHandle>> server_trailing_metadata;
  // A list of finalizers for this call.
  // We use a bespoke data structure here because finalizers can never be
  // asynchronous.
  std::vector<Finalizer> finalizers;
  // A list of functions to call when this stack data is destroyed
  // (to capture ownership of channel data)
  std::vector<ChannelDataDestructor> channel_data_destructors;

  // Add one filter to the list of filters, and update alignment.
  // Returns the offset of the call data for this filter.
  // Specifically does not update any of the layouts or finalizers.
  // Callers are expected to do that themselves.
  // This separation enables separation of *testing* of filters, and since
  // this is a detail type it's felt that a slightly harder to hold API that
  // we have exactly one caller for is warranted for a more thorough testing
  // story.
  template <typename FilterType>
  absl::enable_if_t<!std::is_empty<typename FilterType::Call>::value, size_t>
  AddFilterConstructor(FilterType* channel_data) {
    const size_t alignment = alignof(typename FilterType::Call);
    call_data_alignment = std::max(call_data_alignment, alignment);
    if (call_data_size % alignment != 0) {
      call_data_size += alignment - call_data_size % alignment;
    }
    const size_t call_offset = call_data_size;
    call_data_size += sizeof(typename FilterType::Call);
    filter_constructor.push_back(FilterConstructor{
        channel_data,
        call_offset,
        [](void* call_data, void* channel_data) {
          CallConstructor<FilterType>::Construct(
              call_data, static_cast<FilterType*>(channel_data));
        },
    });
    return call_offset;
  }

  template <typename FilterType>
  absl::enable_if_t<
      std::is_empty<typename FilterType::Call>::value &&
          !std::is_trivially_constructible<typename FilterType::Call>::value,
      size_t>
  AddFilterConstructor(FilterType* channel_data) {
    const size_t alignment = alignof(typename FilterType::Call);
    call_data_alignment = std::max(call_data_alignment, alignment);
    filter_constructor.push_back(FilterConstructor{
        channel_data,
        0,
        [](void* call_data, void* channel_data) {
          CallConstructor<FilterType>::Construct(
              call_data, static_cast<FilterType*>(channel_data));
        },
    });
    return 0;
  }

  template <typename FilterType>
  absl::enable_if_t<
      std::is_empty<typename FilterType::Call>::value &&
          std::is_trivially_constructible<typename FilterType::Call>::value,
      size_t>
  AddFilterConstructor(FilterType*) {
    const size_t alignment = alignof(typename FilterType::Call);
    call_data_alignment = std::max(call_data_alignment, alignment);
    return 0;
  }

  template <typename FilterType>
  absl::enable_if_t<
      !std::is_trivially_destructible<typename FilterType::Call>::value>
  AddFilterDestructor(size_t call_offset) {
    filter_destructor.push_back(FilterDestructor{
        call_offset,
        [](void* call_data) {
          Destruct(static_cast<typename FilterType::Call*>(call_data));
        },
    });
  }

  template <typename FilterType>
  absl::enable_if_t<
      std::is_trivially_destructible<typename FilterType::Call>::value>
  AddFilterDestructor(size_t) {}

  template <typename FilterType>
  size_t AddFilter(FilterType* filter) {
    const size_t call_offset = AddFilterConstructor(filter);
    AddFilterDestructor<FilterType>(call_offset);
    return call_offset;
  }

  // Per operation adders - one for each interception point.
  // Delegate to AddOp() above.

  template <typename FilterType>
  void AddClientInitialMetadataOp(FilterType* channel_data,
                                  size_t call_offset) {
    AddOp<decltype(&FilterType::Call::OnClientInitialMetadata),
          &FilterType::Call::OnClientInitialMetadata>(channel_data, call_offset,
                                                      client_initial_metadata);
  }

  template <typename FilterType>
  void AddServerInitialMetadataOp(FilterType* channel_data,
                                  size_t call_offset) {
    AddOp<decltype(&FilterType::Call::OnServerInitialMetadata),
          &FilterType::Call::OnServerInitialMetadata>(channel_data, call_offset,
                                                      server_initial_metadata);
  }

  template <typename FilterType>
  void AddClientToServerMessageOp(FilterType* channel_data,
                                  size_t call_offset) {
    AddOp<decltype(&FilterType::Call::OnClientToServerMessage),
          &FilterType::Call::OnClientToServerMessage>(
        channel_data, call_offset, client_to_server_messages);
  }

  template <typename FilterType>
  void AddClientToServerHalfClose(FilterType* channel_data,
                                  size_t call_offset) {
    AddHalfClose(channel_data, call_offset,
                 &FilterType::Call::OnClientToServerHalfClose,
                 client_to_server_half_close);
  }

  template <typename FilterType>
  void AddServerToClientMessageOp(FilterType* channel_data,
                                  size_t call_offset) {
    AddOp<decltype(&FilterType::Call::OnServerToClientMessage),
          &FilterType::Call::OnServerToClientMessage>(
        channel_data, call_offset, server_to_client_messages);
  }

  template <typename FilterType>
  void AddServerTrailingMetadataOp(FilterType* channel_data,
                                   size_t call_offset) {
    AddOp<decltype(&FilterType::Call::OnServerTrailingMetadata),
          &FilterType::Call::OnServerTrailingMetadata>(
        channel_data, call_offset, server_trailing_metadata);
  }

  // Finalizer interception adders

  template <typename FilterType>
  void AddFinalizer(FilterType*, size_t, const NoInterceptor* p) {
    DCHECK(p == &FilterType::Call::OnFinalize);
  }

  template <typename FilterType>
  void AddFinalizer(FilterType* channel_data, size_t call_offset,
                    void (FilterType::Call::* p)(const grpc_call_final_info*)) {
    DCHECK(p == &FilterType::Call::OnFinalize);
    finalizers.push_back(Finalizer{
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
                    void (FilterType::Call::* p)(const grpc_call_final_info*,
                                                 FilterType*)) {
    DCHECK(p == &FilterType::Call::OnFinalize);
    finalizers.push_back(Finalizer{
        channel_data,
        call_offset,
        [](void* call_data, void* channel_data,
           const grpc_call_final_info* final_info) {
          static_cast<typename FilterType::Call*>(call_data)->OnFinalize(
              final_info, static_cast<FilterType*>(channel_data));
        },
    });
  }
};

// OperationExecutor is a helper class to execute a sequence of operations
// from a layout on one value.
// We instantiate one of these during the *Pull* promise for each operation
// and wait for it to resolve.
// At this layer the filters look like a list of transformations on the
// value pushed.
// An early-failing filter will cause subsequent filters to not execute.
template <typename T>
class OperationExecutor {
 public:
  OperationExecutor() = default;
  ~OperationExecutor();
  OperationExecutor(const OperationExecutor&) = delete;
  OperationExecutor& operator=(const OperationExecutor&) = delete;
  OperationExecutor(OperationExecutor&& other) noexcept
      : ops_(other.ops_), end_ops_(other.end_ops_) {
    // Movable iff we're not running.
    DCHECK_EQ(other.promise_data_, nullptr);
  }
  OperationExecutor& operator=(OperationExecutor&& other) noexcept {
    DCHECK_EQ(other.promise_data_, nullptr);
    DCHECK_EQ(promise_data_, nullptr);
    ops_ = other.ops_;
    end_ops_ = other.end_ops_;
    return *this;
  }
  // IsRunning() is true if we're currently executing a sequence of operations.
  bool IsRunning() const { return promise_data_ != nullptr; }
  // Start executing a layout. May allocate space to store the relevant promise.
  // Returns the result of the first poll.
  // If the promise finishes, also destroy the promise data.
  Poll<ResultOr<T>> Start(const Layout<FallibleOperator<T>>* layout, T input,
                          void* call_data);
  // Continue executing a layout. Returns the result of the next poll.
  // If the promise finishes, also destroy the promise data.
  Poll<ResultOr<T>> Step(void* call_data);

 private:
  // Start polling on the current step of the layout.
  // `input` is the current value (either the input to the first step, or the
  // so far transformed value)
  // `call_data` is the call data for the filter stack.
  // If this op finishes immediately then we iterative move to the next step.
  // If we reach the end up the ops, we return the overall poll result,
  // otherwise we return Pending.
  Poll<ResultOr<T>> InitStep(T input, void* call_data);
  // Continue polling on the current step of the layout.
  // Called on the next poll after InitStep returns pending.
  // If the promise is still pending, returns this.
  // If the promise completes we call into InitStep to continue execution
  // through the filters.
  Poll<ResultOr<T>> ContinueStep(void* call_data);

  void* promise_data_ = nullptr;
  const FallibleOperator<T>* ops_;
  const FallibleOperator<T>* end_ops_;
};

// Per OperationExecutor, but for infallible operation sequences.
template <typename T>
class InfallibleOperationExecutor {
 public:
  InfallibleOperationExecutor() = default;
  ~InfallibleOperationExecutor();
  InfallibleOperationExecutor(const InfallibleOperationExecutor&) = delete;
  InfallibleOperationExecutor& operator=(const InfallibleOperationExecutor&) =
      delete;
  InfallibleOperationExecutor(InfallibleOperationExecutor&& other) noexcept
      : ops_(other.ops_), end_ops_(other.end_ops_) {
    // Movable iff we're not running.
    DCHECK_EQ(other.promise_data_, nullptr);
  }
  InfallibleOperationExecutor& operator=(
      InfallibleOperationExecutor&& other) noexcept {
    DCHECK_EQ(other.promise_data_, nullptr);
    DCHECK_EQ(promise_data_, nullptr);
    ops_ = other.ops_;
    end_ops_ = other.end_ops_;
    return *this;
  }

  // IsRunning() is true if we're currently executing a sequence of operations.
  bool IsRunning() const { return promise_data_ != nullptr; }
  // Start executing a layout. May allocate space to store the relevant promise.
  // Returns the result of the first poll.
  // If the promise finishes, also destroy the promise data.
  Poll<T> Start(const Layout<InfallibleOperator<T>>* layout, T input,
                void* call_data);
  // Continue executing a layout. Returns the result of the next poll.
  // If the promise finishes, also destroy the promise data.
  Poll<T> Step(void* call_data);

 private:
  // Start polling on the current step of the layout.
  // `input` is the current value (either the input to the first step, or the
  // so far transformed value)
  // `call_data` is the call data for the filter stack.
  // If this op finishes immediately then we iterative move to the next step.
  // If we reach the end up the ops, we return the overall poll result,
  // otherwise we return Pending.
  Poll<T> InitStep(T input, void* call_data);
  // Continue polling on the current step of the layout.
  // Called on the next poll after InitStep returns pending.
  // If the promise is still pending, returns this.
  // If the promise completes we call into InitStep to continue execution
  // through the filters.
  Poll<T> ContinueStep(void* call_data);

  void* promise_data_ = nullptr;
  const InfallibleOperator<T>* ops_;
  const InfallibleOperator<T>* end_ops_;
};

class CallState {
 public:
  CallState();
  // Start the call: allows pulls to proceed
  void Start();
  // PUSH: client -> server
  void BeginPushClientToServerMessage();
  Poll<StatusFlag> PollPushClientToServerMessage();
  void ClientToServerHalfClose();
  // PULL: client -> server
  void FinishPullClientInitialMetadata();
  void BeginPullClientToServerMessage();
  Poll<StatusFlag> PollPullClientToServerMessageAvailable();
  void WaitingForClientToServerMessageAck();
  void AckClientToServerMessage();
  // PUSH: server -> client
  void PushServerInitialMetadata();
  void BeginPushServerToClientMessage();
  Poll<StatusFlag> PollPushServerToClientMessage();
  bool PushServerTrailingMetadata(bool cancel);
  // PULL: server -> client
  Poll<bool> PollPullServerInitialMetadataAvailable();
  void FinishPullServerInitialMetadata();
  void BeginPullServerToClientMessage();
  Poll<StatusFlag> PollPullServerToClientMessageAvailable();
  void WaitingForServerToClientMessageAck();
  void AckServerToClientMessage();
  Poll<Empty> PollServerTrailingMetadataAvailable();
  // Debug
  std::string DebugString() const;

 private:
  enum class ClientToServerPullState : uint8_t {
    // Ready to read: client initial metadata is there, but not yet processed
    // Next state: kIdle or kTerminated
    kBegin,
    // Main call loop: not reading
    // Next state: kReading or kProcessingClientToServerMessage, or kTerminated
    kIdle,
    // Main call loop: reading but no message available
    // Next state: kProcessingClientToServerMessage or kTerminated
    kReading,
    // Main call loop: processing one message
    // Next state: kWaitingForClientToServerMessageAck or kTerminated
    kProcessingClientToServerMessage,
    // Main call loop: waiting for the ack that a message has been processed
    // Next state: kIdle or kTerminated
    kWaitingForClientToServerMessageAck,
    // Client initial metadata has been processed
    // Request has finished
    kTerminated,
  };
  enum class ClientToServerPushState : uint8_t {
    kIdle,
    kPushedMessage,
    kPushedHalfClose,
    kPushedMessageAndHalfClosed,
    kFinished,
  };
  enum class ServerToClientPullState : uint8_t {
    // Not yet started: cannot read
    kUnstarted,
    kStarted,
    // Processing server initial metadata
    kProcessingServerInitialMetadata,
    // Main call loop: not reading
    kIdle,
    // Main call loop: reading but no message available
    kReading,
    // Main call loop: processing one message
    kProcessingServerToClientMessage,
    // Main call loop: waiting for the ack that a message has been processed
    kWaitingForServerToClientMessageAck,
    // Processing server trailing metadata
    kProcessingServerTrailingMetadata,
    kTerminated,
  };
  enum class ServerToClientMessagePushState : uint8_t {
    kIdle,
    kPushed,
    kFailed,
  };
  ClientToServerPullState client_to_server_pull_state_ : 3;
  ClientToServerPushState client_to_server_push_state_ : 3;
  ServerToClientPullState server_to_client_pull_state_ : 4;
  bool pushed_server_initial_metadata_ : 1;
  ServerToClientMessagePushState server_to_client_message_push_state_ : 2;
  bool pushed_server_trailing_metadata_ : 1;
  IntraActivityWaiter client_to_server_pull_waiter_;
  IntraActivityWaiter server_to_client_pull_waiter_;
  IntraActivityWaiter client_to_server_push_waiter_;
  IntraActivityWaiter server_to_client_push_waiter_;
};

template <typename Fn>
class ServerTrailingMetadataInterceptor {
 public:
  class Call {
   public:
    static const NoInterceptor OnClientInitialMetadata;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnClientToServerHalfClose;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnFinalize;
    void OnServerTrailingMetadata(ServerMetadata& md,
                                  ServerTrailingMetadataInterceptor* filter) {
      filter->fn_(md);
    }
  };

  explicit ServerTrailingMetadataInterceptor(Fn fn) : fn_(std::move(fn)) {}

 private:
  GPR_NO_UNIQUE_ADDRESS Fn fn_;
};
template <typename Fn>
const NoInterceptor
    ServerTrailingMetadataInterceptor<Fn>::Call::OnClientInitialMetadata;
template <typename Fn>
const NoInterceptor
    ServerTrailingMetadataInterceptor<Fn>::Call::OnServerInitialMetadata;
template <typename Fn>
const NoInterceptor
    ServerTrailingMetadataInterceptor<Fn>::Call::OnClientToServerMessage;
template <typename Fn>
const NoInterceptor
    ServerTrailingMetadataInterceptor<Fn>::Call::OnClientToServerHalfClose;
template <typename Fn>
const NoInterceptor
    ServerTrailingMetadataInterceptor<Fn>::Call::OnServerToClientMessage;
template <typename Fn>
const NoInterceptor ServerTrailingMetadataInterceptor<Fn>::Call::OnFinalize;

template <typename Fn>
class ClientInitialMetadataInterceptor {
 public:
  class Call {
   public:
    auto OnClientInitialMetadata(ClientMetadata& md,
                                 ClientInitialMetadataInterceptor* filter) {
      return filter->fn_(md);
    }
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnClientToServerHalfClose;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnFinalize;
  };

  explicit ClientInitialMetadataInterceptor(Fn fn) : fn_(std::move(fn)) {}

 private:
  GPR_NO_UNIQUE_ADDRESS Fn fn_;
};
template <typename Fn>
const NoInterceptor
    ClientInitialMetadataInterceptor<Fn>::Call::OnServerInitialMetadata;
template <typename Fn>
const NoInterceptor
    ClientInitialMetadataInterceptor<Fn>::Call::OnClientToServerMessage;
template <typename Fn>
const NoInterceptor
    ClientInitialMetadataInterceptor<Fn>::Call::OnClientToServerHalfClose;
template <typename Fn>
const NoInterceptor
    ClientInitialMetadataInterceptor<Fn>::Call::OnServerToClientMessage;
template <typename Fn>
const NoInterceptor
    ClientInitialMetadataInterceptor<Fn>::Call::OnServerTrailingMetadata;
template <typename Fn>
const NoInterceptor ClientInitialMetadataInterceptor<Fn>::Call::OnFinalize;

}  // namespace filters_detail

// Execution environment for a stack of filters.
// This is a per-call object.
class CallFilters {
 public:
  class StackBuilder;
  class StackTestSpouse;

  // A stack is an opaque, immutable type that contains the data necessary to
  // execute a call through a given set of filters.
  // It's reference counted so that it can be shared between many calls.
  // It contains pointers to the individual filters, yet it does not own those
  // pointers: it's expected that some other object will track that ownership.
  class Stack : public RefCounted<Stack> {
   public:
    ~Stack() override;

   private:
    friend class CallFilters;
    friend class StackBuilder;
    friend class StackTestSpouse;
    explicit Stack(filters_detail::StackData data) : data_(std::move(data)) {}
    const filters_detail::StackData data_;
  };

  // Build stacks... repeatedly call Add with each filter that contributes to
  // the stack, then call Build() to generate a ref counted Stack object.
  class StackBuilder {
   public:
    ~StackBuilder();

    template <typename FilterType>
    void Add(FilterType* filter) {
      const size_t call_offset = data_.AddFilter<FilterType>(filter);
      data_.AddClientInitialMetadataOp(filter, call_offset);
      data_.AddServerInitialMetadataOp(filter, call_offset);
      data_.AddClientToServerMessageOp(filter, call_offset);
      data_.AddClientToServerHalfClose(filter, call_offset);
      data_.AddServerToClientMessageOp(filter, call_offset);
      data_.AddServerTrailingMetadataOp(filter, call_offset);
      data_.AddFinalizer(filter, call_offset, &FilterType::Call::OnFinalize);
    }

    void AddOwnedObject(void (*destroy)(void* p), void* p) {
      data_.channel_data_destructors.push_back({destroy, p});
    }

    template <typename T>
    void AddOwnedObject(RefCountedPtr<T> p) {
      AddOwnedObject([](void* p) { static_cast<T*>(p)->Unref(); }, p.release());
    }

    template <typename T>
    void AddOwnedObject(std::unique_ptr<T> p) {
      AddOwnedObject([](void* p) { delete static_cast<T*>(p); }, p.release());
    }

    template <typename Fn>
    void AddOnClientInitialMetadata(Fn fn) {
      auto filter = std::make_unique<
          filters_detail::ClientInitialMetadataInterceptor<Fn>>(std::move(fn));
      Add(filter.get());
      AddOwnedObject(std::move(filter));
    }

    template <typename Fn>
    void AddOnServerTrailingMetadata(Fn fn) {
      auto filter = std::make_unique<
          filters_detail::ServerTrailingMetadataInterceptor<Fn>>(std::move(fn));
      Add(filter.get());
      AddOwnedObject(std::move(filter));
    }

    RefCountedPtr<Stack> Build();

   private:
    filters_detail::StackData data_;
  };

  explicit CallFilters(ClientMetadataHandle client_initial_metadata);
  ~CallFilters();

  CallFilters(const CallFilters&) = delete;
  CallFilters& operator=(const CallFilters&) = delete;
  CallFilters(CallFilters&&) = delete;
  CallFilters& operator=(CallFilters&&) = delete;

  void SetStack(RefCountedPtr<Stack> stack);

  // Access client initial metadata before it's processed
  ClientMetadata* unprocessed_client_initial_metadata() {
    return client_initial_metadata_.get();
  }

  // Client: Fetch client initial metadata
  // Returns a promise that resolves to ValueOrFailure<ClientMetadataHandle>
  GRPC_MUST_USE_RESULT auto PullClientInitialMetadata();
  // Server: Push server initial metadata
  // Returns a promise that resolves to a StatusFlag indicating success
  void PushServerInitialMetadata(ServerMetadataHandle md) {
    call_state_.PushServerInitialMetadata();
    server_initial_metadata_push_ = std::move(md);
  }
  // Client: Fetch server initial metadata
  // Returns a promise that resolves to ValueOrFailure<ServerMetadataHandle>
  GRPC_MUST_USE_RESULT auto PullServerInitialMetadata();
  // Client: Push client to server message
  // Returns a promise that resolves to a StatusFlag indicating success
  GRPC_MUST_USE_RESULT auto PushClientToServerMessage(MessageHandle message);
  // Client: Indicate that no more messages will be sent
  void FinishClientToServerSends() { call_state_.ClientToServerHalfClose(); }
  // Server: Fetch client to server message
  // Returns a promise that resolves to ValueOrFailure<MessageHandle>
  GRPC_MUST_USE_RESULT auto PullClientToServerMessage();
  // Server: Push server to client message
  // Returns a promise that resolves to a StatusFlag indicating success
  GRPC_MUST_USE_RESULT auto PushServerToClientMessage(MessageHandle message);
  // Server: Fetch server to client message
  // Returns a promise that resolves to ValueOrFailure<MessageHandle>
  GRPC_MUST_USE_RESULT auto PullServerToClientMessage();
  // Server: Indicate end of response
  // Closes the request entirely - no messages can be sent/received
  // If no server initial metadata has been sent, implies
  // NoServerInitialMetadata() called.
  void PushServerTrailingMetadata(ServerMetadataHandle md);
  // Client: Fetch server trailing metadata
  // Returns a promise that resolves to ServerMetadataHandle
  GRPC_MUST_USE_RESULT auto PullServerTrailingMetadata();
  // Server: Wait for server trailing metadata to have been sent
  // Returns a promise that resolves to a StatusFlag indicating whether the
  // request was cancelled or not -- failure to send trailing metadata is
  // considered a cancellation, as is actual cancellation -- but not application
  // errors.
  GRPC_MUST_USE_RESULT auto WasCancelled();
  // Client & server: fill in final_info with the final status of the call.
  void Finalize(const grpc_call_final_info* final_info);

  std::string DebugString() const;

 private:
  /*
   template <
       filters_detail::Layout<filters_detail::FallibleOperator<MessageHandle>>(
           filters_detail::StackData::* layout_ptr),
       void*(CallFilters::* push_ptr),
       void (filters_detail::CallState::* begin_push)(),
       Poll<StatusFlag> (filters_detail::CallState::* poll_push)()>
   struct MessagePromises {
     class Push {
      public:
       Push(CallFilters* filters, MessageHandle value)
           : filters_(filters), value_(std::move(value)) {
         CHECK(value_ != nullptr);
         if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
           gpr_log(GPR_INFO, "BeginPush[%p|%p]: %s", &call_state(), this,
                   call_state().DebugString().c_str());
         }
         CHECK_EQ(push_slot(), nullptr);
         BeginPush();
         push_slot() = this;
       }
       ~Push() {
         if (filters_ != nullptr) {
           if (value_ != nullptr) {
             call_state().DropPush();
           }
           CHECK(push_slot() == this);
           push_slot() = nullptr;
         }
       }

       Push(const Push&) = delete;
       Push& operator=(const Push&) = delete;
       Push(Push&& other) noexcept
           : filters_(std::exchange(other.filters_, nullptr)),
             value_(std::move(other.value_)) {
         if (filters_ != nullptr) {
           DCHECK(push_slot() == &other);
           push_slot() = this;
         }
       }
       Push& operator=(Push&&) = delete;

       Poll<StatusFlag> operator()() {
         if (value_ == nullptr) {
           CHECK_EQ(filters_, nullptr);
           if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
             gpr_log(GPR_INFO, "Push[|%p]: already done", this);
           }
           return Success{};
         }
         if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
           gpr_log(GPR_INFO, "Push[%p|%p]: %s", &call_state(), this,
                   call_state().DebugString().c_str());
         }
         auto r = PollPush();
         if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
           if (r.pending()) {
             gpr_log(GPR_INFO, "Push[%p|%p]: pending; %s", &call_state(), this,
                     call_state().DebugString().c_str());
           } else if (r.value().ok()) {
             gpr_log(GPR_INFO, "Push[%p|%p]: success; %s", &call_state(), this,
                     call_state().DebugString().c_str());
           } else {
             gpr_log(GPR_INFO, "Push[%p|%p]: failure; %s", &call_state(), this,
                     call_state().DebugString().c_str());
           }
         }
         if (r.ready()) {
           push_slot() = nullptr;
           filters_ = nullptr;
         }
         return r;
       }

       MessageHandle TakeValue() {
         if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
           gpr_log(GPR_INFO, "Push[%p|%p]: take value; %s", &call_state(), this,
                   call_state().DebugString().c_str());
         }
         CHECK(value_ != nullptr);
         CHECK(filters_ != nullptr);
         push_slot() = nullptr;
         filters_ = nullptr;
         return std::move(value_);
       }

       absl::string_view DebugString() const {
         return value_ != nullptr ? " (not pulled)" : "";
       }

      private:
       void*& push_slot() { return filters_->*push_ptr; }
       filters_detail::CallState& call_state() { return filters_->call_state_; }
       void BeginPush() { (call_state().*begin_push)(); }
       Poll<StatusFlag> PollPush() { return (call_state().*poll_push)(); }

       CallFilters* filters_;
       MessageHandle value_;
     };

     class Pull {
      public:
       explicit Pull(CallFilters* filters) : filters_(filters) {}
       ~Pull() {
         if (filters_ != nullptr) {
           // call_state().DropPull();
         }
       }

       Pull(const Pull&) = delete;
       Pull& operator=(const Pull&) = delete;
       Pull(Pull&& other) noexcept
           : filters_(std::exchange(other.filters_, nullptr)),
             executor_(std::move(other.executor_)) {}
       Pull& operator=(Pull&&) = delete;

       Poll<ValueOrFailure<absl::optional<MessageHandle>>> operator()() {
         CHECK(filters_ != nullptr);
         if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
           gpr_log(GPR_INFO, "PullMessage[%p|%p]: %s executor:%d",
   &call_state(), this, call_state().DebugString().c_str(),
                   executor_.IsRunning());
         }
         if (executor_.IsRunning()) {
           auto c = call_state().PollClosed();
           if (c.ready() && c.value()) {
             filters_->CancelDueToFailedPipeOperation();
             return Failure{};
           }
           return FinishOperationExecutor(executor_.Step(filters_->call_data_));
         }
         auto p = call_state().PollPull();
         auto* r = p.value_if_ready();
         if (r == nullptr) {
           if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
             gpr_log(GPR_INFO, "PullMessage[%p] pending: %s executor:%d",
                     &call_state(), call_state().DebugString().c_str(),
                     executor_.IsRunning());
           }
           return Pending{};
         }
         if (!r->ok()) {
           filters_->CancelDueToFailedPipeOperation();
           return Failure{};
         }
         if (!**r) {
           if (half_close_layout_ptr != nullptr) {
             filters_detail::RunHalfClose(
                 filters_->stack_->data_.*half_close_layout_ptr,
                 filters_->call_data_);
           }
           return absl::nullopt;
         }
         CHECK(filters_ != nullptr);
         return FinishOperationExecutor(executor_.Start(
             layout(), push()->TakeValue(), filters_->call_data_));
       }

      private:
       filters_detail::CallState& call_state() { return filters_->call_state_; }
       Push* push() { return static_cast<Push*>(filters_->*push_ptr); }
       const filters_detail::Layout<filters_detail::FallibleOperator<Message>>*
       layout() {
         return &(filters_->stack_->data_.*layout_ptr);
       }

       Poll<ValueOrFailure<absl::optional<MessageHandle>>>
       FinishOperationExecutor(Poll<filters_detail::ResultOr<MessageHandle>> p)
   { auto* r = p.value_if_ready(); if (r == nullptr) return Pending{};
         DCHECK(!executor_.IsRunning());
         if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
           gpr_log(GPR_INFO, "PullMessage[%p|%p] executor done: %s", &state(),
                   this, state().DebugString().c_str());
         }
         state().AckPull();
         if (r->ok != nullptr) return std::move(r->ok);
         filters_->PushServerTrailingMetadata(std::move(r->error));
         return Failure{};
       }

       CallFilters* filters_;
       filters_detail::OperationExecutor<MessageHandle> executor_;
     };
   };
   */

  template <typename T, typename GetValuePromise, typename Finish>
  class PullPromise {
   public:
    explicit PullPromise(CallFilters* filters, GetValuePromise get_value,
                         Finish finish)
        : filters_(filters),
          get_value_(std::move(get_value)),
          finish_(std::move(finish)) {}

    PullPromise(const PullPromise&) = delete;
    PullPromise& operator=(const PullPromise&) = delete;
    PullPromise(PullPromise&& other) noexcept
        : filters_(std::exchange(other.filters_, nullptr)),
          executor_(std::move(other.executor_)) {}
    PullPromise& operator=(PullPromise&&) = delete;

    auto operator()() {
      if (executor_.IsRunning()) {
        return FinishOperationExecutor(executor_.Step(filters_->call_data_));
      }
      auto p = get_value_(filters_);
      auto* r = p.value_if_ready();
      if (r == nullptr) return Pending{};
      return StartExecutor(std::move(*r));
    }

   private:
    filters_detail::CallState& call_state() { return filters_->call_state_; }

    Poll<ValueOrFailure<T>> StartExecutor(T input) {
      return FinishOperationExecutor(
          executor_.Start(&filters_->stack_->data_.client_initial_metadata,
                          std::move(input), filters_->call_data_));
    }

    Poll<ValueOrFailure<absl::optional<T>>> StartExecutor(
        absl::optional<T> input) {
      if (!input.has_value()) {
        finish_(filters_, absl::nullopt);
      }
      return StartExecutor(std::move(*input));
    }

    Poll<ValueOrFailure<T>> FinishOperationExecutor(
        Poll<filters_detail::ResultOr<T>> p) {
      auto* r = p.value_if_ready();
      if (r == nullptr) return Pending{};
      DCHECK(!executor_.IsRunning());
      const bool success = r->ok != nullptr;
      finish_(filters_, success);
      if (success) return std::move(r->ok);
      filters_->PushServerTrailingMetadata(std::move(r->error));
      return Failure{};
    }

    CallFilters* filters_;
    filters_detail::OperationExecutor<T> executor_;
    GPR_NO_UNIQUE_ADDRESS GetValuePromise get_value_;
    GPR_NO_UNIQUE_ADDRESS Finish finish_;
  };

  template <typename T, typename GetValuePromise, typename Finish>
  auto MakePullPromise(GetValuePromise get_value, Finish finish) {
    return PullPromise<T, GetValuePromise, Finish>(this, std::move(get_value),
                                                   std::move(finish));
  }

  class PullServerTrailingMetadataPromise {
   public:
    explicit PullServerTrailingMetadataPromise(CallFilters* filters)
        : filters_(filters) {}

    PullServerTrailingMetadataPromise(
        const PullServerTrailingMetadataPromise&) = delete;
    PullServerTrailingMetadataPromise& operator=(
        const PullServerTrailingMetadataPromise&) = delete;
    PullServerTrailingMetadataPromise(
        PullServerTrailingMetadataPromise&& other) noexcept
        : filters_(std::exchange(other.filters_, nullptr)),
          executor_(std::move(other.executor_)) {}
    PullServerTrailingMetadataPromise& operator=(
        PullServerTrailingMetadataPromise&&) = delete;

    Poll<ServerMetadataHandle> operator()() {
      if (executor_.IsRunning()) {
        auto r = executor_.Step(filters_->call_data_);
        if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
          if (r.pending()) {
            gpr_log(GPR_INFO,
                    "%s PullServerTrailingMetadata[%p]: Pending(but executing)",
                    GetContext<Activity>()->DebugTag().c_str(), filters_);
          } else {
            gpr_log(GPR_INFO, "%s PullServerTrailingMetadata[%p]: Ready: %s",
                    GetContext<Activity>()->DebugTag().c_str(), filters_,
                    r.value()->DebugString().c_str());
          }
        }
        return r;
      }
      if (call_state().PollServerTrailingMetadataAvailable().pending()) {
        return Pending{};
      }
      // If no stack has been set, we can just return the result of the call
      if (filters_->stack_ == nullptr) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
          gpr_log(GPR_INFO,
                  "%s PullServerTrailingMetadata[%p]: Ready(no-stack): %s",
                  GetContext<Activity>()->DebugTag().c_str(), filters_,
                  filters_->server_trailing_metadata_->DebugString().c_str());
        }
        return std::move(filters_->server_trailing_metadata_);
      }
      // Otherwise we need to process it through all the filters.
      auto r = executor_.Start(
          &filters_->stack_->data_.server_trailing_metadata,
          std::move(filters_->server_trailing_metadata_), filters_->call_data_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_promise_primitives)) {
        if (r.pending()) {
          gpr_log(GPR_INFO,
                  "%s PullServerTrailingMetadata[%p]: Pending(but executing)",
                  GetContext<Activity>()->DebugTag().c_str(), filters_);
        } else {
          gpr_log(GPR_INFO, "%s PullServerTrailingMetadata[%p]: Ready: %s",
                  GetContext<Activity>()->DebugTag().c_str(), filters_,
                  r.value()->DebugString().c_str());
        }
      }
      return r;
    }

   private:
    filters_detail::CallState& call_state() { return filters_->call_state_; }
    CallFilters* filters_;
    // TODO(ctiller): consider forcing all server trailing metadata handlers to
    // be immediate, removing the need for an executor here.
    filters_detail::InfallibleOperationExecutor<ServerMetadataHandle> executor_;
  };

  void CancelDueToFailedPipeOperation(SourceLocation but_where = {});

  RefCountedPtr<Stack> stack_;

  filters_detail::CallState call_state_;

  void* call_data_;
  ClientMetadataHandle client_initial_metadata_;
  ServerMetadataHandle server_initial_metadata_push_;

  // The following void*'s are pointers to a `Push` object (from above).
  // They are used to track the current push operation for each pipe.
  // It would be lovely for them to be typed pointers, but that would require
  // a recursive type definition since the location of this field needs to be
  // a template argument to the `Push` object itself.
  void* client_to_server_message_push_ = nullptr;
  void* server_to_client_message_push_ = nullptr;

  ServerMetadataHandle server_trailing_metadata_;
};

inline auto CallFilters::PullClientInitialMetadata() {
  return MakePullPromise<ClientMetadataHandle>(
      [](CallFilters* filters) {
        CHECK(filters->client_initial_metadata_ != nullptr);
        return std::move(filters->client_initial_metadata_);
      },
      [](CallFilters* filters, bool success) {
        filters->call_state_.FinishPullClientInitialMetadata();
      });
}

inline auto CallFilters::PullServerInitialMetadata() {
  return MakePullPromise<ServerMetadataHandle>(
      [](CallFilters* filters) -> Poll<absl::optional<ServerMetadataHandle>> {
        auto p = filters->call_state_.PollPullServerInitialMetadataAvailable();
        auto* r = p.value_if_ready();
        if (r == nullptr) return Pending{};
        if (!*r) return absl::nullopt;
        return std::move(filters->server_initial_metadata_push_);
      },
      [](CallFilters* filters, absl::optional<bool> result) {
        filters->call_state_.FinishPullServerInitialMetadata();
      });
}

inline auto CallFilters::PushClientToServerMessage(MessageHandle message) {
  CHECK(message != nullptr);
  // Lambda acts as a firewall to protect member functions of p from being
  // called.
  return [p = ClientToServerMessagePushPromise{
              this, std::move(message)}]() mutable { return p(); };
}

inline auto CallFilters::PullClientToServerMessage() {
  return ClientToServerMessagePromises::PullMessage<
      &filters_detail::StackData::client_to_server_half_close>{this};
}

inline auto CallFilters::PushServerToClientMessage(MessageHandle message) {
  CHECK(message != nullptr);
  // Lambda acts as a firewall to protect member functions of p from being
  // called.
  return [p = ServerToClientMessagePush{this, std::move(message)}]() mutable {
    return p();
  };
}

inline auto CallFilters::PullServerToClientMessage() {
  return ServerToClientMessagePromises::PullMessage<nullptr>{this};
}

inline auto CallFilters::PullServerTrailingMetadata() {
  return PullServerTrailingMetadataPromise(this);
}

inline auto CallFilters::WasCancelled() { return cancelled_.Wait(); }

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_CALL_FILTERS_H
