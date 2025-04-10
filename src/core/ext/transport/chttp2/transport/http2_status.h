//
//
// Copyright 2025 gRPC authors.
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
//
//

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_STATUS_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_STATUS_H

#include <cstdint>
#include <string>
#include <variant>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace http2 {

// These error codes are as per RFC9113
// https://www.rfc-editor.org/rfc/rfc9113.html#name-error-codes
// The RFC tells us to use 32 bit, but since this is our internal
// representation, we can use a smaller value.
enum class Http2ErrorCode : uint8_t {
  kNoError = 0x0,
  kProtocolError = 0x1,
  kInternalError = 0x2,
  kFlowControlError = 0x3,
  kSettingsTimeout = 0x4,
  kStreamClosed = 0x5,
  kFrameSizeError = 0x6,
  kRefusedStream = 0x7,
  kCancel = 0x8,
  kCompressionError = 0x9,
  kConnectError = 0xa,
  kEnhanceYourCalm = 0xb,
  kInadequateSecurity = 0xc,
  kDoNotUse = 0xffu  // Force use of a default clause
};

class Http2Status {
 public:
  // Classifying if an error is a stream error or a connection Http2Status must
  // be done at the time of error object creation. Once the Http2Status object
  // is created, it is immutable. This is intentional.
  enum class Http2ErrorType : uint8_t {
    kOk = 0x0,
    kStreamError = 0x1,
    kConnectionError = 0x2,
  };

  static Http2Status Ok() { return Http2Status(); }

  static Http2Status Http2ConnectionError(const Http2ErrorCode error_code,
                                          std::string message) {
    return Http2Status(error_code, Http2ErrorType::kConnectionError, message);
  }

  static Http2Status Http2StreamError(const Http2ErrorCode error_code,
                                      std::string message) {
    return Http2Status(error_code, Http2ErrorType::kStreamError, message);
  }

  static Http2Status AbslConnectionError(const absl::StatusCode code) {
    return Http2Status(code, Http2ErrorType::kConnectionError);
  }

  static Http2Status AbslStreamError(const absl::StatusCode code) {
    return Http2Status(code, Http2ErrorType::kStreamError);
  }

  GRPC_MUST_USE_RESULT Http2ErrorType GetType() const { return error_type_; }

  // We only expect to use this in 2 places
  // 1. To know what error code to send in a HTTP2 RST_STREAM.
  // 2. In tests
  // Any other usage is strongly discouraged.
  GRPC_MUST_USE_RESULT Http2ErrorCode GetStreamErrorType() const {
    switch (http2_code_) {
      case Http2ErrorType::kOk:
        CHECK(false);
      case Http2ErrorType::kStreamError:
        return http2_code_;
      case Http2ErrorType::kConnectionError:
        CHECK(false);
    }
  }

  // We only expect to use this in 2 places
  // 1. To know what error code to send in a HTTP2 GOAWAY frame.
  // 2. In tests
  // Any other usage is strongly discouraged.
  GRPC_MUST_USE_RESULT Http2ErrorCode GetConnectionErrorType() const {
    switch (http2_code_) {
      case Http2ErrorType::kOk:
        CHECK(false);
      case Http2ErrorType::kStreamError:
        CHECK(false);
      case Http2ErrorType::kConnectionError:
        return http2_code_;
    }
  }

  // If an error code needs to be used along with promises, or passed out of the
  // transport, this function should be used.
  GRPC_MUST_USE_RESULT absl::Status absl_status() const {
    if (is_ok()) {
      return absl::OkStatus();
    }
    return absl::Status(absl_code_, message_);
  }

  std::string DebugString() {
    return absl::StrCat(DebugGetType(), " : ", message_,
                        ". Http2 Code : ", http2_code_);
  }

 private:
  explicit Http2Status()
      : http2_code_(Http2ErrorCode::kNoError),
        error_type_(Http2ErrorType::kOk),
        absl_code_(absl::StatusCode::kOk) {
    Validate();
  }

  explicit Http2Status(const absl::StatusCode code, const Http2ErrorType type,
                       std::string& message)
      : http2_code_((code == absl::StatusCode::kOk)
                        ? Http2ErrorCode::kNoError
                        : Http2ErrorCode::kInternalError),
        error_type_(type),
        absl_code_(code),
        message_(std::move(message)) {
    Validate();
  }

  explicit Http2Status(const Http2ErrorCode code, const Http2ErrorType type,
                       std::string& message)
      : http2_code_(code), error_type_(type), message_(std::move(message)) {
    absl_code_ = ErrorCodeToStatusCode();
    Validate();
  }

  void Validate() {
    DCHECK((http2_code_ == Http2ErrorCode::kNoError &&
            error_type_ == Http2ErrorType::kOk &&
            absl_code_ == absl::StatusCode::kOk) ||
           (http2_code_ > Http2ErrorCode::kNoError &&
            error_type_ > Http2ErrorType::kOk &&
            absl_code_ != absl::StatusCode::kOk));
    DCHECK(is_ok() ? message_.size() == 0 : message_.size() > 0);
  }

  absl::StatusCode ErrorCodeToStatusCode() const {
    switch (http2_code_) {
      case Http2ErrorCode::kNoError:
        return absl::StatusCode::kOk;

      // Majority return kInternal
      case Http2ErrorCode::kProtocolError:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kInternalError:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kFlowControlError:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kSettingsTimeout:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kStreamClosed:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kFrameSizeError:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kRefusedStream:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kCompressionError:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kConnectError:
        return absl::StatusCode::kInternal;

      case Http2ErrorCode::kCancel:
        return absl::StatusCode::kCancelled;
      case Http2ErrorCode::kEnhanceYourCalm:
        return absl::StatusCode::kAborted;
      case Http2ErrorCode::kInadequateSecurity:
        return absl::StatusCode::kPermissionDenied;

      case Http2ErrorCode::kDoNotUse:
        DCHECK(false) << "This error code should never be used";
        return absl::StatusCode::kUnknown;
      default:
        DCHECK(false) << "This error code should never be used";
        return absl::StatusCode::kUnknown;
    }
  }

  bool is_ok() const { return http2_code_ == Http2ErrorCode::kNoError; }

  std::string DebugGetType() {
    switch (error_type_) {
      case Http2ErrorType::kOk:
        return "Ok";
      case Http2ErrorType::kStreamError:
        return "Stream Error";
      case Http2ErrorType::kConnectionError:
        return "Connection Error";
    }
  }

  const Http2ErrorCode http2_code_;
  const Http2ErrorType error_type_;
  absl::StatusCode absl_code_;

  std::string message_;
};

// // A value if an operation was successful, or a failure flag if not.
// template <typename T>
// class ValueOrFailure {
//  public:
//   // NOLINTNEXTLINE(google-explicit-constructor)
//   ValueOrFailure(T value) : value_(std::move(value)) {}
//   // NOLINTNEXTLINE(google-explicit-constructor)
//   ValueOrFailure(Failure) {}
//   // NOLINTNEXTLINE(google-explicit-constructor)
//   ValueOrFailure(StatusFlag status) { CHECK(!status.ok()); }

//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static ValueOrFailure FromOptional(
//       std::optional<T> value) {
//     return ValueOrFailure{std::move(value)};
//   }

//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool ok() const {
//     return value_.has_value();
//   }
//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION StatusFlag status() const {
//     return StatusFlag(ok());
//   }

//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION const T& value() const {
//     return value_.value();
//   }
//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION T& value() { return value_.value(); }
//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION const T& operator*() const {
//     return *value_;
//   }
//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION T& operator*() { return *value_; }
//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION const T* operator->() const {
//     return &*value_;
//   }
//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION T* operator->() { return &*value_; }

//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool operator==(
//       const ValueOrFailure& other) const {
//     return value_ == other.value_;
//   }

//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool operator!=(
//       const ValueOrFailure& other) const {
//     return value_ != other.value_;
//   }

//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool operator==(const T& other) const
//   {
//     return value_ == other;
//   }

//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool operator!=(const T& other) const
//   {
//     return value_ != other;
//   }

//  private:
//   std::optional<T> value_;
// };

// template <typename T>
// inline std::ostream& operator<<(std::ostream& os,
//                                 const ValueOrFailure<T>& value) {
//   if (value.ok()) {
//     return os << "Success(" << *value << ")";
//   } else {
//     return os << "Failure";
//   }
// }

// template <typename Sink, typename T>
// void AbslStringify(Sink& sink, const ValueOrFailure<T>& value) {
//   if (value.ok()) {
//     sink.Append("Success(");
//     sink.Append(absl::StrCat(*value));
//     sink.Append(")");
//   } else {
//     sink.Append("Failure");
//   }
// }

// template <typename Sink, typename... Ts>
// void AbslStringify(Sink& sink, const ValueOrFailure<std::tuple<Ts...>>&
// value) {
//   if (value.ok()) {
//     sink.Append("Success(");
//     sink.Append(absl::StrCat("(", absl::StrJoin(*value, ", "), ")"));
//     sink.Append(")");
//   } else {
//     sink.Append("Failure");
//   }
// }

// template <typename T>
// GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline bool IsStatusOk(
//     const ValueOrFailure<T>& value) {
//   return value.ok();
// }

// template <typename T>
// GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline T TakeValue(
//     ValueOrFailure<T>&& value) {
//   return std::move(value.value());
// }

// template <typename T>
// GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline T TakeValue(
//     absl::StatusOr<T>&& value) {
//   return std::move(*value);
// }

// template <typename T>
// struct StatusCastImpl<absl::StatusOr<T>, ValueOrFailure<T>> {
//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static absl::StatusOr<T> Cast(
//       ValueOrFailure<T> value) {
//     return value.ok() ? absl::StatusOr<T>(std::move(value.value()))
//                       : absl::CancelledError();
//   }
// };

// template <typename T>
// struct StatusCastImpl<ValueOrFailure<T>, Failure> {
//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static ValueOrFailure<T> Cast(Failure)
//   {
//     return ValueOrFailure<T>(Failure{});
//   }
// };

// template <typename T>
// struct StatusCastImpl<ValueOrFailure<T>, StatusFlag&> {
//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static ValueOrFailure<T> Cast(
//       StatusFlag f) {
//     CHECK(!f.ok());
//     return ValueOrFailure<T>(Failure{});
//   }
// };

// template <typename T>
// struct StatusCastImpl<ValueOrFailure<T>, StatusFlag> {
//   GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static ValueOrFailure<T> Cast(
//       StatusFlag f) {
//     CHECK(!f.ok());
//     return ValueOrFailure<T>(Failure{});
//   }
// };

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_STATUS_H
