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

#include "src/core/filter/ext_proc/ext_proc_messages.h"

#include <grpc/status.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/base.upb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.upb.h"
#include "envoy/service/ext_proc/v3/external_processor.upb.h"
#include "google/protobuf/struct.upb.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/call/status_util.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/matchers.h"
#include "src/core/util/string.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"
#include "upb/mem/arena.hpp"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

//
// ExtProcProcessingMode
//

std::string ExtProcProcessingMode::ToString() const {
  std::string result = "{";
  StrAppend(result, "send_request_headers=");
  StrAppend(result, send_request_headers ? "true" : "false");
  StrAppend(result, ", send_response_headers=");
  StrAppend(result, send_response_headers ? "true" : "false");
  StrAppend(result, ", send_response_trailers=");
  StrAppend(result, send_response_trailers ? "true" : "false");
  StrAppend(result, ", send_request_body=");
  StrAppend(result, send_request_body ? "true" : "false");
  StrAppend(result, ", send_response_body=");
  StrAppend(result, send_response_body ? "true" : "false");
  StrAppend(result, "}");
  return result;
}

//
// ExtProcResponse::Parse()
//

namespace {

absl::StatusOr<ExtProcResponse::HeaderMutation> ParseExtProcHeaderMutation(
    const envoy_service_ext_proc_v3_HeaderMutation* header_mutation) {
  if (header_mutation == nullptr) {
    return ExtProcResponse::HeaderMutation{};
  }
  ExtProcResponse::HeaderMutation header_mutation_response;
  size_t set_headers_size = 0;
  const envoy_config_core_v3_HeaderValueOption* const* set_headers =
      envoy_service_ext_proc_v3_HeaderMutation_set_headers(header_mutation,
                                                           &set_headers_size);
  for (size_t i = 0; i < set_headers_size; ++i) {
    ValidationErrors errors;
    auto parsed = ParseXdsHeaderValueOption(set_headers[i], &errors);
    if (!errors.ok()) {
      return errors.status(absl::StatusCode::kInternal,
                           "Failed to parse XdsHeaderValueOption");
    }
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

absl::Status ValidateCommonResponse(
    const envoy_service_ext_proc_v3_CommonResponse* common_response) {
  if (common_response == nullptr) {
    return absl::InternalError("common_response is not set");
  }
  int32_t status =
      envoy_service_ext_proc_v3_CommonResponse_status(common_response);
  if (status == envoy_service_ext_proc_v3_CommonResponse_CONTINUE_AND_REPLACE) {
    return absl::InternalError("CONTINUE_AND_REPLACE is not supported");
  }
  return absl::OkStatus();
}

absl::StatusOr<ExtProcResponse::HeaderMutation> ParseExtProcHeaders(
    const envoy_service_ext_proc_v3_CommonResponse* common_response) {
  auto status = ValidateCommonResponse(common_response);
  if (!status.ok()) return status;
  const envoy_service_ext_proc_v3_HeaderMutation* header_mutation =
      envoy_service_ext_proc_v3_CommonResponse_header_mutation(common_response);
  return ParseExtProcHeaderMutation(header_mutation);
}

absl::StatusOr<ExtProcResponse::BodyMutation> ParseExtProcBodyMutation(
    const envoy_service_ext_proc_v3_CommonResponse* common_response) {
  auto status = ValidateCommonResponse(common_response);
  if (!status.ok()) return status;
  const envoy_service_ext_proc_v3_BodyMutation* body_mutation =
      envoy_service_ext_proc_v3_CommonResponse_body_mutation(common_response);
  if (body_mutation == nullptr) {
    return absl::InternalError("body_mutation is not set");
  }
  auto streamed_response =
      envoy_service_ext_proc_v3_BodyMutation_streamed_response(body_mutation);
  if (streamed_response == nullptr) {
    return absl::InternalError("streamed_response is not set");
  }
  if (envoy_service_ext_proc_v3_StreamedBodyResponse_grpc_message_compressed(
          streamed_response)) {
    return absl::InternalError("grpc_message_compressed is not supported");
  }
  auto body =
      envoy_service_ext_proc_v3_StreamedBodyResponse_body(streamed_response);
  bool end_of_stream =
      envoy_service_ext_proc_v3_StreamedBodyResponse_end_of_stream(
          streamed_response);
  bool end_of_stream_without_message =
      envoy_service_ext_proc_v3_StreamedBodyResponse_end_of_stream_without_message(
          streamed_response);
  return ExtProcResponse::BodyMutation{
      UpbStringToStdString(body), end_of_stream, end_of_stream_without_message};
}
}  // namespace

absl::StatusOr<ExtProcResponse> ExtProcResponse::Parse(
    absl::string_view serialized_response) {
  upb::Arena arena;
  const auto* response = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized_response.data(), serialized_response.size(), arena.ptr());
  if (response == nullptr) {
    return absl::InternalError("Failed to parse ProcessingResponse");
  }
  ExtProcResponse ext_proc_response;
  // parse request_drain
  ext_proc_response.request_drain =
      envoy_service_ext_proc_v3_ProcessingResponse_request_drain(response);
  switch (
      envoy_service_ext_proc_v3_ProcessingResponse_response_case(response)) {
    case envoy_service_ext_proc_v3_ProcessingResponse_response_request_headers: {
      const envoy_service_ext_proc_v3_HeadersResponse* request_headers =
          envoy_service_ext_proc_v3_ProcessingResponse_request_headers(
              response);
      if (request_headers == nullptr) {
        return absl::InternalError("request_headers is not set");
      }
      const envoy_service_ext_proc_v3_CommonResponse* common_response =
          envoy_service_ext_proc_v3_HeadersResponse_response(request_headers);
      auto mutation = ParseExtProcHeaders(common_response);
      if (!mutation.ok()) return mutation.status();
      ext_proc_response.response = RequestHeaders{std::move(*mutation)};
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_response_headers: {
      const envoy_service_ext_proc_v3_HeadersResponse* response_headers =
          envoy_service_ext_proc_v3_ProcessingResponse_response_headers(
              response);
      if (response_headers == nullptr) {
        return absl::InternalError("response_headers is not set");
      }
      const envoy_service_ext_proc_v3_CommonResponse* common_response =
          envoy_service_ext_proc_v3_HeadersResponse_response(response_headers);
      auto mutation = ParseExtProcHeaders(common_response);
      if (!mutation.ok()) return mutation.status();
      ext_proc_response.response = ResponseHeaders{std::move(*mutation)};
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_response_trailers: {
      const envoy_service_ext_proc_v3_TrailersResponse* response_trailer =
          envoy_service_ext_proc_v3_ProcessingResponse_response_trailers(
              response);
      if (response_trailer == nullptr) {
        return absl::InternalError("response_trailer is not set");
      }
      const envoy_service_ext_proc_v3_HeaderMutation* header_mutation =
          envoy_service_ext_proc_v3_TrailersResponse_header_mutation(
              response_trailer);
      auto mutation = ParseExtProcHeaderMutation(header_mutation);
      if (!mutation.ok()) return mutation.status();
      ext_proc_response.response = ResponseTrailers{std::move(*mutation)};
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_request_body: {
      const envoy_service_ext_proc_v3_BodyResponse* request_body =
          envoy_service_ext_proc_v3_ProcessingResponse_request_body(response);
      if (request_body == nullptr) {
        return absl::InternalError("request_body is not set");
      }
      const envoy_service_ext_proc_v3_CommonResponse* common_response =
          envoy_service_ext_proc_v3_BodyResponse_response(request_body);
      auto mutation = ParseExtProcBodyMutation(common_response);
      if (!mutation.ok()) return mutation.status();
      ext_proc_response.response = RequestBody{std::move(*mutation)};
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_response_body: {
      const envoy_service_ext_proc_v3_BodyResponse* response_body =
          envoy_service_ext_proc_v3_ProcessingResponse_response_body(response);
      if (response_body == nullptr) {
        return absl::InternalError("response_body is not set");
      }
      const envoy_service_ext_proc_v3_CommonResponse* common_response =
          envoy_service_ext_proc_v3_BodyResponse_response(response_body);
      auto mutation = ParseExtProcBodyMutation(common_response);
      if (!mutation.ok()) return mutation.status();
      if (mutation->end_of_stream || mutation->end_of_stream_without_message) {
        return absl::InternalError(
            "end_of_stream / end_of_stream_without_message is not supported "
            "for response_body");
      }
      ext_proc_response.response = ResponseBody{std::move(*mutation)};
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_immediate_response: {
      const envoy_service_ext_proc_v3_ImmediateResponse* immediate_response =
          envoy_service_ext_proc_v3_ProcessingResponse_immediate_response(
              response);
      if (immediate_response == nullptr) {
        return absl::InternalError("immediate_response is not set");
      }
      ExtProcResponse::ImmediateResponse immediate_response_value;
      immediate_response_value.details = UpbStringToStdString(
          envoy_service_ext_proc_v3_ImmediateResponse_details(
              immediate_response));
      auto header_mutation = ParseExtProcHeaderMutation(
          envoy_service_ext_proc_v3_ImmediateResponse_headers(
              immediate_response));
      if (!header_mutation.ok()) return header_mutation.status();
      immediate_response_value.header_mutation = std::move(*header_mutation);
      auto grpc_status =
          envoy_service_ext_proc_v3_ImmediateResponse_grpc_status(
              immediate_response);
      if (grpc_status == nullptr) {
        return absl::InternalError(
            "grpc_status is not set in ImmediateResponse");
      }
      grpc_status_code status_code;
      if (!grpc_status_code_from_int(
              envoy_service_ext_proc_v3_GrpcStatus_status(grpc_status),
              &status_code)) {
        return absl::InternalError(
            "Invalid grpc status code in ImmediateResponse");
      }
      immediate_response_value.status = status_code;
      ext_proc_response.response = std::move(immediate_response_value);
      break;
    }
    case envoy_service_ext_proc_v3_ProcessingResponse_response_NOT_SET:
      break;
    default:
      return absl::InternalError(absl::StrCat(
          "Unsupported ProcessingResponse response case: ",
          envoy_service_ext_proc_v3_ProcessingResponse_response_case(
              response)));
  }
  return ext_proc_response;
}

//
// CreateExtProcRequest()
//

namespace {

// An encoder class used with grpc_metadata_batch::Encode() to iterate over all
// metadata entries in a batch and populate a upb envoy.config.core.v3.HeaderMap
// message.
//
// Filters metadata entries against allowed and disallowed StringMatcher lists:
// - If disallowed matchers are specified and a header key matches, it is
// skipped.
// - If allowed matchers are specified, only matching header keys are included.
// - In accordance with gRFC A102, for each forwarded header, the raw_value
// field is
//   populated instead of the value field to preserve exact binary/raw header
//   bytes.
class UpbHeaderMapEncoder {
 public:
  UpbHeaderMapEncoder(envoy_config_core_v3_HeaderMap* header_map,
                      upb_Arena* arena,
                      const std::vector<StringMatcher>& allowed_headers,
                      const std::vector<StringMatcher>& disallowed_headers)
      : header_map_(header_map),
        arena_(arena),
        allowed_headers_(allowed_headers),
        disallowed_headers_(disallowed_headers) {}

  void Encode(const Slice& key, const Slice& value) {
    Append(key.as_string_view(), value.as_string_view());
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType& value) {
    Append(Which::key(), Which::Encode(value).as_string_view());
  }

 private:
  ABSL_ATTRIBUTE_NOINLINE static bool HeaderInMatcher(
      absl::string_view key, const std::vector<StringMatcher>& matchers) {
    for (const auto& matcher : matchers) {
      if (matcher.Match(key)) return true;
    }
    return false;
  }

  ABSL_ATTRIBUTE_NOINLINE bool ShouldForwardHeader(
      absl::string_view key) const {
    if (disallowed_headers_.empty()) {
      return allowed_headers_.empty() || HeaderInMatcher(key, allowed_headers_);
    }
    if (HeaderInMatcher(key, disallowed_headers_)) {
      return false;
    }
    return allowed_headers_.empty() || HeaderInMatcher(key, allowed_headers_);
  }

  ABSL_ATTRIBUTE_NOINLINE void Append(absl::string_view key,
                                      absl::string_view value) {
    if (!ShouldForwardHeader(key)) {
      return;
    }
    auto* value_msg =
        envoy_config_core_v3_HeaderMap_add_headers(header_map_, arena_);
    envoy_config_core_v3_HeaderValue_set_key(
        value_msg, upb_StringView_FromDataAndSize(key.data(), key.size()));
    // Per gRFC A102, when writing, we always set the raw_value field and never
    // the value field.
    envoy_config_core_v3_HeaderValue_set_raw_value(
        value_msg, upb_StringView_FromDataAndSize(value.data(), value.size()));
  }

  envoy_config_core_v3_HeaderMap* header_map_;
  upb_Arena* arena_;
  const std::vector<StringMatcher>& allowed_headers_;
  const std::vector<StringMatcher>& disallowed_headers_;
};

void PopulateMetadataBatchToHeaderMap(
    grpc_metadata_batch& batch,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers, upb_Arena* arena,
    envoy_config_core_v3_HeaderMap* header_map) {
  UpbHeaderMapEncoder encoder(header_map, arena, allowed_headers,
                              disallowed_headers);
  batch.Encode(&encoder);
}

void SetExtProcRequestHeaders(
    upb_Arena* arena, envoy_config_core_v3_HeaderMap* headers,
    envoy_service_ext_proc_v3_ProcessingRequest* request) {
  auto http_headers = envoy_service_ext_proc_v3_HttpHeaders_new(arena);
  envoy_service_ext_proc_v3_HttpHeaders_set_headers(http_headers, headers);
  envoy_service_ext_proc_v3_HttpHeaders_set_end_of_stream(http_headers, false);
  envoy_service_ext_proc_v3_ProcessingRequest_set_request_headers(request,
                                                                  http_headers);
}

void SetExtProcResponseHeaders(
    upb_Arena* arena, envoy_config_core_v3_HeaderMap* headers,
    bool end_of_stream, envoy_service_ext_proc_v3_ProcessingRequest* request) {
  auto http_headers = envoy_service_ext_proc_v3_HttpHeaders_new(arena);
  envoy_service_ext_proc_v3_HttpHeaders_set_headers(http_headers, headers);
  if (end_of_stream) {
    envoy_service_ext_proc_v3_HttpHeaders_set_end_of_stream(http_headers,
                                                            end_of_stream);
  }
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_headers(
      request, http_headers);
}

void SetExtProcRequestBody(
    upb_Arena* arena, upb_StringView buf, bool end_of_stream,
    bool end_of_stream_without_message,
    envoy_service_ext_proc_v3_ProcessingRequest* request) {
  envoy_service_ext_proc_v3_HttpBody* body =
      envoy_service_ext_proc_v3_HttpBody_new(arena);
  envoy_service_ext_proc_v3_HttpBody_set_body(body, buf);
  if (end_of_stream) {
    envoy_service_ext_proc_v3_HttpBody_set_end_of_stream(body, end_of_stream);
  }
  if (end_of_stream_without_message) {
    envoy_service_ext_proc_v3_HttpBody_set_end_of_stream_without_message(body,
                                                                         true);
  }
  envoy_service_ext_proc_v3_ProcessingRequest_set_request_body(request, body);
}

void SetExtProcResponseBody(
    upb_Arena* arena, upb_StringView buf,
    envoy_service_ext_proc_v3_ProcessingRequest* request) {
  envoy_service_ext_proc_v3_HttpBody* body =
      envoy_service_ext_proc_v3_HttpBody_new(arena);
  envoy_service_ext_proc_v3_HttpBody_set_body(body, buf);
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_body(request, body);
}

void SetExtProcResponseTrailers(
    upb_Arena* arena, envoy_config_core_v3_HeaderMap* trailer,
    envoy_service_ext_proc_v3_ProcessingRequest* request) {
  auto http_trailers = envoy_service_ext_proc_v3_HttpTrailers_new(arena);
  envoy_service_ext_proc_v3_HttpTrailers_set_trailers(http_trailers, trailer);
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_trailers(
      request, http_trailers);
}

void SetExtProcAttributes(
    upb_Arena* arena, ::google_protobuf_Struct* attributes,
    envoy_service_ext_proc_v3_ProcessingRequest* request) {
  if (attributes == nullptr) return;
  constexpr absl::string_view kAttributeKey = "envoy.filters.http.ext_proc";
  envoy_service_ext_proc_v3_ProcessingRequest_attributes_set(
      request,
      upb_StringView_FromDataAndSize(kAttributeKey.data(),
                                     kAttributeKey.size()),
      attributes, arena);
}

void SetExtProcProtocolConfig(
    upb_Arena* arena, const ExtProcProcessingMode& processing_mode,
    envoy_service_ext_proc_v3_ProcessingRequest* request) {
  auto* protocol_config =
      envoy_service_ext_proc_v3_ProcessingRequest_mutable_protocol_config(
          request, arena);
  envoy_service_ext_proc_v3_ProtocolConfiguration_set_request_body_mode(
      protocol_config,
      processing_mode.send_request_body
          ? envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_GRPC
          : envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_NONE);
  envoy_service_ext_proc_v3_ProtocolConfiguration_set_response_body_mode(
      protocol_config,
      processing_mode.send_response_body
          ? envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_GRPC
          : envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_NONE);
}

absl::StatusOr<std::string> SerializeExtProcMessage(
    envoy_service_ext_proc_v3_ProcessingRequest* request, upb_Arena* arena) {
  size_t size;
  auto message = envoy_service_ext_proc_v3_ProcessingRequest_serialize(
      request, arena, &size);
  if (message == nullptr) {
    return absl::InternalError("Failed to serialize ProcessingRequest");
  }
  return std::string(message, size);
}

envoy_service_ext_proc_v3_ProcessingRequest* CreateCommonRequest(
    upb_Arena* arena, ::google_protobuf_Struct* attributes,
    bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode) {
  auto* request = envoy_service_ext_proc_v3_ProcessingRequest_new(arena);
  SetExtProcAttributes(arena, attributes, request);
  envoy_service_ext_proc_v3_ProcessingRequest_set_observability_mode(
      request, observability_mode);
  if (processing_mode.has_value()) {
    SetExtProcProtocolConfig(arena, *processing_mode, request);
  }
  return request;
}

// An encoder class used with grpc_metadata_batch::Encode() to iterate over all
// metadata entries in a batch and insert them as key-value pairs into a upb
// google.protobuf.Struct message.
//
// For each metadata entry encountered, it converts the key and value to string
// views and creates a google.protobuf.Value string field in the target Struct
// message on the arena.
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
  ABSL_ATTRIBUTE_NOINLINE void Append(absl::string_view key,
                                      absl::string_view value) {
    ::google_protobuf_Value* val_msg = ::google_protobuf_Value_new(arena_);
    ::google_protobuf_Value_set_string_value(
        val_msg, upb_StringView_FromDataAndSize(value.data(), value.size()));
    ::google_protobuf_Struct_fields_set(
        struct_msg_, upb_StringView_FromDataAndSize(key.data(), key.size()),
        val_msg, arena_);
  }

  ::google_protobuf_Struct* struct_msg_;
  upb_Arena* arena_;
};

}  // namespace

//
// CreateExtProcAttributesProtoStruct()
//

// TODO(rishesh): Support CEL attributes from A103 (except
// xds.cluster_metadata.filter_metadata) when adding support for ext_proc on
// the server side. See
// https://github.com/grpc/proposal/blob/master/A103-xds-composite-filter.md#cel-attributes
::google_protobuf_Struct* CreateExtProcAttributesProtoStruct(
    upb_Arena* arena, const std::vector<std::string>& attributes,
    const grpc_metadata_batch& metadata) {
  if (attributes.empty()) return nullptr;
  ::google_protobuf_Struct* struct_msg = ::google_protobuf_Struct_new(arena);
  auto add_field = [&](absl::string_view name, absl::string_view value) {
    ::google_protobuf_Value* val_msg = ::google_protobuf_Value_new(arena);
    ::google_protobuf_Value_set_string_value(
        val_msg, upb_StringView_FromDataAndSize(value.data(), value.size()));
    ::google_protobuf_Struct_fields_set(
        struct_msg, upb_StringView_FromDataAndSize(name.data(), name.size()),
        val_msg, arena);
  };
  for (const auto& attr : attributes) {
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
    } else if (attr == "request.method") {
      if (auto* method = metadata.get_pointer(HttpMethodMetadata())) {
        add_field(attr, HttpMethodMetadata::Encode(*method).as_string_view());
      } else {
        add_field(attr, "POST");
      }
    } else if (attr == "request.headers") {
      ::google_protobuf_Struct* headers_struct =
          ::google_protobuf_Struct_new(arena);
      UpbStructHeadersEncoder encoder(headers_struct, arena);
      metadata.Encode(&encoder);
      ::google_protobuf_Value* val_msg = ::google_protobuf_Value_new(arena);
      ::google_protobuf_Value_set_struct_value(val_msg, headers_struct);
      ::google_protobuf_Struct_fields_set(
          struct_msg, upb_StringView_FromDataAndSize(attr.data(), attr.size()),
          val_msg, arena);
    } else if (attr == "request.referer" || attr == "request.useragent" ||
               attr == "request.id") {
      absl::string_view key;
      if (attr == "request.referer") {
        key = "referer";
      } else if (attr == "request.useragent") {
        key = "user-agent";
      } else {
        key = "x-request-id";
      }
      std::string backing_str;
      std::optional<absl::string_view> val =
          metadata.GetStringValue(key, &backing_str);
      if (val.has_value()) add_field(attr, *val);
    } else if (attr == "request.query") {
      add_field(attr, "");
    }
  }
  return struct_msg;
}

namespace {

envoy_config_core_v3_HeaderMap* CreateUpbHeaderMap(
    upb_Arena* arena, grpc_metadata_batch& metadata,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers) {
  auto* upb_headers = envoy_config_core_v3_HeaderMap_new(arena);
  PopulateMetadataBatchToHeaderMap(metadata, allowed_headers,
                                   disallowed_headers, arena, upb_headers);
  return upb_headers;
}

absl::StatusOr<std::string> CreateRequestAndSerialize(
    upb_Arena* arena, ::google_protobuf_Struct* attributes,
    bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode,
    absl::FunctionRef<void(envoy_service_ext_proc_v3_ProcessingRequest*)>
        populate_payload) {
  auto* request = CreateCommonRequest(arena, attributes, observability_mode,
                                      processing_mode);
  populate_payload(request);
  return SerializeExtProcMessage(request, arena);
}

}  // namespace

absl::StatusOr<std::string> CreateExtProcClientHeadersRequest(
    upb_Arena* arena, grpc_metadata_batch* metadata,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode) {
  return CreateRequestAndSerialize(
      arena, attributes, observability_mode, processing_mode,
      [&](envoy_service_ext_proc_v3_ProcessingRequest* request) {
        auto* upb_headers = CreateUpbHeaderMap(
            arena, *metadata, allowed_headers, disallowed_headers);
        SetExtProcRequestHeaders(arena, upb_headers, request);
      });
}

absl::StatusOr<std::string> CreateExtProcServerHeadersRequest(
    upb_Arena* arena, grpc_metadata_batch* metadata,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode, bool end_of_stream) {
  return CreateRequestAndSerialize(
      arena, attributes, observability_mode, processing_mode,
      [&](envoy_service_ext_proc_v3_ProcessingRequest* request) {
        auto* upb_headers = CreateUpbHeaderMap(
            arena, *metadata, allowed_headers, disallowed_headers);
        SetExtProcResponseHeaders(arena, upb_headers, end_of_stream, request);
      });
}

absl::StatusOr<std::string> CreateExtProcClientBodyRequest(
    upb_Arena* arena, absl::string_view body,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode, bool end_of_stream,
    bool end_of_stream_without_message) {
  return CreateRequestAndSerialize(
      arena, attributes, observability_mode, processing_mode,
      [&](envoy_service_ext_proc_v3_ProcessingRequest* request) {
        SetExtProcRequestBody(
            arena, upb_StringView_FromDataAndSize(body.data(), body.size()),
            end_of_stream, end_of_stream_without_message, request);
      });
}

absl::StatusOr<std::string> CreateExtProcServerBodyRequest(
    upb_Arena* arena, absl::string_view body,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode) {
  return CreateRequestAndSerialize(
      arena, attributes, observability_mode, processing_mode,
      [&](envoy_service_ext_proc_v3_ProcessingRequest* request) {
        SetExtProcResponseBody(
            arena, upb_StringView_FromDataAndSize(body.data(), body.size()),
            request);
      });
}

absl::StatusOr<std::string> CreateExtProcServerTrailersRequest(
    upb_Arena* arena, grpc_metadata_batch* trailers,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    std::optional<ExtProcProcessingMode> processing_mode) {
  return CreateRequestAndSerialize(
      arena, attributes, observability_mode, processing_mode,
      [&](envoy_service_ext_proc_v3_ProcessingRequest* request) {
        auto* upb_trailers = CreateUpbHeaderMap(
            arena, *trailers, allowed_headers, disallowed_headers);
        SetExtProcResponseTrailers(arena, upb_trailers, request);
      });
}

}  // namespace grpc_core
