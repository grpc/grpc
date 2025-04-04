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

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace http2 {

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
  static Http2Status Ok() { return Http2Status(absl::StatusCode::kOk); }

  static Http2Status ConnectionError(const Http2ErrorCode error_code,
                                     absl::string_view message) {
    return Http2Status(error_code, Http2ErrorType::kConnectionError, message);
  }

  static Http2Status FrameSizeConnectionError(absl::string_view message) {
    return Http2Status(Http2ErrorCode::kFrameSizeError,
                       Http2ErrorType::kConnectionError, message);
  }

  static Http2Status ProtocolConnectionError(absl::string_view message) {
    return Http2Status(Http2ErrorCode::kProtocolError,
                       Http2ErrorType::kConnectionError, message);
  }

  static Http2Status StreamError(const Http2ErrorCode error_code,
                                 absl::string_view message) {
    return Http2Status(error_code, Http2ErrorType::kStreamError, message);
  }

  static Http2Status GrpcError(const Http2ErrorCode error_code,
                               absl::string_view message) {
    return Http2Status(error_code, Http2ErrorType::kGrpcError, message);
  }

  absl::Status absl_status() const {
    if (is_ok()) {
      return absl::OkStatus();
    }
    if (absl_code_ != absl::StatusCode::kOk) {
      return absl::Status(absl_code_, message_);
    }
    return absl::Status(ErrorCodeToStatusCode(), message_);
  }

 private:
  enum class Http2ErrorType : uint8_t {
    kOk = 0x0,
    kStreamError = 0x1,
    kConnectionError = 0x2,
    kGrpcError = 0x3,
  };

  Http2Status(const absl::StatusCode code)
      : http2_code_(Http2ErrorCode::kNoError),
        error_type_(Http2ErrorType::kOk),
        absl_code_(code) {}

  Http2Status(const absl::StatusCode code, absl::string_view message)
      : http2_code_(Http2ErrorCode::kNoError),
        error_type_(Http2ErrorType::kOk),
        absl_code_(code),
        message_(message) {}

  Http2Status(const Http2ErrorCode code, const Http2ErrorType type,
              absl::string_view message)
      : http2_code_(code),
        error_type_(type),
        absl_code_(absl::StatusCode::kOk),
        message_(message) {
    DCHECK((http2_code_ == Http2ErrorCode::kNoError &&
            error_type_ == Http2ErrorType::kOk) ||
           (http2_code_ > Http2ErrorCode::kNoError &&
            error_type_ > Http2ErrorType::kOk));
  }

  absl::StatusCode ErrorCodeToStatusCode() const {
    switch (http2_code_) {
      case Http2ErrorCode::kNoError:
        return absl::StatusCode::kOk;
      case Http2ErrorCode::kProtocolError:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kInternalError:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kFlowControlError:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kSettingsTimeout:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kStreamClosed:
        return absl::StatusCode::kAborted;
      case Http2ErrorCode::kFrameSizeError:
        return absl::StatusCode::kInvalidArgument;
      case Http2ErrorCode::kRefusedStream:
        return absl::StatusCode::kResourceExhausted;
      case Http2ErrorCode::kCancel:
        return absl::StatusCode::kCancelled;
      case Http2ErrorCode::kCompressionError:
        return absl::StatusCode::kInternal;
      case Http2ErrorCode::kConnectError:
        return absl::StatusCode::kUnavailable;
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

  bool is_ok() const {
    return http2_code_ == Http2ErrorCode::kNoError &&
           absl_code_ == absl::StatusCode::kOk;
  }

  Http2ErrorCode http2_code_;
  Http2ErrorType error_type_;
  absl::StatusCode absl_code_;

  absl::string_view message_;
};

template <typename T>
class Http2StatusOr {
  bool ok() const { return std::holds_alternative<T>(status_or_); }

  T get() { return std::get<T>(status_or_); }

  Http2Status status() { return std::get<Http2Status>(status_or_); }

 private:
  std::variant<Http2Status, T> status_or_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_STATUS_H
