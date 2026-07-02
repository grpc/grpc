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

#include "envoy/config/core/v3/base.upb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.upb.h"
#include "envoy/service/ext_proc/v3/external_processor.upb.h"
#include "google/protobuf/struct.upb.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/call/status_util.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/util/matchers.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

//
// ExtProcProcessingMode
//

std::string ExtProcProcessingMode::ToString() const {
  std::string result = "{";
  absl::StrAppend(&result, "send_request_headers=");
  absl::StrAppend(&result, send_request_headers ? "true" : "false");
  absl::StrAppend(&result, ", send_response_headers=");
  absl::StrAppend(&result, send_response_headers ? "true" : "false");
  absl::StrAppend(&result, ", send_response_trailers=");
  absl::StrAppend(&result, send_response_trailers ? "true" : "false");
  absl::StrAppend(&result, ", send_request_body=");
  absl::StrAppend(&result, send_request_body ? "true" : "false");
  absl::StrAppend(&result, ", send_response_body=");
  absl::StrAppend(&result, send_response_body ? "true" : "false");
  absl::StrAppend(&result, "}");
  return result;
}

//
// ExtProcResponse::Parse()
//

namespace {

// TODO(rishesh): The following functions (GetHeaderValue, ParseXdsHeader,
// ParseXdsHeaderValueOptionAppendAction, ParseXdsHeaderValueOption) are
// duplicated from src/core/xds/grpc/xds_common_types_parser.cc to break the
// dependency cycle (grpc_xds_client -> grpc_ext_proc_filter ->
// ext_proc_messages -> grpc_xds_client).
// Consider refactoring these parser helpers into a separate target that
// does not depend on grpc_xds_client.
std::optional<std::string> GetHeaderValue(upb_StringView upb_value,
                                          bool is_binary,
                                          absl::string_view field_name,
                                          ValidationErrors* errors) {
  absl::string_view value = UpbStringToAbsl(upb_value);
  if (value.empty()) return std::nullopt;
  ValidationErrors::ScopedField field(errors, field_name);
  if (value.size() > 16384) errors->AddError("longer than 16384 bytes");
  if (is_binary) {
    std::string decoded_value;
    if (!absl::Base64Unescape(value, &decoded_value)) {
      errors->AddError("invalid base64");
    }
    return decoded_value;
  }
  ValidateMetadataResult result = ValidateNonBinaryHeaderValueIsLegal(value);
  if (result != ValidateMetadataResult::kOk) {
    errors->AddError(ValidateMetadataResultToString(result));
  }
  return std::string(value);
}

std::pair<std::string, std::string> ParseXdsHeader(
    const envoy_config_core_v3_HeaderValue* header_value,
    ValidationErrors* errors) {
  // key
  absl::string_view key =
      UpbStringToAbsl(envoy_config_core_v3_HeaderValue_key(header_value));
  {
    ValidationErrors::ScopedField field(errors, ".key");
    if (key.size() > 16384) errors->AddError("longer than 16384 bytes");
    if (absl::StartsWith(key, ":") || absl::StartsWith(key, "grpc-") ||
        key == "host") {
      errors->AddError(absl::StrCat("header \"", key, "\" not allowed"));
    } else {
      ValidateMetadataResult result = ValidateHeaderKeyIsLegal(key);
      if (result != ValidateMetadataResult::kOk) {
        errors->AddError(ValidateMetadataResultToString(result));
      }
    }
  }
  bool is_binary = absl::EndsWith(key, "-bin");
  std::optional<std::string> value =
      GetHeaderValue(envoy_config_core_v3_HeaderValue_raw_value(header_value),
                     is_binary, ".raw_value", errors);
  if (!value.has_value()) {
    value = GetHeaderValue(envoy_config_core_v3_HeaderValue_value(header_value),
                           is_binary, ".value", errors);
  }
  return {std::string(key), value.has_value() ? std::move(*value) : ""};
}

XdsHeaderValueOption::AppendAction ParseXdsHeaderValueOptionAppendAction(
    int32_t header_value_option_append_action, ValidationErrors* errors) {
  switch (header_value_option_append_action) {
    case envoy_config_core_v3_HeaderValueOption_APPEND_IF_EXISTS_OR_ADD:
      return XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd;
    case envoy_config_core_v3_HeaderValueOption_ADD_IF_ABSENT:
      return XdsHeaderValueOption::AppendAction::kAddIfAbsent;
    case envoy_config_core_v3_HeaderValueOption_OVERWRITE_IF_EXISTS_OR_ADD:
      return XdsHeaderValueOption::AppendAction::kOverwriteIfExistsOrAdd;
    case envoy_config_core_v3_HeaderValueOption_OVERWRITE_IF_EXISTS:
      return XdsHeaderValueOption::AppendAction::kOverwriteIfExists;
    default:
      errors->AddError("unsupported append action");
      return XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd;
  }
}

XdsHeaderValueOption ParseXdsHeaderValueOption(
    const envoy_config_core_v3_HeaderValueOption* header_value_option_config,
    ValidationErrors* errors) {
  if (header_value_option_config == nullptr) {
    errors->AddError("field is not present");
    return {};
  }
  XdsHeaderValueOption header_value_option;
  // parse header
  {
    ValidationErrors::ScopedField field(errors, ".header");
    if (const auto* header = envoy_config_core_v3_HeaderValueOption_header(
            header_value_option_config);
        header != nullptr) {
      header_value_option.header = ParseXdsHeader(header, errors);
    } else {
      errors->AddError("field not set");
    }
  }
  // parse header_append_action
  {
    ValidationErrors::ScopedField field(errors, ".append_action");
    int32_t header_append_action =
        envoy_config_core_v3_HeaderValueOption_append_action(
            header_value_option_config);
    header_value_option.append_action =
        ParseXdsHeaderValueOptionAppendAction(header_append_action, errors);
  }
  return header_value_option;
}

absl::StatusOr<ExtProcResponse::HeaderMutation> ParseHeaderMutation(
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

absl::StatusOr<ExtProcResponse::HeaderMutation> ParseHeaders(
    const envoy_service_ext_proc_v3_CommonResponse* common_response) {
  if (common_response == nullptr) {
    return absl::InternalError("common_response is not set");
  }
  // parse ResponseStatus status if CONTINUE_AND_REPLACE return error
  int32_t status =
      envoy_service_ext_proc_v3_CommonResponse_status(common_response);
  if (status == envoy_service_ext_proc_v3_CommonResponse_CONTINUE_AND_REPLACE) {
    return absl::InternalError("CONTINUE_AND_REPLACE is not supported");
  }
  // otherwise parse HeaderMutation header_mutation and return header mutation
  const envoy_service_ext_proc_v3_HeaderMutation* header_mutation =
      envoy_service_ext_proc_v3_CommonResponse_header_mutation(common_response);
  return ParseHeaderMutation(header_mutation);
}

absl::StatusOr<ExtProcResponse::BodyMutation> ParseBodyMutation(
    const envoy_service_ext_proc_v3_CommonResponse* common_response) {
  if (common_response == nullptr) {
    return absl::InternalError("common_response is not set");
  }
  int32_t status =
      envoy_service_ext_proc_v3_CommonResponse_status(common_response);
  if (status == envoy_service_ext_proc_v3_CommonResponse_CONTINUE_AND_REPLACE) {
    return absl::InternalError("CONTINUE_AND_REPLACE is not supported");
  }
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
      auto mutation = ParseHeaders(common_response);
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
      auto mutation = ParseHeaders(common_response);
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
      auto mutation = ParseHeaderMutation(header_mutation);
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
      auto mutation = ParseBodyMutation(common_response);
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
      auto mutation = ParseBodyMutation(common_response);
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
      auto header_mutation = ParseHeaderMutation(
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
  bool ShouldForwardHeader(absl::string_view key) const {
    auto header_in_matcher = [](absl::string_view key,
                                const std::vector<StringMatcher>& matchers) {
      for (const auto& matcher : matchers) {
        if (matcher.Match(key)) return true;
      }
      return false;
    };
    if (disallowed_headers_.empty()) {
      return allowed_headers_.empty() ||
             header_in_matcher(key, allowed_headers_);
    }
    // Now disallow list is set.
    if (header_in_matcher(key, disallowed_headers_)) {
      return false;
    }
    if (allowed_headers_.empty() || header_in_matcher(key, allowed_headers_)) {
      return true;
    }
    return false;
  }

  void Append(absl::string_view key, absl::string_view value) {
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

void SetRequestHeaders(upb_Arena* arena,
                       envoy_config_core_v3_HeaderMap* headers,
                       envoy_service_ext_proc_v3_ProcessingRequest* request) {
  auto http_headers = envoy_service_ext_proc_v3_HttpHeaders_new(arena);
  envoy_service_ext_proc_v3_HttpHeaders_set_headers(http_headers, headers);
  envoy_service_ext_proc_v3_HttpHeaders_set_end_of_stream(http_headers, false);
  envoy_service_ext_proc_v3_ProcessingRequest_set_request_headers(request,
                                                                  http_headers);
}

void SetResponseHeaders(upb_Arena* arena,
                        envoy_config_core_v3_HeaderMap* headers,
                        bool end_of_stream,
                        envoy_service_ext_proc_v3_ProcessingRequest* request) {
  auto http_headers = envoy_service_ext_proc_v3_HttpHeaders_new(arena);
  envoy_service_ext_proc_v3_HttpHeaders_set_headers(http_headers, headers);
  envoy_service_ext_proc_v3_HttpHeaders_set_end_of_stream(http_headers,
                                                          end_of_stream);
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_headers(
      request, http_headers);
}

void SetRequestBody(upb_Arena* arena, upb_StringView buf, bool end_of_stream,
                    bool end_of_stream_without_message,
                    envoy_service_ext_proc_v3_ProcessingRequest* request) {
  envoy_service_ext_proc_v3_HttpBody* body =
      envoy_service_ext_proc_v3_HttpBody_new(arena);
  envoy_service_ext_proc_v3_HttpBody_set_body(body, buf);
  envoy_service_ext_proc_v3_HttpBody_set_end_of_stream(body, end_of_stream);
  envoy_service_ext_proc_v3_HttpBody_set_end_of_stream_without_message(body,
                                                                       true);
  envoy_service_ext_proc_v3_ProcessingRequest_set_request_body(request, body);
}

void SetResponseBody(upb_Arena* arena, upb_StringView buf,
                     envoy_service_ext_proc_v3_ProcessingRequest* request) {
  envoy_service_ext_proc_v3_HttpBody* body =
      envoy_service_ext_proc_v3_HttpBody_new(arena);
  envoy_service_ext_proc_v3_HttpBody_set_body(body, buf);
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_body(request, body);
}

void SetResponseTrailers(upb_Arena* arena,
                         envoy_config_core_v3_HeaderMap* trailer,
                         envoy_service_ext_proc_v3_ProcessingRequest* request) {
  auto http_trailers = envoy_service_ext_proc_v3_HttpTrailers_new(arena);
  envoy_service_ext_proc_v3_HttpTrailers_set_trailers(http_trailers, trailer);
  envoy_service_ext_proc_v3_ProcessingRequest_set_response_trailers(
      request, http_trailers);
}

void SetAttributes(upb_Arena* arena, ::google_protobuf_Struct* attributes,
                   envoy_service_ext_proc_v3_ProcessingRequest* request) {
  if (attributes == nullptr) return;
  constexpr absl::string_view kAttributeKey = "envoy.filters.http.ext_proc";
  envoy_service_ext_proc_v3_ProcessingRequest_attributes_set(
      request,
      upb_StringView_FromDataAndSize(kAttributeKey.data(),
                                     kAttributeKey.size()),
      attributes, arena);
}

void SetProtocolConfig(upb_Arena* arena,
                       const ExtProcProcessingMode& processing_mode,
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

absl::StatusOr<std::string> SerializeMessage(
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
    bool observability_mode, bool is_first_message,
    const ExtProcProcessingMode& processing_mode) {
  auto* request = envoy_service_ext_proc_v3_ProcessingRequest_new(arena);
  SetAttributes(arena, attributes, request);
  envoy_service_ext_proc_v3_ProcessingRequest_set_observability_mode(
      request, observability_mode);
  if (is_first_message) {
    SetProtocolConfig(arena, processing_mode, request);
  }
  return request;
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
// CreateAttributesStructProto()
//

// TODO(rishesh): Support CEL attributes from A103 (except
// xds.cluster_metadata.filter_metadata) when adding support for ext_proc on
// the server side. See
// https://github.com/grpc/proposal/blob/master/A103-xds-composite-filter.md#cel-attributes
::google_protobuf_Struct* CreateAttributesStructProto(
    upb_Arena* arena, const std::vector<std::string>& attributes,
    const grpc_metadata_batch& metadata) {
  if (attributes.empty()) return nullptr;
  ::google_protobuf_Struct* struct_msg = google_protobuf_Struct_new(arena);
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
          google_protobuf_Struct_new(arena);
      UpbStructHeadersEncoder encoder(headers_struct, arena);
      metadata.Encode(&encoder);
      ::google_protobuf_Value* val_msg = ::google_protobuf_Value_new(arena);
      ::google_protobuf_Value_set_struct_value(val_msg, headers_struct);
      ::google_protobuf_Struct_fields_set(
          struct_msg, upb_StringView_FromDataAndSize(attr.data(), attr.size()),
          val_msg, arena);
    } else if (attr == "request.referer") {
      std::string ref_str;
      auto val = metadata.GetStringValue("referer", &ref_str);
      if (val.has_value()) add_field(attr, *val);
    } else if (attr == "request.useragent") {
      std::string ua_str;
      auto val = metadata.GetStringValue("user-agent", &ua_str);
      if (val.has_value()) add_field(attr, *val);
    } else if (attr == "request.id") {
      std::string id_str;
      auto val = metadata.GetStringValue("x-request-id", &id_str);
      if (val.has_value()) add_field(attr, *val);
    } else if (attr == "request.query") {
      add_field(attr, "");
    }
  }
  return struct_msg;
}

absl::StatusOr<std::string> CreateClientHeadersRequest(
    upb_Arena* arena, grpc_metadata_batch* metadata,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    bool is_first_message, const ExtProcProcessingMode& processing_mode) {
  auto* request = CreateCommonRequest(arena, attributes, observability_mode,
                                      is_first_message, processing_mode);
  auto* upb_headers = envoy_config_core_v3_HeaderMap_new(arena);
  PopulateMetadataBatchToHeaderMap(*metadata, allowed_headers,
                                   disallowed_headers, arena, upb_headers);
  SetRequestHeaders(arena, upb_headers, request);
  return SerializeMessage(request, arena);
}

absl::StatusOr<std::string> CreateServerHeadersRequest(
    upb_Arena* arena, grpc_metadata_batch* metadata,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    bool is_first_message, const ExtProcProcessingMode& processing_mode,
    bool end_of_stream) {
  auto* request = CreateCommonRequest(arena, attributes, observability_mode,
                                      is_first_message, processing_mode);
  auto* upb_headers = envoy_config_core_v3_HeaderMap_new(arena);
  PopulateMetadataBatchToHeaderMap(*metadata, allowed_headers,
                                   disallowed_headers, arena, upb_headers);
  SetResponseHeaders(arena, upb_headers, end_of_stream, request);
  return SerializeMessage(request, arena);
}

absl::StatusOr<std::string> CreateClientBodyRequest(
    upb_Arena* arena, absl::string_view body,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    bool is_first_message, const ExtProcProcessingMode& processing_mode,
    bool end_of_stream, bool end_of_stream_without_message) {
  auto* request = CreateCommonRequest(arena, attributes, observability_mode,
                                      is_first_message, processing_mode);
  SetRequestBody(arena,
                 upb_StringView_FromDataAndSize(body.data(), body.size()),
                 end_of_stream, end_of_stream_without_message, request);
  return SerializeMessage(request, arena);
}

absl::StatusOr<std::string> CreateServerBodyRequest(
    upb_Arena* arena, absl::string_view body,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    bool is_first_message, const ExtProcProcessingMode& processing_mode) {
  auto* request = CreateCommonRequest(arena, attributes, observability_mode,
                                      is_first_message, processing_mode);
  SetResponseBody(
      arena, upb_StringView_FromDataAndSize(body.data(), body.size()), request);
  return SerializeMessage(request, arena);
}

absl::StatusOr<std::string> CreateServerTrailersRequest(
    upb_Arena* arena, grpc_metadata_batch* trailers,
    const std::vector<StringMatcher>& allowed_headers,
    const std::vector<StringMatcher>& disallowed_headers,
    ::google_protobuf_Struct* attributes, bool observability_mode,
    bool is_first_message, const ExtProcProcessingMode& processing_mode) {
  auto* request = CreateCommonRequest(arena, attributes, observability_mode,
                                      is_first_message, processing_mode);
  auto* upb_trailers = envoy_config_core_v3_HeaderMap_new(arena);
  PopulateMetadataBatchToHeaderMap(*trailers, allowed_headers,
                                   disallowed_headers, arena, upb_trailers);
  SetResponseTrailers(arena, upb_trailers, request);
  return SerializeMessage(request, arena);
}

}  // namespace grpc_core
