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

#include "src/core/ext/filters/ext_proc/ext_proc_messages.h"

#include <string>
#include <vector>

#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.upb.h"
#include "envoy/service/ext_proc/v3/external_processor.upb.h"
#include "google/protobuf/struct.upb.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

namespace {

ExtProcResponse::HeaderMutation ParseHeaderMutation(
    const envoy_service_ext_proc_v3_HeaderMutation* header_mutation) {
  if (header_mutation == nullptr) {
    return {};
  }
  ExtProcResponse::HeaderMutation header_mutation_response;
  size_t set_headers_size = 0;
  const envoy_config_core_v3_HeaderValueOption* const* set_headers =
      envoy_service_ext_proc_v3_HeaderMutation_set_headers(header_mutation,
                                                           &set_headers_size);
  for (size_t i = 0; i < set_headers_size; ++i) {
    ValidationErrors errors;
    auto parsed = ParseHeaderValueOption(set_headers[i], &errors);
    header_mutation_response.set_headers.push_back(std::move(parsed));
  }
  size_t remove_headers_size = 0;
  upb_StringView const* remove_headers =
      envoy_service_ext_proc_v3_HeaderMutation_remove_headers(
          header_mutation, &remove_headers_size);
  for (size_t i = 0; i < remove_headers_size; ++i) {
    header_mutation_response.remove_headers.emplace_back(
        UpbStringToStdString(remove_headers[i]));
  }
  return header_mutation_response;
}

absl::StatusOr<ExtProcResponse::HeaderMutation> ParseHeaders(
    const envoy_service_ext_proc_v3_CommonResponse* common_response) {
  if (common_response == nullptr) {
    return absl::InvalidArgumentError("common_response is not available");
  }
  // parse ResponseStatus status if CONTINUE_AND_REPLACE return error
  int32_t status =
      envoy_service_ext_proc_v3_CommonResponse_status(common_response);
  if (status == envoy_service_ext_proc_v3_CommonResponse_CONTINUE_AND_REPLACE) {
    return absl::InvalidArgumentError("CONTINUE_AND_REPLACE is not supported");
  }
  // otherwise parse HeaderMutation header_mutation and return header mutation
  const envoy_service_ext_proc_v3_HeaderMutation* header_mutation =
      envoy_service_ext_proc_v3_CommonResponse_header_mutation(common_response);

  return ParseHeaderMutation(header_mutation);
}

absl::StatusOr<ExtProcResponse::BodyMutation> ParseBodyMutation(
    const envoy_service_ext_proc_v3_CommonResponse* common_response) {
  if (common_response == nullptr) {
    return ExtProcResponse::BodyMutation{};
  }
  int32_t status =
      envoy_service_ext_proc_v3_CommonResponse_status(common_response);
  if (status == envoy_service_ext_proc_v3_CommonResponse_CONTINUE_AND_REPLACE) {
    return absl::InvalidArgumentError("CONTINUE_AND_REPLACE is not supported");
  }
  const envoy_service_ext_proc_v3_BodyMutation* body_mutation =
      envoy_service_ext_proc_v3_CommonResponse_body_mutation(common_response);
  if (body_mutation == nullptr) {
    return absl::InvalidArgumentError("body_mutation is not available");
  }
  if (!envoy_service_ext_proc_v3_BodyMutation_has_streamed_response(
          body_mutation)) {
    return ExtProcResponse::BodyMutation{};
  }
  auto streamed_response =
      envoy_service_ext_proc_v3_BodyMutation_streamed_response(body_mutation);
  if (streamed_response == nullptr) {
    return absl::InvalidArgumentError("streamed_response is not available");
  }
  if (envoy_service_ext_proc_v3_StreamedBodyResponse_grpc_message_compressed(
          streamed_response)) {
    return absl::InvalidArgumentError(
        "grpc_message_compressed is not supported");
  }
  auto body =
      envoy_service_ext_proc_v3_StreamedBodyResponse_body(streamed_response);
  auto end_of_stream =
      envoy_service_ext_proc_v3_StreamedBodyResponse_end_of_stream(
          streamed_response);
  auto end_of_stream_without_message =
      envoy_service_ext_proc_v3_StreamedBodyResponse_end_of_stream_without_message(
          streamed_response);
  return ExtProcResponse::BodyMutation{
      UpbStringToStdString(body), end_of_stream, end_of_stream_without_message};
}

class UpbStructHeadersEncoder {
 public:
  UpbStructHeadersEncoder(::google_protobuf_Struct* struct_msg,
                          upb_Arena* arena)
      : struct_msg_(struct_msg), arena_(arena) {}

  void Encode(const Slice& key, const Slice& value) {
    Append(key.as_string_view(), value.as_string_view());
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType& value) {
    Append(Which::key(), Which::Encode(value).as_string_view());
  }

 private:
  void Append(absl::string_view key, absl::string_view value) {
    if (key.empty()) return;
    char* name_buf = static_cast<char*>(upb_Arena_Malloc(arena_, key.size()));
    memcpy(name_buf, key.data(), key.size());
    char* value_buf =
        static_cast<char*>(upb_Arena_Malloc(arena_, value.size()));
    memcpy(value_buf, value.data(), value.size());
    ::google_protobuf_Value* val_msg = ::google_protobuf_Value_new(arena_);
    ::google_protobuf_Value_set_string_value(
        val_msg, upb_StringView_FromDataAndSize(value_buf, value.size()));
    ::google_protobuf_Struct_fields_set(
        struct_msg_, upb_StringView_FromDataAndSize(name_buf, key.size()),
        val_msg, arena_);
  }

  ::google_protobuf_Struct* struct_msg_;
  upb_Arena* arena_;
};

}  // namespace

absl::StatusOr<ExtProcResponse> ParseExtProcResponse(
    const envoy_service_ext_proc_v3_ProcessingResponse* response,
    bool observability_mode) {
  ExtProcResponse ext_proc_response;
  if (response == nullptr) {
    return absl::InvalidArgumentError("response is null");
  }
  if (observability_mode) {
    return ext_proc_response;
  }
  // parse mode_override
  ext_proc_response.mode_override =
      envoy_service_ext_proc_v3_ProcessingResponse_mode_override(response);
  // parse request_drain
  ext_proc_response.request_drain =
      envoy_service_ext_proc_v3_ProcessingResponse_request_drain(response);
  switch (
      envoy_service_ext_proc_v3_ProcessingResponse_response_case(response)) {
    case envoy_service_ext_proc_v3_ProcessingResponse_response_request_headers: {
      const envoy_service_ext_proc_v3_HeadersResponse* request_headers =
          envoy_service_ext_proc_v3_ProcessingResponse_request_headers(
              response);
      const envoy_service_ext_proc_v3_CommonResponse* common_response =
          envoy_service_ext_proc_v3_HeadersResponse_response(request_headers);
      auto request_headers_response = ParseHeaders(common_response);
      if (!request_headers_response.ok()) {
        ext_proc_response.request_headers = request_headers_response.status();
      } else {
        ext_proc_response.request_headers =
            std::move(request_headers_response.value());
      }
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_response_headers: {
      const envoy_service_ext_proc_v3_HeadersResponse* response_headers =
          envoy_service_ext_proc_v3_ProcessingResponse_response_headers(
              response);
      const envoy_service_ext_proc_v3_CommonResponse* common_response =
          envoy_service_ext_proc_v3_HeadersResponse_response(response_headers);
      auto response_headers_response = ParseHeaders(common_response);
      if (!response_headers_response.ok()) {
        ext_proc_response.response_headers = response_headers_response.status();
      } else {
        ext_proc_response.response_headers =
            std::move(response_headers_response.value());
      }
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_response_trailers: {
      const envoy_service_ext_proc_v3_TrailersResponse* response_trailer =
          envoy_service_ext_proc_v3_ProcessingResponse_response_trailers(
              response);
      const envoy_service_ext_proc_v3_HeaderMutation* header_mutation =
          envoy_service_ext_proc_v3_TrailersResponse_header_mutation(
              response_trailer);
      ext_proc_response.response_trailers =
          ParseHeaderMutation(header_mutation);
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_request_body: {
      const envoy_service_ext_proc_v3_BodyResponse* request_body =
          envoy_service_ext_proc_v3_ProcessingResponse_request_body(response);
      const envoy_service_ext_proc_v3_CommonResponse* common_response =
          envoy_service_ext_proc_v3_BodyResponse_response(request_body);
      auto request_body_response = ParseBodyMutation(common_response);
      if (!request_body_response.ok()) {
        ext_proc_response.request_body = request_body_response.status();
      } else {
        ext_proc_response.request_body =
            std::move(request_body_response.value());
      }
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_response_body: {
      const envoy_service_ext_proc_v3_BodyResponse* response_body =
          envoy_service_ext_proc_v3_ProcessingResponse_response_body(response);
      const envoy_service_ext_proc_v3_CommonResponse* common_response =
          envoy_service_ext_proc_v3_BodyResponse_response(response_body);
      auto response_body_response = ParseBodyMutation(common_response);
      if (!response_body_response.ok()) {
        ext_proc_response.response_body = response_body_response.status();
      } else {
        ext_proc_response.response_body =
            std::move(response_body_response.value());
      }
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_immediate_response: {
      const envoy_service_ext_proc_v3_ImmediateResponse* immediate_response =
          envoy_service_ext_proc_v3_ProcessingResponse_immediate_response(
              response);
      ExtProcResponse::ImmediateResponse immediate_response_value;
      immediate_response_value.details = UpbStringToStdString(
          envoy_service_ext_proc_v3_ImmediateResponse_details(
              immediate_response));
      immediate_response_value.header_mutation = ParseHeaderMutation(
          envoy_service_ext_proc_v3_ImmediateResponse_headers(
              immediate_response));
      auto grpc_status =
          envoy_service_ext_proc_v3_ImmediateResponse_grpc_status(
              immediate_response);
      if (grpc_status != nullptr) {
        immediate_response_value.status =
            envoy_service_ext_proc_v3_GrpcStatus_status(grpc_status);
      }
      ext_proc_response.immediate_response =
          std::move(immediate_response_value);
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_NOT_SET:
    default:
      break;
  }
  return ext_proc_response;
}

namespace {

class UpbHeaderMapEncoder {
 public:
  UpbHeaderMapEncoder(envoy_config_core_v3_HeaderMap* header_map,
                      upb_Arena* arena)
      : header_map_(header_map), arena_(arena) {}

  void Encode(const Slice& key, const Slice& value) {
    Append(key.as_string_view(), value.as_string_view());
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType& value) {
    Append(Which::key(), Which::Encode(value).as_string_view());
  }

 private:
  void Append(absl::string_view key, absl::string_view value) {
    if (key.empty() || key.size() > 16384 || key == "host" ||
        value.size() > 16384) {
      return;
    }
    absl::string_view validation_key = key;
    if (absl::StartsWith(key, ":")) {
      validation_key = key.substr(1);
    }
    if (ValidateHeaderKeyIsLegal(validation_key) !=
        ValidateMetadataResult::kOk) {
      return;
    }
    auto* value_msg =
        envoy_config_core_v3_HeaderMap_add_headers(header_map_, arena_);
    char* key_buf = static_cast<char*>(upb_Arena_Malloc(arena_, key.size()));
    memcpy(key_buf, key.data(), key.size());
    envoy_config_core_v3_HeaderValue_set_key(
        value_msg, upb_StringView_FromDataAndSize(key_buf, key.size()));

    char* val_buf = static_cast<char*>(upb_Arena_Malloc(arena_, value.size()));
    memcpy(val_buf, value.data(), value.size());
    if (absl::EndsWith(key, "-bin")) {
      envoy_config_core_v3_HeaderValue_set_raw_value(
          value_msg, upb_StringView_FromDataAndSize(val_buf, value.size()));
    } else {
      envoy_config_core_v3_HeaderValue_set_value(
          value_msg, upb_StringView_FromDataAndSize(val_buf, value.size()));
    }
  }

  envoy_config_core_v3_HeaderMap* header_map_;
  upb_Arena* arena_;
};

void PopulateMetadataBatchToHeaderMap(
    grpc_metadata_batch& batch, envoy_config_core_v3_HeaderMap* header_map,
    upb_Arena* arena) {
  UpbHeaderMapEncoder encoder(header_map, arena);
  batch.Encode(&encoder);
}

void SetRequestHeaders(envoy_service_ext_proc_v3_ProcessingRequest* request,
                       upb_Arena* arena,
                       envoy_config_core_v3_HeaderMap* headers,
                       bool end_of_stream) {
  auto http_headers = envoy_service_ext_proc_v3_HttpHeaders_new(arena);
  envoy_service_ext_proc_v3_HttpHeaders_set_headers(http_headers, headers);
  envoy_service_ext_proc_v3_HttpHeaders_set_end_of_stream(http_headers,
                                                          end_of_stream);
  envoy_service_ext_proc_v3_ProcessingRequest_set_request_headers(request,
                                                                  http_headers);
}

void SetResponseHeaders(envoy_service_ext_proc_v3_ProcessingRequest* request,
                        upb_Arena* arena,
                        envoy_config_core_v3_HeaderMap* headers,
                        bool end_of_stream) {
  auto http_headers = envoy_service_ext_proc_v3_HttpHeaders_new(arena);
  envoy_service_ext_proc_v3_HttpHeaders_set_headers(http_headers, headers);
  envoy_service_ext_proc_v3_HttpHeaders_set_end_of_stream(http_headers,
                                                          end_of_stream);
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_headers(
      request, http_headers);
}

void SetRequestBody(envoy_service_ext_proc_v3_ProcessingRequest* request,
                    upb_Arena* arena, upb_StringView buf, bool end_of_stream,
                    bool end_of_stream_without_message) {
  envoy_service_ext_proc_v3_HttpBody* body =
      envoy_service_ext_proc_v3_HttpBody_new(arena);
  envoy_service_ext_proc_v3_HttpBody_set_body(body, buf);
  envoy_service_ext_proc_v3_HttpBody_set_end_of_stream(body, end_of_stream);
  if (end_of_stream_without_message) {
    envoy_service_ext_proc_v3_HttpBody_set_end_of_stream_without_message(body,
                                                                         true);
  } else if (buf.size == 0 && end_of_stream) {
    envoy_service_ext_proc_v3_HttpBody_set_end_of_stream_without_message(body,
                                                                         true);
  }
  envoy_service_ext_proc_v3_ProcessingRequest_set_request_body(request, body);
}

void SetResponseBody(envoy_service_ext_proc_v3_ProcessingRequest* request,
                     upb_Arena* arena, upb_StringView buf, bool end_of_stream) {
  envoy_service_ext_proc_v3_HttpBody* body =
      envoy_service_ext_proc_v3_HttpBody_new(arena);
  envoy_service_ext_proc_v3_HttpBody_set_body(body, buf);
  envoy_service_ext_proc_v3_HttpBody_set_end_of_stream(body, end_of_stream);
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_body(request, body);
}

void SetResponseTrailers(envoy_service_ext_proc_v3_ProcessingRequest* request,
                         upb_Arena* arena,
                         envoy_config_core_v3_HeaderMap* trailer) {
  auto http_trailers = envoy_service_ext_proc_v3_HttpTrailers_new(arena);
  envoy_service_ext_proc_v3_HttpTrailers_set_trailers(http_trailers, trailer);
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_trailers(
      request, http_trailers);
}

void SetObservabilityMode(envoy_service_ext_proc_v3_ProcessingRequest* request,
                          bool mode) {
  envoy_service_ext_proc_v3_ProcessingRequest_set_observability_mode(request,
                                                                     mode);
}

void SetAttributes(envoy_service_ext_proc_v3_ProcessingRequest* request,
                   upb_Arena* arena, ::google_protobuf_Struct* attributes) {
  if (attributes == nullptr) return;

  envoy_service_ext_proc_v3_ProcessingRequest_attributes_set(
      request,
      upb_StringView_FromDataAndSize("envoy.filters.http.ext_proc",
                                     sizeof("envoy.filters.http.ext_proc") - 1),
      attributes, arena);
}

void SetProtocolConfigRequest(
    envoy_service_ext_proc_v3_ProcessingRequest* request, upb_Arena* arena,
    bool is_first_message, bool send_body) {
  if (!is_first_message) return;
  auto* protocol_config =
      envoy_service_ext_proc_v3_ProcessingRequest_mutable_protocol_config(
          request, arena);
  if (send_body) {
    envoy_service_ext_proc_v3_ProtocolConfiguration_set_request_body_mode(
        protocol_config,
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_GRPC);
  } else {
    envoy_service_ext_proc_v3_ProtocolConfiguration_set_request_body_mode(
        protocol_config,
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_NONE);
  }
}

void SetProtocolConfigResponse(
    envoy_service_ext_proc_v3_ProcessingRequest* request, upb_Arena* arena,
    bool is_first_message, bool send_body) {
  if (!is_first_message) return;
  auto* protocol_config =
      envoy_service_ext_proc_v3_ProcessingRequest_mutable_protocol_config(
          request, arena);
  if (send_body) {
    envoy_service_ext_proc_v3_ProtocolConfiguration_set_response_body_mode(
        protocol_config,
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_GRPC);
  } else {
    envoy_service_ext_proc_v3_ProtocolConfiguration_set_response_body_mode(
        protocol_config,
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_NONE);
  }
}

std::string SerializeMessage(
    envoy_service_ext_proc_v3_ProcessingRequest* request, upb_Arena* arena) {
  size_t size;
  auto message = envoy_service_ext_proc_v3_ProcessingRequest_serialize(
      request, arena, &size);
  if (message == nullptr) return "";
  return std::string(message, size);
}

}  // namespace

std::string CreateExtProcRequest(
    upb_Arena* arena, ExtProcRequestType type,
    std::variant<grpc_metadata_batch*, upb_StringView> payload,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    bool is_first_message, bool send_request_body, bool send_response_body,
    bool end_of_stream, bool end_of_stream_without_message) {
  auto* request = envoy_service_ext_proc_v3_ProcessingRequest_new(arena);
  switch (type) {
    case ExtProcRequestType::kClientHeaders: {
      auto* upb_headers = envoy_config_core_v3_HeaderMap_new(arena);
      PopulateMetadataBatchToHeaderMap(*std::get<grpc_metadata_batch*>(payload),
                                       upb_headers, arena);
      SetRequestHeaders(request, arena, upb_headers, /*end_of_stream=*/false);
      break;
    }
    case ExtProcRequestType::kServerHeaders: {
      auto* upb_headers = envoy_config_core_v3_HeaderMap_new(arena);
      PopulateMetadataBatchToHeaderMap(*std::get<grpc_metadata_batch*>(payload),
                                       upb_headers, arena);
      SetResponseHeaders(request, arena, upb_headers,
                         /*end_of_stream=*/end_of_stream);
      break;
    }
    case ExtProcRequestType::kClientMessage: {
      SetRequestBody(request, arena, std::get<upb_StringView>(payload),
                     end_of_stream, end_of_stream_without_message);
      break;
    }
    case ExtProcRequestType::kServerMessage: {
      SetResponseBody(request, arena, std::get<upb_StringView>(payload),
                      /*end_of_stream=*/false);
      break;
    }
    case ExtProcRequestType::kServerTrailers: {
      auto* upb_trailers = envoy_config_core_v3_HeaderMap_new(arena);
      PopulateMetadataBatchToHeaderMap(*std::get<grpc_metadata_batch*>(payload),
                                       upb_trailers, arena);
      SetResponseTrailers(request, arena, upb_trailers);
      break;
    }
  }
  SetAttributes(request, arena, attributes);
  SetObservabilityMode(request, observability_mode);
  SetProtocolConfigRequest(request, arena, is_first_message, send_request_body);
  SetProtocolConfigResponse(request, arena, is_first_message,
                            send_response_body);
  return SerializeMessage(request, arena);
}

::google_protobuf_Struct* ParseAttributes(
    upb_Arena* arena, const std::vector<std::string>& requested_attributes,
    const grpc_metadata_batch& metadata) {
  if (requested_attributes.empty()) return nullptr;
  ::google_protobuf_Struct* struct_msg = google_protobuf_Struct_new(arena);
  auto add_field = [&](absl::string_view name, absl::string_view value) {
    char* name_buf = static_cast<char*>(upb_Arena_Malloc(arena, name.size()));
    memcpy(name_buf, name.data(), name.size());
    char* value_buf = static_cast<char*>(upb_Arena_Malloc(arena, value.size()));
    memcpy(value_buf, value.data(), value.size());
    ::google_protobuf_Value* val_msg = ::google_protobuf_Value_new(arena);
    ::google_protobuf_Value_set_string_value(
        val_msg, upb_StringView_FromDataAndSize(value_buf, value.size()));
    ::google_protobuf_Struct_fields_set(
        struct_msg, upb_StringView_FromDataAndSize(name_buf, name.size()),
        val_msg, arena);
  };

  for (const auto& attr : requested_attributes) {
    if (attr == "request.path" || attr == "request.url_path") {
      if (const Slice* path = metadata.get_pointer(HttpPathMetadata())) {
        add_field(attr, path->as_string_view());
      }
    } else if (attr == "request.host") {
      if (const Slice* auth = metadata.get_pointer(HttpAuthorityMetadata())) {
        add_field(attr, auth->as_string_view());
      } else if (const Slice* host = metadata.get_pointer(HostMetadata())) {
        add_field(attr, host->as_string_view());
      }
    } else if (attr == "request.scheme") {
      // Not set - omit from map entirely per specification.
    } else if (attr == "request.method") {
      if (auto* method = metadata.get_pointer(HttpMethodMetadata())) {
        add_field(attr, HttpMethodMetadata::Encode(*method).as_string_view());
      } else {
        add_field(attr, "POST");
      }
    } else if (attr == "request.headers") {
      ::google_protobuf_Struct* headers_struct =
          google_protobuf_Struct_new(arena);
      UpbStructHeadersEncoder encoder(headers_struct, arena);
      const_cast<grpc_metadata_batch&>(metadata).Encode(&encoder);
      char* name_buf = static_cast<char*>(upb_Arena_Malloc(arena, attr.size()));
      memcpy(name_buf, attr.data(), attr.size());
      ::google_protobuf_Value* val_msg = ::google_protobuf_Value_new(arena);
      ::google_protobuf_Value_set_struct_value(val_msg, headers_struct);
      ::google_protobuf_Struct_fields_set(
          struct_msg, upb_StringView_FromDataAndSize(name_buf, attr.size()),
          val_msg, arena);
    } else if (attr == "request.referer") {
      std::string ref_str;
      auto val = metadata.GetStringValue("referer", &ref_str);
      if (val.has_value()) add_field(attr, *val);
    } else if (attr == "request.useragent") {
      std::string ua_str;
      auto val = metadata.GetStringValue("user-agent", &ua_str);
      if (val.has_value()) add_field(attr, *val);
    } else if (attr == "request.time") {
      // Not set - omit from map entirely per specification.
    } else if (attr == "request.id") {
      std::string id_str;
      auto val = metadata.GetStringValue("x-request-id", &id_str);
      if (val.has_value()) add_field(attr, *val);
    } else if (attr == "request.protocol") {
      // Not set - omit from map entirely per specification.
    } else if (attr == "request.query") {
      add_field(attr, "");
    }
  }
  return struct_msg;
}

}  // namespace grpc_core
