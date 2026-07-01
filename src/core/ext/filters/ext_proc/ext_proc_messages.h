//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_EXT_PROC_EXT_PROC_MESSAGES_H
#define GRPC_SRC_CORE_EXT_FILTERS_EXT_PROC_EXT_PROC_MESSAGES_H

#include <grpc/status.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "envoy/config/core/v3/base.upb.h"
#include "envoy/service/ext_proc/v3/external_processor.upb.h"
#include "google/protobuf/struct.upb.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/util/matchers.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "upb/mem/arena.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

struct ExtProcProcessingMode {
  bool send_request_headers = false;
  bool send_response_headers = false;
  bool send_response_trailers = false;
  bool send_request_body = false;
  bool send_response_body = false;

  bool operator==(const ExtProcProcessingMode& other) const {
    return send_request_headers == other.send_request_headers &&
           send_response_headers == other.send_response_headers &&
           send_response_trailers == other.send_response_trailers &&
           send_request_body == other.send_request_body &&
           send_response_body == other.send_response_body;
  }

  std::string ToString() const;
};

absl::StatusOr<std::string> CreateClientHeadersRequest(
    upb_Arena* arena, grpc_metadata_batch* metadata,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    bool is_first_message, const ExtProcProcessingMode& processing_mode);

absl::StatusOr<std::string> CreateServerHeadersRequest(
    upb_Arena* arena, grpc_metadata_batch* metadata,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    bool is_first_message, const ExtProcProcessingMode& processing_mode,
    bool end_of_stream);

absl::StatusOr<std::string> CreateClientBodyRequest(
    upb_Arena* arena, absl::string_view body,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    bool is_first_message, const ExtProcProcessingMode& processing_mode,
    bool end_of_stream, bool end_of_stream_without_message);

absl::StatusOr<std::string> CreateServerBodyRequest(
    upb_Arena* arena, absl::string_view body,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    bool is_first_message, const ExtProcProcessingMode& processing_mode);

absl::StatusOr<std::string> CreateServerTrailersRequest(
    upb_Arena* arena, grpc_metadata_batch* trailers,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    bool is_first_message, const ExtProcProcessingMode& processing_mode);

struct ExtProcResponse {
  struct HeaderMutation {
    // Headers to set or append.
    std::vector<XdsHeaderValueOption> set_headers;
    // Header keys to remove.
    std::vector<std::string> remove_headers;
  };

  struct BodyMutation {
    // The new body content.
    std::string body;
    // If true, indicates the end of the stream.
    bool end_of_stream = false;
    // If true, indicates the end of the stream without a message.
    bool end_of_stream_without_message = false;
  };

  struct RequestHeaders {
    HeaderMutation mutation;
  };

  struct ResponseHeaders {
    HeaderMutation mutation;
  };

  struct ResponseTrailers {
    HeaderMutation mutation;
  };

  struct RequestBody {
    BodyMutation mutation;
  };

  struct ResponseBody {
    BodyMutation mutation;
  };

  struct ImmediateResponse {
    // The gRPC status code to return.
    grpc_status_code status = GRPC_STATUS_UNKNOWN;
    // error message to return with.
    std::string details;
    // Headers to set in the response.
    HeaderMutation header_mutation;
  };

  // The variant representing the actual response content.
  // It can hold one of the mutation types or std::monostate if no response
  // is set.
  using ResponseValue =
      std::variant<std::monostate, RequestHeaders, ResponseHeaders,
                   ResponseTrailers, RequestBody, ResponseBody,
                   ImmediateResponse>;

  ResponseValue response;
  // If true, indicates that the client request should be drained.
  bool request_drain = false;

  // Parses a serialized ProcessingResponse proto.
  // Returns the parsed ExtProcResponse, or an error status if parsing fails.
  static absl::StatusOr<ExtProcResponse> Parse(
      absl::string_view serialized_response);
};

::google_protobuf_Struct* CreateAttributesStructProto(
    upb_Arena* arena, const std::vector<std::string>& requested_attributes,
    const grpc_metadata_batch& metadata);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_PROC_EXT_PROC_MESSAGES_H
