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

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "google/protobuf/struct.upb.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/service/ext_proc/v3/external_processor.upb.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "upb/mem/arena.h"

namespace grpc_core {

struct ExtProcResponse {
  struct HeaderMutation {
    std::vector<XdsHeaderValueOption> set_headers;
    std::vector<std::string> remove_headers;
  };
  std::optional<absl::StatusOr<HeaderMutation>> request_headers;
  std::optional<absl::StatusOr<HeaderMutation>> response_headers;
  std::optional<HeaderMutation> response_trailers;
  struct BodyMutation {
    std::string body;
    bool end_of_stream = false;
    bool end_of_stream_without_message = false;
  };
  std::optional<absl::StatusOr<BodyMutation>> request_body;
  std::optional<absl::StatusOr<BodyMutation>> response_body;
  struct ImmediateResponse {
    uint32_t status;
    HeaderMutation header_mutation;
    std::string details;
  };
  std::optional<ImmediateResponse> immediate_response;
  bool mode_override = false;
  bool request_drain = false;
};

class ExtProcRequest {
 public:
  explicit ExtProcRequest(upb_Arena* arena);

  enum class RequestType {
    kClientHeaders,
    kServerHeaders,
    kClientMessage,
    kServerMessage,
    kServerTrailers,
  };

  std::string CreateRequest(
      RequestType type,
      std::variant<grpc_metadata_batch*, upb_StringView> payload,
      ::google_protobuf_Struct* attributes, bool observability_mode,
      bool is_first_message, bool send_request_body, bool send_response_body,
      bool end_of_stream = false, bool end_of_stream_without_message = false);

  std::string SerializeMessage();

 private:
  void SetRequestHeaders(envoy_config_core_v3_HeaderMap* headers,
                         bool end_of_stream);
  void SetResponseHeaders(envoy_config_core_v3_HeaderMap* headers,
                          bool end_of_stream);
  void SetRequestBody(upb_StringView buf, bool end_of_stream,
                      bool end_of_stream_without_message = false);
  void SetResponseBody(upb_StringView buf, bool end_of_stream);
  void SetResponseTrailers(envoy_config_core_v3_HeaderMap* trailer);
  void SetObservabilityMode(bool mode);
  void SetAttributes(::google_protobuf_Struct* attributes);
  void SetProtocolConfigRequest(bool is_first_message, bool send_body);
  void SetProtocolConfigResponse(bool is_first_message, bool send_body);

  upb_Arena* arena_;
  envoy_service_ext_proc_v3_ProcessingRequest* request_;
};

::google_protobuf_Struct* ParseAttributes(
    upb_Arena* arena, const std::vector<std::string>& requested_attributes,
    const grpc_metadata_batch& metadata);

absl::StatusOr<ExtProcResponse> ParseExtProcResponse(
    const envoy_service_ext_proc_v3_ProcessingResponse* response,
    bool observability_mode = false);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_EXT_PROC_EXT_PROC_MESSAGES_H
