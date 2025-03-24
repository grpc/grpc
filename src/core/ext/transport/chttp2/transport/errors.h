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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_ERRORS_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_ERRORS_H

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

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

inline constexpr absl::string_view kNoError = "Ok";
inline constexpr absl::string_view kStreamIdShouldBeOdd =
    "HTTP2 Error : Streams initiated by a client MUST use odd-numbered stream "
    "identifiers.";

class Http2Error {
 public:
  static Http2Error Ok() {
    return Http2Error(Http2ErrorCode::kNoError, Http2ErrorType::kOk, kNoError);
  }
  static Http2Error ConnectionError(const Http2ErrorCode error_code,
                                    absl::string_view error_message) {
    return Http2Error(error_code, Http2ErrorType::kConnectionError,
                      error_message);
  }
  static Http2Error StreamError(const Http2ErrorCode error_code,
                                absl::string_view error_message) {
    return Http2Error(error_code, Http2ErrorType::kStreamError, error_message);
  }
  static Http2Error GrpcError(const Http2ErrorCode error_code,
                              absl::string_view error_message) {
    return Http2Error(error_code, Http2ErrorType::kGrpcError, error_message);
  }

  bool is_ok() const { return (code_ == Http2ErrorCode::kNoError); }
  bool is_connection_error() const {
    return (error_type_ == Http2ErrorType::kConnectionError);
  }
  bool is_stream_error() const {
    return (error_type_ == Http2ErrorType::kStreamError);
  }
  Http2ErrorCode error_code() const { return code_; };

  absl::Status absl_status() const {
    if (is_ok()) {
      return absl::OkStatus();
    }
    return absl::Status(ErrorCodeToStatusCode(), error_message_);
  }

 private:
  enum class Http2ErrorType : uint8_t {
    kOk = 0x0,
    kStreamError = 0x1,
    kConnectionError = 0x2,
    kGrpcError = 0x3,
  };
  Http2Error(const Http2ErrorCode code, Http2ErrorType error_type,
             absl::string_view error_message)
      : error_message_(error_message), code_(code), error_type_(error_type) {
    DCHECK(
        (code == Http2ErrorCode::kNoError &&
         error_type == Http2ErrorType::kOk) ||
        (code > Http2ErrorCode::kNoError && error_type > Http2ErrorType::kOk));
  }
  absl::StatusCode ErrorCodeToStatusCode() const {
    switch (code_) {
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
  absl::string_view error_message_;
  Http2ErrorCode code_;
  Http2ErrorType error_type_;
};

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_ERRORS_H
