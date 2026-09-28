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

#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>
#include <grpc/support/time.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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
#include "src/core/call/evaluate_args.h"
#include "src/core/call/status_util.h"
#include "src/core/credentials/transport/tls/tls_utils.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/surface/validate_metadata.h"
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
#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {

namespace {

// TODO(rishesh): Move UpbHeaderMapEncoder to a common place shared with
// ext_proc.
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
    auto* header =
        envoy_config_core_v3_HeaderMap_add_headers(header_map_, arena_);
    envoy_config_core_v3_HeaderValue_set_key(
        header, CopyStdStringToUpbString(key, arena_));
    if (absl::EndsWith(key, "-bin")) {
      envoy_config_core_v3_HeaderValue_set_raw_value(
          header, CopyStdStringToUpbString(value, arena_));
    } else {
      envoy_config_core_v3_HeaderValue_set_value(
          header, CopyStdStringToUpbString(value, arena_));
    }
  }

  envoy_config_core_v3_HeaderMap* header_map_;
  upb_Arena* arena_;
  const std::vector<StringMatcher>& allowed_headers_;
  const std::vector<StringMatcher>& disallowed_headers_;
};

// Returns the value for AttributeContext.Peer.principal, given the identity
// of one endpoint's certificate: its first URI SAN if set, otherwise its
// first DNS SAN if set, otherwise its subject. Returns an empty string_view
// if none of those are available (e.g., TLS is not used or the endpoint
// presented no certificate). This is applied to the peer's certificate for
// AttributeContext.source and to this endpoint's own certificate for
// AttributeContext.destination.
//
// Lifetime: the returned view points into whatever the caller's
// string_views point into -- in practice the grpc_auth_context that
// EvaluateArgs::PerChannelArgs was constructed from (see
// GetAuthPropertyArray(), which points each view at a grpc_auth_property's
// value). In particular, when the caller passes a std::vector returned by
// value from GetUriSans()/GetDnsSans(), destroying that vector does not
// invalidate the returned view, because the views point at the auth context
// rather than at the vector's storage. The caller is responsible for ensuring
// that the auth context outlives the returned view, which PerChannelArgs
// already requires.
absl::string_view GetPrincipal(absl::Span<const absl::string_view> uri_sans,
                               absl::Span<const absl::string_view> dns_sans,
                               absl::string_view subject) {
  for (const absl::string_view uri : uri_sans) {
    if (!uri.empty()) return uri;
  }
  for (const absl::string_view dns : dns_sans) {
    if (!dns.empty()) return dns;
  }
  return subject;
}

// Converts \a resolved_addr into an envoy.config.core.v3.Address message.
// Returns nullptr if the address is not set or cannot be converted.
//
// Only the SocketAddress case is implemented, because that is the only thing
// EvaluateArgs can produce: ParseEndpointUri() in evaluate_args.cc fills in
// EvaluateArgs::PerChannelArgs::Address::address via
// StringToSockaddr(), which accepts only IPv4 and IPv6 host:port
// strings.  For any other endpoint -- a unix domain socket in particular --
// StringToSockaddr() fails and EvaluateArgs reports a zero-length sockaddr,
// so there is nothing to convert and we leave AttributeContext.Peer.address
// unset.  (Note that it is StringToSockaddr(), not SplitHostPort(), that
// rejects a socket path: SplitHostPort() succeeds on a colon-free path,
// treating the whole thing as a bare host name with no port.)  An
// envoy.config.core.v3.Pipe branch here would therefore be dead code.
// TODO(rishesh): Teach EvaluateArgs to expose unix domain socket addresses
// (i.e., make ParseEndpointUri() in evaluate_args.cc handle the "unix" and
// "unix-abstract" URI schemes), and then populate Address.pipe here for those
// connections.
envoy_config_core_v3_Address* CreateAddress(
    upb_Arena* arena, const grpc_resolved_address& resolved_addr) {
  if (resolved_addr.len == 0) return nullptr;
  const char* scheme = grpc_sockaddr_get_uri_scheme(&resolved_addr);
  if (scheme == nullptr) return nullptr;
  if (strcmp(scheme, "ipv4") != 0 && strcmp(scheme, "ipv6") != 0) {
    return nullptr;
  }
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
  auto* address = envoy_config_core_v3_Address_new(arena);
  envoy_config_core_v3_Address_set_socket_address(address, socket_address);
  return address;
}

envoy_service_auth_v3_AttributeContext_Peer* CreateSource(
    upb_Arena* arena, const ExtAuthzRequest& request) {
  const EvaluateArgs& args = *request.args;
  auto* source = envoy_service_auth_v3_AttributeContext_Peer_new(arena);
  const grpc_resolved_address peer_address = args.GetPeerAddress();
  auto* address = CreateAddress(arena, peer_address);
  if (address != nullptr) {
    envoy_service_auth_v3_AttributeContext_Peer_set_address(source, address);
  }
  const std::vector<absl::string_view> uri_sans = args.GetUriSans();
  const std::vector<absl::string_view> dns_sans = args.GetDnsSans();
  absl::string_view principal =
      GetPrincipal(uri_sans, dns_sans, args.GetSubject());
  if (!principal.empty()) {
    envoy_service_auth_v3_AttributeContext_Peer_set_principal(
        source, CopyStdStringToUpbString(principal, arena));
  }
  // The caller populates this only if the ext_authz config sets
  // include_peer_certificate.
  if (!request.peer_certificate.empty()) {
    envoy_service_auth_v3_AttributeContext_Peer_set_certificate(
        source, CopyStdStringToUpbString(request.peer_certificate, arena));
  }
  return source;
}

envoy_service_auth_v3_AttributeContext_Peer* CreateDestination(
    upb_Arena* arena, const ExtAuthzRequest& request) {
  const EvaluateArgs& args = *request.args;
  auto* destination = envoy_service_auth_v3_AttributeContext_Peer_new(arena);
  const grpc_resolved_address local_address = args.GetLocalAddress();
  auto* address = CreateAddress(arena, local_address);
  if (address != nullptr) {
    envoy_service_auth_v3_AttributeContext_Peer_set_address(destination,
                                                            address);
  }
  // destination.principal is the identity asserted by the certificate that
  // this server presented on this connection, derived with exactly the same
  // precedence rule as source.principal (which grpc-java and Envoy also
  // share).  Unlike the peer, the local endpoint contributes at most one URI
  // SAN and one DNS SAN, because carrying every local SAN would mean an
  // unbounded number of per-connection auth context properties.  It is left
  // unset when TLS is not in use or this server presented no certificate, in
  // which case all three values are empty.
  absl::string_view principal = GetPrincipal(
      {args.GetLocalUriSan()}, {args.GetLocalDnsSan()}, args.GetLocalSubject());
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
  // time
  auto* timestamp = envoy_service_auth_v3_AttributeContext_Request_mutable_time(
      envoy_request, arena);
  if (request.start_time.has_value()) {
    gpr_timespec ts = request.start_time->as_timespec(GPR_CLOCK_REALTIME);
    TimestampToUpb(ts, timestamp);
  } else {
    TimestampToUpb(gpr_now(GPR_CLOCK_REALTIME), timestamp);
  }
  // http_request
  auto* http_request =
      envoy_service_auth_v3_AttributeContext_Request_mutable_http(envoy_request,
                                                                  arena);
  // method
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_method(
      http_request, CopyStdStringToUpbString("POST", arena));
  // path
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_path(
      http_request, CopyStdStringToUpbString(request.path, arena));
  // size
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_size(http_request, -1);
  // protocol
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_protocol(
      http_request, CopyStdStringToUpbString("HTTP/2", arena));
  // header_map
  auto* header_map = envoy_config_core_v3_HeaderMap_new(arena);
  if (request.metadata != nullptr) {
    UpbHeaderMapEncoder encoder(header_map, arena, request.allowed_headers,
                                request.disallowed_headers);
    request.metadata->Encode(&encoder);
  }
  envoy_service_auth_v3_AttributeContext_HttpRequest_set_header_map(
      http_request, header_map);
  return envoy_request;
}

envoy_service_auth_v3_AttributeContext* CreateAttributeContext(
    upb_Arena* arena, const ExtAuthzRequest& request) {
  auto* attribute_context = envoy_service_auth_v3_AttributeContext_new(arena);
  if (!request.is_client_call && request.args != nullptr) {
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

//
// GetUrlEncodedPemPeerCertificate()
//

// Percent-encodes \a value exactly the way that Envoy's
// Http::Utility::PercentEncoding::urlEncode() does: every character other than
// ALPHA, DIGIT, '*', '-', '.' and '_' is encoded as %XX, using uppercase
// hexadecimal digits (see RFC 3986 section 2.1).  This ensures that ext_authz
// servers see the same value regardless of whether the client is gRPC or
// Envoy.
std::string UrlEncode(absl::string_view value) {
  static constexpr char kHexDigits[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(value.size());
  for (const char c : value) {
    const auto byte = static_cast<unsigned char>(c);
    if (absl::ascii_isalnum(byte) || c == '*' || c == '-' || c == '.' ||
        c == '_') {
      encoded.push_back(c);
    } else {
      encoded.push_back('%');
      encoded.push_back(kHexDigits[byte >> 4]);
      encoded.push_back(kHexDigits[byte & 0x0f]);
    }
  }
  return encoded;
}

// TODO(rishesh): Computing this in the ext_authz filter is sub-optimal,
// because we will wind up computing it once for each filter chain. We should
// eventually fix that by creating a common connection context object, and this
// should be storable as one of the elements of that context.
std::string GetUrlEncodedPemPeerCertificate(grpc_auth_context* auth_context) {
  if (auth_context == nullptr) return "";
  absl::string_view pem_cert =
      GetAuthPropertyValue(auth_context, GRPC_X509_PEM_CERT_PROPERTY_NAME);
  if (pem_cert.empty()) return "";
  // AttributeContext.Peer.certificate is documented as the peer certificate
  // "encoded in URL and PEM format".
  return UrlEncode(pem_cert);
}

//
// CreateExtAuthzRequest()
//

absl::StatusOr<std::string> CreateExtAuthzRequest(
    const ExtAuthzRequest& request) {
  upb::Arena arena;
  auto* check_request = envoy_service_auth_v3_CheckRequest_new(arena.ptr());
  auto* attribute_context = CreateAttributeContext(arena.ptr(), request);
  envoy_service_auth_v3_CheckRequest_set_attributes(check_request,
                                                    attribute_context);
  size_t output_length = 0;
  char* output = envoy_service_auth_v3_CheckRequest_serialize(
      check_request, arena.ptr(), &output_length);
  if (output == nullptr) {
    return absl::InternalError("Failed to serialize CheckRequest");
  }
  return std::string(output, output_length);
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
  // status
  const auto* status = envoy_service_auth_v3_CheckResponse_status(response);
  if (status == nullptr) {
    return absl::InternalError("status not present in CheckResponse");
  }
  ExtAuthzResponse ext_authz_response;
  int32_t code_int = google_rpc_Status_code(status);
  if (!grpc_status_code_from_int(code_int, &ext_authz_response.status_code)) {
    return absl::InternalError(absl::StrCat(
        "Invalid grpc status code in CheckResponse status: ", code_int));
  }
  ext_authz_response.status_message =
      UpbStringToStdString(google_rpc_Status_message(status));
  // ok_response
  if (ext_authz_response.status_code == GRPC_STATUS_OK) {
    const auto* ok_resp =
        envoy_service_auth_v3_CheckResponse_ok_response(response);
    if (ok_resp == nullptr) {
      return absl::InternalError("ok_response not present in CheckResponse");
    }
    ExtAuthzResponse::OkResponse ok_response;
    size_t size = 0;
    // headers
    auto headers = ParseExtAuthzHeaderOptions(
        envoy_service_auth_v3_OkHttpResponse_headers(ok_resp, &size), size);
    if (!headers.ok()) return headers.status();
    ok_response.header_mutation.set_headers = std::move(*headers);
    // headers_to_remove
    auto headers_to_remove =
        envoy_service_auth_v3_OkHttpResponse_headers_to_remove(ok_resp, &size);
    for (size_t i = 0; i < size; ++i) {
      absl::string_view key = UpbStringToAbsl(headers_to_remove[i]);
      if (ValidateHeaderKeyIsLegal(key) != ValidateMetadataResult::kOk) {
        return absl::InvalidArgumentError(
            absl::StrCat("Invalid header name to remove: ", key));
      }
      ok_response.header_mutation.remove_headers.push_back(std::string(key));
    }
    // response_headers_to_add
    auto response_headers_to_add = ParseExtAuthzHeaderOptions(
        envoy_service_auth_v3_OkHttpResponse_response_headers_to_add(ok_resp,
                                                                     &size),
        size);
    if (!response_headers_to_add.ok()) return response_headers_to_add.status();
    ok_response.response_headers_to_add = std::move(*response_headers_to_add);
    ext_authz_response.response = std::move(ok_response);
  } else {
    // denied_response
    const auto* denied =
        envoy_service_auth_v3_CheckResponse_denied_response(response);
    if (denied == nullptr) {
      return absl::InternalError(
          "denied_response not present in CheckResponse");
    }
    ExtAuthzResponse::DeniedResponse denied_response;
    denied_response.status = GRPC_STATUS_PERMISSION_DENIED;
    // status
    if (const auto* http_status =
            envoy_service_auth_v3_DeniedHttpResponse_status(denied);
        http_status != nullptr) {
      denied_response.status = grpc_http2_status_to_grpc_status(
          envoy_type_v3_HttpStatus_code(http_status));
    }
    // headers
    size_t size = 0;
    auto headers = ParseExtAuthzHeaderOptions(
        envoy_service_auth_v3_DeniedHttpResponse_headers(denied, &size), size);
    if (!headers.ok()) return headers.status();
    denied_response.headers = std::move(*headers);
    ext_authz_response.response = std::move(denied_response);
  }
  return ext_authz_response;
}

}  // namespace grpc_core
