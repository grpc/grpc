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

#include "src/core/ext/filters/ext_authz/ext_authz_messages.h"

#include <grpc/status.h>
#include <grpc/support/time.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/service/auth/v3/attribute_context.upb.h"
#include "envoy/service/auth/v3/external_auth.upb.h"
#include "envoy/type/v3/http_status.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "google/rpc/status.upb.h"
#include "src/core/call/status_util.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/util/host_port.h"
#include "src/core/util/matchers.h"
#include "src/core/util/time.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"
#include "upb/mem/arena.hpp"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {

bool IsHeaderAllowed(absl::string_view key,
                     const std::vector<StringMatcher>& allowed_headers,
                     const std::vector<StringMatcher>& disallowed_headers) {
  for (const auto& disallowed : disallowed_headers) {
    if (disallowed.Match(key)) {
      return false;
    }
  }
  if (allowed_headers.empty()) {
    return true;
  }
  for (const auto& allowed : allowed_headers) {
    if (allowed.Match(key)) {
      return true;
    }
  }
  return false;
}

std::string GetPrincipal(const ExtAuthzRequest::Peer& peer) {
  for (const auto& uri : peer.uri_sans) {
    if (!uri.empty()) return uri;
  }
  for (const auto& dns : peer.dns_sans) {
    if (!dns.empty()) return dns;
  }
  if (!peer.subject.empty()) {
    return peer.subject;
  }
  return "";
}

envoy_config_core_v3_Address* CreateAddress(upb_Arena* arena,
                                            const ExtAuthzRequest::Peer& peer) {
  if (!peer.address.has_value()) {
    return nullptr;
  }
  const grpc_resolved_address& resolved_addr = *peer.address;
  const char* scheme = grpc_sockaddr_get_uri_scheme(&resolved_addr);
  if (scheme == nullptr) return nullptr;
  auto* address = envoy_config_core_v3_Address_new(arena);
  if (strcmp(scheme, "unix") == 0) {
    auto path = grpc_sockaddr_to_string(&resolved_addr, false /* normalize */);
    if (!path.ok()) return nullptr;
    auto* pipe = envoy_config_core_v3_Pipe_new(arena);
    envoy_config_core_v3_Pipe_set_path(pipe,
                                       CopyStdStringToUpbString(*path, arena));
    envoy_config_core_v3_Address_set_pipe(address, pipe);
    return address;
  }
  if (strcmp(scheme, "ipv4") == 0 || strcmp(scheme, "ipv6") == 0) {
    auto host_port =
        grpc_sockaddr_to_string(&resolved_addr, false /* normalize */);
    if (!host_port.ok()) return nullptr;
    std::string host;
    std::string port_str;
    if (!SplitHostPort(*host_port, &host, &port_str)) return nullptr;
    int port = grpc_sockaddr_get_port(&resolved_addr);
    auto* socket_address = envoy_config_core_v3_SocketAddress_new(arena);
    envoy_config_core_v3_SocketAddress_set_protocol(
        socket_address, envoy_config_core_v3_SocketAddress_TCP);
    envoy_config_core_v3_SocketAddress_set_address(
        socket_address, CopyStdStringToUpbString(host, arena));
    envoy_config_core_v3_SocketAddress_set_port_value(socket_address, port);
    envoy_config_core_v3_Address_set_socket_address(address, socket_address);
    return address;
  }
  return nullptr;
}

envoy_service_auth_v3_AttributeContext_Peer* CreateSource(
    upb_Arena* arena, const ExtAuthzRequest& request) {
  auto* source = envoy_service_auth_v3_AttributeContext_Peer_new(arena);
  auto* address = CreateAddress(arena, request.source);
  if (address != nullptr) {
    envoy_service_auth_v3_AttributeContext_Peer_set_address(source, address);
  }
  std::string principal = GetPrincipal(request.source);
  if (!principal.empty()) {
    envoy_service_auth_v3_AttributeContext_Peer_set_principal(
        source, CopyStdStringToUpbString(principal, arena));
  }
  if (request.include_peer_certificate && !request.source.certificate.empty()) {
    envoy_service_auth_v3_AttributeContext_Peer_set_certificate(
        source, CopyStdStringToUpbString(request.source.certificate, arena));
  }
  return source;
}

envoy_service_auth_v3_AttributeContext_Peer* CreateDestination(
    upb_Arena* arena, const ExtAuthzRequest& request) {
  auto* destination = envoy_service_auth_v3_AttributeContext_Peer_new(arena);
  auto* address = CreateAddress(arena, request.destination);
  if (address != nullptr) {
    envoy_service_auth_v3_AttributeContext_Peer_set_address(destination,
                                                            address);
  }
  std::string principal = GetPrincipal(request.destination);
  if (!principal.empty()) {
    envoy_service_auth_v3_AttributeContext_Peer_set_principal(
        destination, CopyStdStringToUpbString(principal, arena));
  }
  return destination;
}

envoy_service_auth_v3_AttributeContext_Request* CreateRequest(
    upb_Arena* arena, const ExtAuthzRequest& request) {
  auto* envoy_request =
      envoy_service_auth_v3_AttributeContext_Request_new(arena);
  auto* timestamp = envoy_service_auth_v3_AttributeContext_Request_mutable_time(
      envoy_request, arena);
  if (request.start_time.has_value()) {
    gpr_timespec ts = request.start_time->as_timespec(GPR_CLOCK_REALTIME);
    TimestampToUpb(ts, timestamp);
  } else {
    TimestampToUpb(gpr_now(GPR_CLOCK_REALTIME), timestamp);
  }
  auto* http_request =
      envoy_service_auth_v3_AttributeContext_Request_mutable_http(envoy_request,
                                                                  arena);
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_method(
      http_request, CopyStdStringToUpbString("POST", arena));
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_path(
      http_request, CopyStdStringToUpbString(request.path, arena));
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_size(http_request, -1);
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_protocol(
      http_request, CopyStdStringToUpbString("HTTP/2", arena));
  auto* header_map = envoy_config_core_v3_HeaderMap_new(arena);
  for (const auto& [key, value] : request.headers) {
    if (IsHeaderAllowed(key, request.allowed_headers,
                        request.disallowed_headers)) {
      auto* header =
          envoy_config_core_v3_HeaderMap_add_headers(header_map, arena);
      envoy_config_core_v3_HeaderValue_set_key(
          header, CopyStdStringToUpbString(key, arena));
      if (absl::EndsWith(key, "-bin")) {
        envoy_config_core_v3_HeaderValue_set_raw_value(
            header, CopyStdStringToUpbString(value, arena));
      } else {
        envoy_config_core_v3_HeaderValue_set_value(
            header, CopyStdStringToUpbString(value, arena));
      }
    }
  }
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_header_map(
      http_request, header_map);
  return envoy_request;
}

envoy_service_auth_v3_AttributeContext* CreateAttributeContext(
    upb_Arena* arena, const ExtAuthzRequest& request) {
  auto* attribute_context = envoy_service_auth_v3_AttributeContext_new(arena);
  if (!request.is_client_call) {
    envoy_service_auth_v3_AttributeContext_set_source(
        attribute_context, CreateSource(arena, request));
    envoy_service_auth_v3_AttributeContext_set_destination(
        attribute_context, CreateDestination(arena, request));
  }
  envoy_service_auth_v3_AttributeContext_set_request(
      attribute_context, CreateRequest(arena, request));
  return attribute_context;
}

}  // namespace

std::string CreateExtAuthzRequest(const ExtAuthzRequest& request) {
  upb::Arena arena;
  auto* check_request = envoy_service_auth_v3_CheckRequest_new(arena.ptr());
  auto* attribute_context = CreateAttributeContext(arena.ptr(), request);
  envoy_service_auth_v3_CheckRequest_set_attributes(check_request,
                                                    attribute_context);
  size_t output_length = 0;
  char* output = envoy_service_auth_v3_CheckRequest_serialize(
      check_request, arena.ptr(), &output_length);
  return (output != nullptr && output_length > 0)
             ? std::string(output, output_length)
             : std::string();
}

namespace {

absl::StatusOr<std::vector<XdsHeaderValueOption>> ParseExtAuthzHeaderOptions(
    const envoy_config_core_v3_HeaderValueOption* const* headers, size_t size) {
  std::vector<XdsHeaderValueOption> result;
  result.reserve(size);
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors errors;
    auto parsed = ParseXdsHeaderValueOption(headers[i], &errors);
    if (!errors.ok()) {
      return errors.status(absl::StatusCode::kInternal,
                           "Failed to parse XdsHeaderValueOption");
    }
    result.push_back(std::move(parsed));
  }
  return result;
}

}  // namespace

absl::StatusOr<ExtAuthzResponse> ExtAuthzResponse::Parse(
    absl::string_view serialized_response) {
  upb::Arena arena;
  const auto* response = envoy_service_auth_v3_CheckResponse_parse(
      serialized_response.data(), serialized_response.size(), arena.ptr());
  if (response == nullptr) {
    return absl::InternalError("Failed to parse CheckResponse");
  }
  ExtAuthzResponse ext_authz_response;
  bool has_status = envoy_service_auth_v3_CheckResponse_has_status(response);
  bool has_denied =
      envoy_service_auth_v3_CheckResponse_has_denied_response(response);
  if (has_status) {
    const auto* status = envoy_service_auth_v3_CheckResponse_status(response);
    int32_t code_int = google_rpc_Status_code(status);
    if (!grpc_status_code_from_int(code_int, &ext_authz_response.status_code)) {
      return absl::InternalError(absl::StrCat(
          "Invalid grpc status code in CheckResponse status: ", code_int));
    }
    ext_authz_response.status_message =
        UpbStringToStdString(google_rpc_Status_message(status));
  } else {
    if (has_denied) {
      ext_authz_response.status_code = GRPC_STATUS_PERMISSION_DENIED;
    } else {
      ext_authz_response.status_code = GRPC_STATUS_OK;
    }
  }
  if (ext_authz_response.status_code == GRPC_STATUS_OK) {
    ExtAuthzResponse::OkResponse ok_response;
    const auto* ok_resp =
        envoy_service_auth_v3_CheckResponse_ok_response(response);
    if (ok_resp != nullptr) {
      size_t size = 0;
      const auto* const* headers =
          envoy_service_auth_v3_OkHttpResponse_headers(ok_resp, &size);
      auto parsed_headers = ParseExtAuthzHeaderOptions(headers, size);
      if (!parsed_headers.ok()) return parsed_headers.status();
      ok_response.header_mutation.set_headers = std::move(*parsed_headers);

      auto headers_remove =
          envoy_service_auth_v3_OkHttpResponse_headers_to_remove(ok_resp,
                                                                 &size);
      for (size_t i = 0; i < size; ++i) {
        ok_response.header_mutation.remove_headers.push_back(
            UpbStringToStdString(headers_remove[i]));
      }

      const auto* const* resp_headers =
          envoy_service_auth_v3_OkHttpResponse_response_headers_to_add(ok_resp,
                                                                       &size);
      auto parsed_resp_headers = ParseExtAuthzHeaderOptions(resp_headers, size);
      if (!parsed_resp_headers.ok()) return parsed_resp_headers.status();
      ok_response.response_headers_to_add = std::move(*parsed_resp_headers);
    }
    ext_authz_response.response = std::move(ok_response);
  } else {
    ExtAuthzResponse::DeniedResponse denied_response;
    denied_response.status = GRPC_STATUS_PERMISSION_DENIED;
    const auto* denied =
        envoy_service_auth_v3_CheckResponse_denied_response(response);
    if (denied != nullptr) {
      const auto* http_status =
          envoy_service_auth_v3_DeniedHttpResponse_status(denied);
      if (http_status != nullptr) {
        int code = envoy_type_v3_HttpStatus_code(http_status);
        if (code > 0) {
          denied_response.status = grpc_http2_status_to_grpc_status(code);
        }
      }
      size_t size = 0;
      const auto* const* headers =
          envoy_service_auth_v3_DeniedHttpResponse_headers(denied, &size);
      auto parsed_headers = ParseExtAuthzHeaderOptions(headers, size);
      if (!parsed_headers.ok()) return parsed_headers.status();
      denied_response.headers = std::move(*parsed_headers);
    }
    ext_authz_response.response = std::move(denied_response);
  }
  return ext_authz_response;
}

}  // namespace grpc_core
