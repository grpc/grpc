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

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <string>
#include <variant>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
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
  // is created, its Http2ErrorType is immutable. This is intentional.
  enum class Http2ErrorType : uint8_t {
    kOk = 0x0,
    kStreamError = 0x1,
    kConnectionError = 0x2,
  };

  static Http2Status Ok() { return Http2Status(); }

  // To pass message :
  // Either pass a absl::string_view which is then copied into a std::string.
  // Or, pass a local std::string using std::move
  static Http2Status Http2ConnectionError(const Http2ErrorCode error_code,
                                          std::string message) {
    return Http2Status(error_code, Http2ErrorType::kConnectionError, message);
  }

  static Http2Status Http2StreamError(const Http2ErrorCode error_code,
                                      std::string message) {
    return Http2Status(error_code, Http2ErrorType::kStreamError, message);
  }

  static Http2Status AbslConnectionError(const absl::StatusCode code,
                                         std::string message) {
    return Http2Status(code, Http2ErrorType::kConnectionError, message);
  }

  static Http2Status AbslStreamError(const absl::StatusCode code,
                                     std::string message) {
    return Http2Status(code, Http2ErrorType::kStreamError, message);
  }

  GRPC_MUST_USE_RESULT Http2ErrorType GetType() const { return error_type_; }

  // We only expect to use this in 2 places
  // 1. To know what error code to send in a HTTP2 RST_STREAM.
  // 2. In tests
  // Any other usage is strongly discouraged.
  GRPC_MUST_USE_RESULT Http2ErrorCode GetStreamErrorType() const {
    switch (error_type_) {
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
    switch (error_type_) {
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
    DCHECK((is_ok() && message_.empty()) || (!is_ok() && !message_.empty()));
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

// TODO(tjagtap): [PH2][P2] : We can add more methods and helpers as needed.
// This class is similar to ValueOrFailure but a more minamilasit version.
// Reference :
// https://github.com/grpc/grpc/blob/master/src/core/lib/promise/status_flag.h

// A value if an operation was successful, or a Http2Status if not.
template <typename T>
class ValueOrHttp2Status {
 public:
  // TODO(tjagtap): [PH2][P0] : some http2 frame types used to give some
  // compile issue with std::move. Check with tests.

  // NOLINTNEXTLINE(google-explicit-constructor)
  explicit ValueOrHttp2Status(T value) : value_(std::move(value)) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  explicit ValueOrHttp2Status(Http2Status status) : status_(std::move(status)) {
    CHECK(status_.GetType() != Http2Status::Http2ErrorType::kOk);
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool ok() const {
    return value_.has_value();
  }

  // Prefer TakeValue when you want std::move to be used
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION const T& value() const {
    return value_.value();
  }

  // Prefer TakeValue when you want std::move to be used
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION T& value() { return value_.value(); }

  GRPC_MUST_USE_RESULT Http2Status::Http2ErrorType GetErrorType() const {
    CHECK(status_.has_value());
    return status_.value().GetType();
  }

  GRPC_MUST_USE_RESULT Http2ErrorCode GetStreamErrorType() const {
    CHECK(status_.has_value());
    return status_.value().GetStreamErrorType();
  }

  GRPC_MUST_USE_RESULT Http2ErrorCode GetConnectionErrorType() const {
    CHECK(status_.has_value());
    return status_.value().GetConnectionErrorType();
  }

  std::string DebugString() const {
    CHECK(status_.has_value());
    return status_.value().DebugString();
  }

  GRPC_MUST_USE_RESULT absl::Status absl_status() const {
    CHECK(status_.has_value());
    return status_.value().absl_status();
  }

 private:
  friend T TakeValue(ValueOrHttp2Status<T>&& value);
  std::optional<T> value_;
  std::optional<T> status_;
};

template <typename T>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline T TakeValue(
    ValueOrHttp2Status<T>&& value) {
  return std::move(value.value());
}

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_STATUS_H
