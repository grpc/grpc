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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_DETAIL_PROMISE_LIKE_H
#define GRPC_SRC_CORE_LIB_PROMISE_DETAIL_PROMISE_LIKE_H

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <utility>

#include "absl/meta/type_traits.h"
#include "src/core/channelz/property_list.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/util/function_signature.h"
#include "src/core/util/upb_utils.h"
#include "src/proto/grpc/channelz/v2/promise.upb.h"
#include "src/proto/grpc/channelz/v2/promise.upbdefs.h"
#include "upb/reflection/def.hpp"

// A Promise is a callable object that returns Poll<T> for some T.
// Often when we're writing code that uses promises, we end up wanting to also
// deal with code that completes instantaneously - that is, it returns some T
// where T is not Poll.
// PromiseLike wraps any callable that takes no parameters and implements the
// Promise interface. For things that already return Poll, this wrapping does
// nothing. For things that do not return Poll, we wrap the return type in Poll.
// This allows us to write things like:
//   Seq(
//     [] { return 42; },
//     ...)
// in preference to things like:
//   Seq(
//     [] { return Poll<int>(42); },
//     ...)
// or:
//   Seq(
//     [] -> Poll<int> { return 42; },
//     ...)
// leading to slightly more concise code and eliminating some rules that in
// practice people find hard to deal with.

namespace grpc_core {

namespace promise_detail {

template <typename Promise, typename = void>
constexpr bool kHasToProtoMethod = false;

template <typename Promise>
constexpr bool kHasToProtoMethod<
    Promise, std::void_t<decltype(std::declval<Promise>().ToProto(
                 static_cast<grpc_channelz_v2_Promise*>(nullptr),
                 static_cast<upb_Arena*>(nullptr)))>> = true;

template <typename Promise, typename = void>
constexpr bool kHasChannelzPropertiesMethod = false;

template <typename Promise>
constexpr bool kHasChannelzPropertiesMethod<
    Promise,
    std::void_t<decltype(std::declval<Promise>().ChannelzProperties())>> = true;

}  // namespace promise_detail

template <typename Promise>
void PromiseAsProto(const Promise& promise,
                    grpc_channelz_v2_Promise* promise_proto, upb_Arena* arena) {
  if constexpr (promise_detail::kHasToProtoMethod<Promise>) {
    promise.ToProto(promise_proto, arena);
  } else if constexpr (promise_detail::kHasChannelzPropertiesMethod<Promise>) {
    auto* custom_promise =
        grpc_channelz_v2_Promise_mutable_custom_promise(promise_proto, arena);
    grpc_channelz_v2_Promise_Custom_set_type(
        custom_promise, StdStringToUpbString(TypeName<Promise>()));
    promise.ChannelzProperties().FillUpbProto(
        grpc_channelz_v2_Promise_Custom_mutable_properties(custom_promise,
                                                           arena),
        arena);
  } else {
    grpc_channelz_v2_Promise_set_unknown_promise(
        promise_proto, StdStringToUpbString(TypeName<Promise>()));
  }
}

// Wrapper for Promises to convert them to PropertyValue types.
// Allows the type resolution logic to properly handle arbitrary promises.
template <typename T>
class PromiseProperty {
 public:
  explicit PromiseProperty(T* value) : value_(value) {}

  T* TakeValue() { return std::exchange(value_, nullptr); }

 private:
  T* value_;
};

template <typename T>
PromiseProperty(T* value) -> PromiseProperty<T>;

// TODO(ctiller): needed to avoid circular dependencies as we transition the
// codebase, but we'll need a better long-term solution here.
namespace channelz::property_list_detail {

class PromisePropertyValue final : public OtherPropertyValue {
 public:
  template <typename T>
  explicit PromisePropertyValue(T* value) {
    PromiseAsProto(*value, promise_proto_, arena_);
  }

  PromisePropertyValue(PromisePropertyValue&&) = delete;
  PromisePropertyValue& operator=(PromisePropertyValue&&) = delete;
  PromisePropertyValue(const PromisePropertyValue&) = delete;
  PromisePropertyValue& operator=(const PromisePropertyValue&) = delete;

  ~PromisePropertyValue() override { upb_Arena_Free(arena_); }

  void FillAny(google_protobuf_Any* any, upb_Arena* arena) override {
    size_t length;
    upb_Arena_Fuse(arena_, arena);
    auto* bytes =
        grpc_channelz_v2_Promise_serialize(promise_proto_, arena, &length);
    google_protobuf_Any_set_value(
        any, upb_StringView_FromDataAndSize(bytes, length));
    google_protobuf_Any_set_type_url(
        any,
        StdStringToUpbString("type.googleapis.com/grpc.channelz.v2.Promise"));
  }

  Json::Object TakeJsonObject() override {
    upb::DefPool def_pool;
    auto* def = grpc_channelz_v2_Promise_getmsgdef(def_pool.ptr());
    size_t length =
        upb_TextEncode(reinterpret_cast<upb_Message*>(promise_proto_), def,
                       def_pool.ptr(), 0, nullptr, 0);
    auto str = std::make_unique<char[]>(length);
    upb_TextEncode(reinterpret_cast<upb_Message*>(promise_proto_), def,
                   def_pool.ptr(), 0, str.get(), length);
    return {{"promise", Json::FromString(std::string(str.get()))}};
  }

 private:
  upb_Arena* arena_ = upb_Arena_New();
  grpc_channelz_v2_Promise* promise_proto_ =
      grpc_channelz_v2_Promise_new(arena_);
};

template <typename T>
struct Wrapper<PromiseProperty<T>> {
  static std::optional<PropertyValue> Wrap(PromiseProperty<T> value) {
    return PropertyValue(
        std::make_shared<PromisePropertyValue>(value.TakeValue()));
  }
};

}  // namespace channelz::property_list_detail

namespace promise_detail {

template <typename T>
struct PollWrapper {
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static Poll<T> Wrap(T&& x) {
    return Poll<T>(std::forward<T>(x));
  }
};

template <typename T>
struct PollWrapper<Poll<T>> {
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static Poll<T> Wrap(Poll<T>&& x) {
    return std::forward<Poll<T>>(x);
  }
};

template <typename T>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline auto WrapInPoll(T&& x)
    -> decltype(PollWrapper<T>::Wrap(std::forward<T>(x))) {
  return PollWrapper<T>::Wrap(std::forward<T>(x));
}

// T -> T, const T& -> T
template <typename T>
using RemoveCVRef = absl::remove_cv_t<absl::remove_reference_t<T>>;

template <typename F, typename SfinaeVoid = void>
class PromiseLike;

template <>
class PromiseLike<void>;

template <typename F>
class PromiseLike<
    F, absl::enable_if_t<!std::is_void<std::invoke_result_t<F>>::value>> {
 private:
  GPR_NO_UNIQUE_ADDRESS RemoveCVRef<F> f_;
  using OriginalResult = decltype(f_());
  using WrappedResult = decltype(WrapInPoll(std::declval<OriginalResult>()));

 public:
  // NOLINTNEXTLINE - internal detail that drastically simplifies calling code.
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION PromiseLike(F&& f)
      : f_(std::forward<F>(f)) {}
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION WrappedResult operator()() {
    return WrapInPoll(f_());
  }
  void ToProto(grpc_channelz_v2_Promise* promise_proto,
               upb_Arena* arena) const {
    PromiseAsProto(f_, promise_proto, arena);
  }
  PromiseLike(const PromiseLike&) = default;
  PromiseLike& operator=(const PromiseLike&) = default;
  PromiseLike(PromiseLike&&) = default;
  PromiseLike& operator=(PromiseLike&&) = default;
  using Result = typename PollTraits<WrappedResult>::Type;
};

template <typename F>
class PromiseLike<
    F, absl::enable_if_t<std::is_void<std::invoke_result_t<F>>::value>> {
 private:
  GPR_NO_UNIQUE_ADDRESS RemoveCVRef<F> f_;

 public:
  // NOLINTNEXTLINE - internal detail that drastically simplifies calling code.
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION PromiseLike(F&& f)
      : f_(std::forward<F>(f)) {}
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Poll<Empty> operator()() {
    f_();
    return Empty{};
  }
  void ToProto(grpc_channelz_v2_Promise* promise_proto,
               upb_Arena* arena) const {
    PromiseAsProto(f_, promise_proto, arena);
  }
  PromiseLike(const PromiseLike&) = default;
  PromiseLike& operator=(const PromiseLike&) = default;
  PromiseLike(PromiseLike&&) = default;
  PromiseLike& operator=(PromiseLike&&) = default;
  using Result = Empty;
};

}  // namespace promise_detail

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_DETAIL_PROMISE_LIKE_H
