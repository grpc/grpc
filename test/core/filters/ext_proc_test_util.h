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

#ifndef GRPC_TEST_CORE_FILTERS_EXT_PROC_TEST_UTIL_H
#define GRPC_TEST_CORE_FILTERS_EXT_PROC_TEST_UTIL_H

#include <grpc/status.h>
#include <stdint.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "gmock/gmock.h"
#include "absl/strings/string_view.h"

// Helpers shared by the ext_proc filter unit tests and end-to-end tests:
// builders for the ProcessingResponse messages sent by a fake ext_proc server,
// and gmock matchers for the ProcessingRequest messages sent by the filter.

namespace grpc_core {

//
// Constants
//

// The ext_proc service method the filter calls on its side stream.
inline constexpr char kExtProcMethod[] =
    "/envoy.service.ext_proc.v3.ExternalProcessor/Process";
// Name of the ext_proc filter instance in the xDS HttpConnectionManager.
inline constexpr absl::string_view kFilterInstanceName = "ext_proc_instance";
// Target of the data plane channel; also the "target" label on metrics.
inline constexpr char kServerUri[] = "dns:///server.example.com";
inline constexpr char kPath[] = "/package.Service/Method";

// Headers.
inline constexpr char kRequestHeadersMutatedHeaderKey[] =
    "x-extproc-request-headers-mutated";
inline constexpr char kResponseHeadersMutatedHeaderKey[] =
    "x-extproc-response-headers-mutated";
inline constexpr char kResponseTrailersMutatedHeaderKey[] =
    "x-extproc-response-trailers-mutated";
inline constexpr char kHeaderMutatedValue[] = "yes";
inline constexpr char kImmediateResponseHeaderKey[] =
    "x-extproc-immediate-response-added";
inline constexpr char kCustomHeaderKey[] = "custom-header-key";
inline constexpr char kCustomHeaderValue[] = "custom-header-value";

// Message bodies.
inline constexpr char kMessage1[] = "message1";
inline constexpr char kMessage2[] = "message2";
inline constexpr char kMessage1Mutated[] = "message1-mutated";
inline constexpr char kMessage1DoubleMutated[] = "message1-mutated-mutated";
inline constexpr char kMutatedSuffix[] = "-mutated";
inline constexpr char kRequestBodyMutatedSuffix[] = "-request-body-mutated";
inline constexpr char kResponseBodyMutatedSuffix[] = "-response-body-mutated";
inline constexpr bool kEndOfStream = true;
inline constexpr char kEmptyBody[] = "";

//
// ProcessingResponse builders
//

inline void PopulateHeaderMutation(
    ::envoy::service::ext_proc::v3::HeaderMutation* mutation,
    const std::vector<std::pair<std::string, std::string>>& set_headers,
    const std::vector<std::string>& remove_headers = {}) {
  for (const auto& [key, value] : set_headers) {
    auto* header = mutation->add_set_headers();
    header->mutable_header()->set_key(key);
    header->mutable_header()->set_value(value);
  }
  for (const auto& key : remove_headers) {
    mutation->add_remove_headers(key);
  }
}

inline void PopulateBodyMutation(
    ::envoy::service::ext_proc::v3::BodyMutation* mutation,
    absl::string_view body, bool end_of_stream,
    bool end_of_stream_without_message = false) {
  auto* streamed_response = mutation->mutable_streamed_response();
  streamed_response->set_body(std::string(body));
  streamed_response->set_end_of_stream(end_of_stream);
  streamed_response->set_end_of_stream_without_message(
      end_of_stream_without_message);
}

inline ::envoy::service::ext_proc::v3::ProcessingResponse
MakeRequestHeadersMutationResponse(
    const std::vector<std::pair<std::string, std::string>>& set_headers,
    const std::vector<std::string>& remove_headers = {}) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  PopulateHeaderMutation(response.mutable_request_headers()
                             ->mutable_response()
                             ->mutable_header_mutation(),
                         set_headers, remove_headers);
  return response;
}

inline ::envoy::service::ext_proc::v3::ProcessingResponse
MakeResponseHeadersMutationResponse(
    const std::vector<std::pair<std::string, std::string>>& set_headers,
    const std::vector<std::string>& remove_headers = {}) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  PopulateHeaderMutation(response.mutable_response_headers()
                             ->mutable_response()
                             ->mutable_header_mutation(),
                         set_headers, remove_headers);
  return response;
}

inline ::envoy::service::ext_proc::v3::ProcessingResponse
MakeResponseTrailersMutationResponse(
    const std::vector<std::pair<std::string, std::string>>& set_headers,
    const std::vector<std::string>& remove_headers = {}) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  PopulateHeaderMutation(
      response.mutable_response_trailers()->mutable_header_mutation(),
      set_headers, remove_headers);
  return response;
}

inline ::envoy::service::ext_proc::v3::ProcessingResponse
MakeRequestBodyMutationResponse(absl::string_view body,
                                bool end_of_stream = false) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  PopulateBodyMutation(response.mutable_request_body()
                           ->mutable_response()
                           ->mutable_body_mutation(),
                       body, end_of_stream);
  return response;
}

// The response to the request_body event the filter sends for the client's
// half-close: passes the half-close through without adding a message.
inline ::envoy::service::ext_proc::v3::ProcessingResponse
MakeRequestHalfCloseResponse() {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  PopulateBodyMutation(response.mutable_request_body()
                           ->mutable_response()
                           ->mutable_body_mutation(),
                       /*body=*/"", /*end_of_stream=*/true,
                       /*end_of_stream_without_message=*/true);
  return response;
}

inline ::envoy::service::ext_proc::v3::ProcessingResponse
MakeResponseBodyMutationResponse(absl::string_view body,
                                 bool end_of_stream = false) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  PopulateBodyMutation(response.mutable_response_body()
                           ->mutable_response()
                           ->mutable_body_mutation(),
                       body, end_of_stream);
  return response;
}

// Returns `response` modified to also ask the filter to drain request and/or
// response messages.
inline ::envoy::service::ext_proc::v3::ProcessingResponse SetRequestDrains(
    ::envoy::service::ext_proc::v3::ProcessingResponse response,
    bool drain_requests, bool drain_responses) {
  if (drain_requests) response.set_request_drain_requests(true);
  if (drain_responses) response.set_request_drain_responses(true);
  return response;
}

// Acknowledges the filter's request body drain_complete.
inline ::envoy::service::ext_proc::v3::ProcessingResponse
MakeRequestBodyDrainCompleteResponse() {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  response.mutable_request_body()
      ->mutable_response()
      ->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_drain_complete(true);
  return response;
}

// Acknowledges the filter's response body drain_complete.
inline ::envoy::service::ext_proc::v3::ProcessingResponse
MakeResponseBodyDrainCompleteResponse() {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  response.mutable_response_body()
      ->mutable_response()
      ->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_drain_complete(true);
  return response;
}

inline ::envoy::service::ext_proc::v3::ProcessingResponse MakeImmediateResponse(
    grpc_status_code code, absl::string_view details,
    const std::vector<std::pair<std::string, std::string>>& set_headers) {
  ::envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* immediate = response.mutable_immediate_response();
  immediate->mutable_grpc_status()->set_status(static_cast<uint32_t>(code));
  immediate->set_details(std::string(details));
  PopulateHeaderMutation(immediate->mutable_headers(), set_headers);
  return response;
}

// Flattens a HeaderMap from a ProcessingRequest.
inline std::multimap<std::string, std::string> HeaderMapToMultimap(
    const ::envoy::config::core::v3::HeaderMap& header_map) {
  std::multimap<std::string, std::string> map;
  for (const auto& header : header_map.headers()) {
    std::string val =
        !header.raw_value().empty() ? header.raw_value() : header.value();
    map.emplace(header.key(), std::move(val));
  }
  return map;
}

//
// Matchers
//

MATCHER_P(MatchesRequestHeaders, headers_matcher,
          "matches request_headers with given headers") {
  if (!arg.has_request_headers()) {
    *result_listener << "request does not have request_headers";
    return false;
  }
  return ::testing::ExplainMatchResult(
      headers_matcher, HeaderMapToMultimap(arg.request_headers().headers()),
      result_listener);
}

MATCHER_P2(MatchesRequestBody, body_matcher, end_of_stream,
           "matches request_body with given body and end_of_stream") {
  if (!arg.has_request_body()) {
    *result_listener << "request does not have request_body";
    return false;
  }
  const auto& request_body = arg.request_body();
  if (request_body.end_of_stream() != end_of_stream) {
    *result_listener << "expected end_of_stream " << end_of_stream
                     << " but got " << request_body.end_of_stream();
    return false;
  }
  return ::testing::ExplainMatchResult(body_matcher, request_body.body(),
                                       result_listener);
}

MATCHER(MatchesRequestBodyDrainComplete,
        "matches request_body with drain_complete = true") {
  if (!arg.has_request_body()) {
    *result_listener << "request does not have request_body";
    return false;
  }
  if (!arg.request_body().drain_complete()) {
    *result_listener << "expected drain_complete to be true";
    return false;
  }
  return true;
}

MATCHER_P2(MatchesResponseHeaders, headers_matcher, end_of_stream,
           "matches response_headers with given headers and end_of_stream") {
  if (!arg.has_response_headers()) {
    *result_listener << "request does not have response_headers";
    return false;
  }
  const auto& response_headers = arg.response_headers();
  if (response_headers.end_of_stream() != end_of_stream) {
    *result_listener << "expected end_of_stream " << end_of_stream
                     << " but got " << response_headers.end_of_stream();
    return false;
  }
  return ::testing::ExplainMatchResult(
      headers_matcher, HeaderMapToMultimap(response_headers.headers()),
      result_listener);
}

MATCHER_P(MatchesResponseHeaders, headers_matcher,
          "matches response_headers with given headers") {
  return ::testing::ExplainMatchResult(
      MatchesResponseHeaders(headers_matcher, /*end_of_stream=*/false), arg,
      result_listener);
}

MATCHER_P2(MatchesResponseBody, body_matcher, end_of_stream,
           "matches response_body with given body and end_of_stream") {
  if (!arg.has_response_body()) {
    *result_listener << "request does not have response_body";
    return false;
  }
  const auto& response_body = arg.response_body();
  if (response_body.end_of_stream() != end_of_stream) {
    *result_listener << "expected end_of_stream " << end_of_stream
                     << " but got " << response_body.end_of_stream();
    return false;
  }
  return ::testing::ExplainMatchResult(body_matcher, response_body.body(),
                                       result_listener);
}

MATCHER(MatchesResponseBodyDrainComplete,
        "matches response_body with drain_complete = true") {
  if (!arg.has_response_body()) {
    *result_listener << "request does not have response_body";
    return false;
  }
  if (!arg.response_body().drain_complete()) {
    *result_listener << "expected drain_complete to be true";
    return false;
  }
  return true;
}

MATCHER_P(MatchesResponseTrailers, trailers_matcher,
          "matches response_trailers with given trailers") {
  if (!arg.has_response_trailers()) {
    *result_listener << "request does not have response_trailers";
    return false;
  }
  return ::testing::ExplainMatchResult(
      trailers_matcher, HeaderMapToMultimap(arg.response_trailers().trailers()),
      result_listener);
}

// A pulled message (ClientToServerNextMessage or ServerToClientNextMessage)
// that carries a message whose payload matches `payload_matcher`.
MATCHER_P(IsMessage, payload_matcher, "is a message") {
  if (!arg.ok()) {
    *result_listener << "pull failed";
    return false;
  }
  if (!arg.has_value()) {
    *result_listener << "got end-of-stream";
    return false;
  }
  std::string payload = arg.value().payload()->JoinIntoString();
  *result_listener << "payload: \"" << payload << "\"";
  return ::testing::Matches(payload_matcher)(payload);
}

// A pulled message that is a clean end-of-stream (i.e. a half-close, or the
// server's trailing metadata).
MATCHER(IsEndOfStream, "is end-of-stream") {
  if (!arg.ok()) {
    *result_listener << "pull failed";
    return false;
  }
  if (arg.has_value()) {
    *result_listener << "got message with payload \""
                     << arg.value().payload()->JoinIntoString() << "\"";
    return false;
  }
  return true;
}

MATCHER(IsStatusOk, "") {
  if (!arg.ok()) {
    *result_listener << "code=" << arg.error_code()
                     << ", message=" << arg.error_message();
    return false;
  }
  return true;
}

MATCHER_P2(GrpcStatusIs, code, message_matcher, "") {
  *result_listener << "actual code=" << arg.error_code()
                   << ", actual message=\"" << arg.error_message() << "\"";
  return ::testing::ExplainMatchResult(code, arg.error_code(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(message_matcher, arg.error_message(),
                                       result_listener);
}

MATCHER_P(EchoRequestMessageIs, message_matcher,
          "matches serialized EchoRequest with given message") {
  grpc::testing::EchoRequest echo_req;
  if (!echo_req.ParseFromString(arg)) {
    *result_listener << "could not be parsed as EchoRequest";
    return false;
  }
  return ::testing::ExplainMatchResult(message_matcher, echo_req.message(),
                                       result_listener);
}

MATCHER_P(EchoResponseMessageIs, message_matcher,
          "matches serialized EchoResponse with given message") {
  grpc::testing::EchoResponse echo_resp;
  if (!echo_resp.ParseFromString(arg)) {
    *result_listener << "could not be parsed as EchoResponse";
    return false;
  }
  return ::testing::ExplainMatchResult(message_matcher, echo_resp.message(),
                                       result_listener);
}

MATCHER_P(MatchesEchoResponse, message_matcher,
          "matches EchoResponse with given message") {
  return ::testing::ExplainMatchResult(message_matcher, arg.message(),
                                       result_listener);
}

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_FILTERS_EXT_PROC_TEST_UTIL_H
