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

#ifndef GRPC_SRC_CORE_FILTER_EXT_PROC_EXT_PROC_MESSAGES_H
#define GRPC_SRC_CORE_FILTER_EXT_PROC_EXT_PROC_MESSAGES_H

#include <grpc/status.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "google/protobuf/struct.upb.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/util/matchers.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "upb/mem/arena.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

// Data structures and message creation/parsing helpers for the xDS External
// Processing (ext_proc) filter in gRPC, as specified in gRFC A93
// (https://github.com/grpc/proposal/blob/master/A93-xds-ext-proc.md).

namespace grpc_core {

// Represents the processing mode configuration for an external processor
// stream, corresponding to
// envoy.extensions.filters.http.ext_proc.v3.ProcessingMode in gRFC A93. Note:
// In gRPC, when body processing is enabled (send_request_body /
// send_response_body), it always operates in GRPC mode (deframed gRPC messages
// sent one at a time).
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

// Creates a serialized envoy.service.ext_proc.v3.ProcessingRequest containing a
// HttpHeaders message for client request headers.
//
// Parameters:
//  - arena: The upb arena used for memory allocation during proto creation and
//  serialization.
//  - metadata: The gRPC metadata batch containing the client request headers.
//  - allowed_headers: List of string matchers specifying which headers are
//  allowed to be forwarded.
//  - disallowed_headers: List of string matchers specifying which headers must
//  be excluded.
//  - attributes: A protobuf Struct message containing request/connection
//  attributes to include, or nullptr.
//  - observability_mode: If true (observability mode in gRFC A93), indicates
//  that the external processor should only observe traffic asynchronously
//  without blocking or mutating the stream.
//  - processing_mode: If present, populates the protocol_config field in the
//  request (sent on the first message of a stream to configure desired
//  processing modes as per gRFC A93).
absl::StatusOr<std::string> CreateExtProcClientHeadersRequest(
    upb_Arena* arena, grpc_metadata_batch* metadata,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode);

// Creates a serialized envoy.service.ext_proc.v3.ProcessingRequest containing a
// HttpHeaders message for server response headers.
//
// Parameters:
//  - arena: The upb arena used for memory allocation during proto creation and
//  serialization.
//  - metadata: The gRPC metadata batch containing the server response headers.
//  - allowed_headers: List of string matchers specifying which headers are
//  allowed to be forwarded.
//  - disallowed_headers: List of string matchers specifying which headers must
//  be excluded.
//  - attributes: A protobuf Struct message containing request/connection
//  attributes to include, or nullptr.
//  - observability_mode: If true (observability mode in gRFC A93), indicates
//  that the external
//    processor should only observe traffic asynchronously without blocking or
//    mutating the stream.
//  - processing_mode: If present, populates the protocol_config field in the
//  request (sent on the
//    first message of a stream to configure desired processing modes as per
//    gRFC A93).
//  - end_of_stream: If true, indicates that this header message is also the end
//  of the HTTP/gRPC stream.
absl::StatusOr<std::string> CreateExtProcServerHeadersRequest(
    upb_Arena* arena, grpc_metadata_batch* metadata,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode, bool end_of_stream);

// Creates a serialized envoy.service.ext_proc.v3.ProcessingRequest containing a
// HttpBody message for a client request body payload chunk.
//
// Parameters:
//  - arena: The upb arena used for memory allocation during proto creation and
//  serialization.
//  - body: The payload data chunk of the body. In GRPC mode (gRFC A93), this
//  represents a complete, deframed gRPC message payload (not raw HTTP/2 DATA
//  frames).
//  - attributes: A protobuf Struct message containing request/connection
//  attributes to include, or nullptr.
//  - observability_mode: If true (observability mode in gRFC A93), indicates
//  that the external processor should only observe traffic asynchronously
//  without blocking or mutating the stream.
//  - processing_mode: If present, populates the protocol_config field in the
//  request (sent on the first message of a stream to configure desired
//  processing modes as per gRFC A93).
//  - end_of_stream: If true, indicates that this body chunk is the last message
//  on the stream.
//  - end_of_stream_without_message: If true, indicates end of stream with an
//  empty body chunk.
absl::StatusOr<std::string> CreateExtProcClientBodyRequest(
    upb_Arena* arena, absl::string_view body,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode, bool end_of_stream,
    bool end_of_stream_without_message);

// Creates a serialized envoy.service.ext_proc.v3.ProcessingRequest containing a
// HttpBody message for a server response body payload chunk.
//
// Parameters:
//  - arena: The upb arena used for memory allocation during proto creation and
//  serialization.
//  - body: The payload data chunk of the body. In GRPC mode (gRFC A93), this
//  represents a complete, deframed gRPC message payload (not raw HTTP/2 DATA
//  frames).
//  - attributes: A protobuf Struct message containing request/connection
//  attributes to include, or nullptr.
//  - observability_mode: If true (observability mode in gRFC A93), indicates
//  that the external processor should only observe traffic asynchronously
//  without blocking or mutating the stream.
//  - processing_mode: If present, populates the protocol_config field in the
//  request (sent on the first message of a stream to configure desired
//  processing modes as per gRFC A93).
absl::StatusOr<std::string> CreateExtProcServerBodyRequest(
    upb_Arena* arena, absl::string_view body,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode);

// Creates a serialized envoy.service.ext_proc.v3.ProcessingRequest containing a
// HttpTrailers message for server response trailers.
//
// Parameters:
//  - arena: The upb arena used for memory allocation during proto creation and
//  serialization.
//  - trailers: The gRPC metadata batch containing the server response trailers.
//  - allowed_headers: List of string matchers specifying which trailer keys are
//  allowed to be forwarded.
//  - disallowed_headers: List of string matchers specifying which trailer keys
//  must be excluded.
//  - attributes: A protobuf Struct message containing request/connection
//  attributes to include, or nullptr.
//  - observability_mode: If true (observability mode in gRFC A93), indicates
//  that the external processor should only observe traffic asynchronously
//  without blocking or mutating the stream.
//  - processing_mode: If present, populates the protocol_config field in the
//  request (sent on the first message of a stream to configure desired
//  processing modes as per gRFC A93).
absl::StatusOr<std::string> CreateExtProcServerTrailersRequest(
    upb_Arena* arena, grpc_metadata_batch* trailers,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode);

// Creates a protobuf Struct message (::google_protobuf_Struct*) containing
// connection and request metadata attributes requested by the external
// processor configuration.
//
// Parameters:
//  - arena: The upb arena used for allocating the Struct message and its
//  fields.
//  - requested_attributes: A list of attribute names (e.g., "request.path",
//  "request.method", "request.host") to extract and populate.
//  - metadata: The gRPC metadata batch from which attribute values (like
//  authority, method, path, or headers) are extracted.
//
// Returns:
//  A pointer to the newly created ::google_protobuf_Struct message on the
//  arena, or nullptr if no requested attributes were matched or populated.
::google_protobuf_Struct* CreateExtProcAttributesProtoStruct(
    upb_Arena* arena, const std::vector<std::string>& requested_attributes,
    const grpc_metadata_batch& metadata);

// Represents the parsed response from an external processor, corresponding to
// envoy.service.ext_proc.v3.ProcessingResponse in gRFC A93.
struct ExtProcResponse {
  struct HeaderMutation {
    // Headers to set or append.
    std::vector<XdsHeaderValueOption> set_headers;
    // Header keys to remove.
    std::vector<std::string> remove_headers;
  };

  // Represents body mutations returned by the external processor.
  // In GRPC mode (gRFC A93), the body string must be a complete deframed gRPC
  // message.
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
  // If true, indicates that the client request should be drained as specified
  // in gRFC A93: the filter sends a half-close on the ext_proc stream and
  // pauses data plane reading until the ext_proc server echoes remaining
  // messages and terminates the stream with OK status.
  bool request_drain = false;

  // Parses a serialized envoy.service.ext_proc.v3.ProcessingResponse proto.
  //
  // Parameters:
  //  - serialized_response: The raw string containing the protobuf-serialized
  //  ProcessingResponse.
  //
  // Returns:
  //  The parsed ExtProcResponse structure representing header/body/trailer
  //  mutations or immediate responses, or an absl::Status error if
  //  deserialization or validation fails.
  static absl::StatusOr<ExtProcResponse> Parse(
      absl::string_view serialized_response);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_EXT_PROC_EXT_PROC_MESSAGES_H
