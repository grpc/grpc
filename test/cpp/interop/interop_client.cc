//
//
// Copyright 2015-2016 gRPC authors.
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

#include "test/cpp/interop/interop_client.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/security/credentials.h>

#include <cinttypes>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "src/core/config/config_vars.h"
#include "src/core/config/core_configuration.h"
#include "src/core/util/crash.h"
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/core/test_util/histogram.h"
#include "test/cpp/interop/backend_metrics_lb_policy.h"
#include "test/cpp/interop/client_helper.h"

namespace grpc {
namespace testing {

namespace {
// The same value is defined by the Java client.
const std::vector<int> request_stream_sizes = {27182, 8, 1828, 45904};
const std::vector<int> response_stream_sizes = {31415, 9, 2653, 58979};
const int kNumResponseMessages = 2000;
const int kResponseMessageSize = 1030;
const int kReceiveDelayMilliSeconds = 20;
const int kLargeRequestSize = 271828;
const int kLargeResponseSize = 314159;

void NoopChecks(const InteropClientContextInspector& /*inspector*/,
                const SimpleRequest* /*request*/,
                const SimpleResponse* /*response*/) {}

void UnaryCompressionChecks(const InteropClientContextInspector& inspector,
                            const SimpleRequest* request,
                            const SimpleResponse* /*response*/) {
  const grpc_compression_algorithm received_compression =
      inspector.GetCallCompressionAlgorithm();
  if (request->response_compressed().value()) {
    if (received_compression == GRPC_COMPRESS_NONE) {
      // Requested some compression, got NONE. This is an error.
      grpc_core::Crash(
          "Failure: Requested compression but got uncompressed response "
          "from server.");
    }
    CHECK(inspector.WasCompressed());
  } else {
    // Didn't request compression -> make sure the response is uncompressed
    CHECK(!(inspector.WasCompressed()));
  }
}

std::optional<std::string> ValuesDiff(absl::string_view field, double expected,
                                      double actual) {
  if (expected != actual) {
    return absl::StrFormat("%s: expected: %f, actual: %f", field, expected,
                           actual);
  }
  return std::nullopt;
}

template <typename Map>
std::optional<std::string> MapsDiff(absl::string_view path, const Map& expected,
                                    const Map& actual) {
  auto result = ValuesDiff(absl::StrFormat("%s size", path), expected.size(),
                           actual.size());
  if (result.has_value()) {
    return result;
  }
  for (const auto& key_value : expected) {
    auto it = actual.find(key_value.first);
    if (it == actual.end()) {
      return absl::StrFormat("In field %s, key %s was not found", path,
                             key_value.first);
    }
    result = ValuesDiff(absl::StrFormat("%s/%s", path, key_value.first),
                        key_value.second, it->second);
    if (result.has_value()) {
      return result;
    }
  }
  return std::nullopt;
}

std::optional<std::string> OrcaLoadReportsDiff(const TestOrcaReport& expected,
                                               const TestOrcaReport& actual) {
  auto error = ValuesDiff("cpu_utilization", expected.cpu_utilization(),
                          actual.cpu_utilization());
  if (error.has_value()) {
    return error;
  }
  error = ValuesDiff("mem_utilization", expected.memory_utilization(),
                     actual.memory_utilization());
  if (error.has_value()) {
    return error;
  }
  error =
      MapsDiff("request_cost", expected.request_cost(), actual.request_cost());
  if (error.has_value()) {
    return error;
  }
  error = MapsDiff("utilization", expected.utilization(), actual.utilization());
  if (error.has_value()) {
    return error;
  }
  return std::nullopt;
}
}  // namespace

InteropClient::ServiceStub::ServiceStub(
    ChannelCreationFunc channel_creation_func, bool new_stub_every_call)
    : channel_creation_func_(std::move(channel_creation_func)),
      new_stub_every_call_(new_stub_every_call) {}

TestService::Stub* InteropClient::ServiceStub::Get() {
  if (new_stub_every_call_ || stub_ == nullptr) {
    if (channel_ == nullptr) {
      channel_ = channel_creation_func_();
    }
    stub_ = TestService::NewStub(channel_);
  }
  return stub_.get();
}

UnimplementedService::Stub*
InteropClient::ServiceStub::GetUnimplementedServiceStub() {
  if (unimplemented_service_stub_ == nullptr) {
    if (channel_ == nullptr) {
      channel_ = channel_creation_func_();
    }
    unimplemented_service_stub_ = UnimplementedService::NewStub(channel_);
  }
  return unimplemented_service_stub_.get();
}

void InteropClient::ServiceStub::ResetChannel() {
  channel_.reset();
  stub_.reset();
}

InteropClient::InteropClient(ChannelCreationFunc channel_creation_func,
                             bool new_stub_every_test_case,
                             bool do_not_abort_on_transient_failures)
    : serviceStub_(
          [channel_creation_func = std::move(channel_creation_func), this]() {
            return channel_creation_func(
                load_report_tracker_.GetChannelArguments());
          },
          new_stub_every_test_case),
      do_not_abort_on_transient_failures_(do_not_abort_on_transient_failures) {}

bool InteropClient::AssertStatusOk(const Status& s,
                                   const std::string& optional_debug_string) {
  if (s.ok()) {
    return true;
  }

  // Note: At this point, s.error_code is definitely not StatusCode::OK (we
  // already checked for s.ok() above). So, the following will call abort()
  // (unless s.error_code() corresponds to a transient failure and
  // 'do_not_abort_on_transient_failures' is true)
  return AssertStatusCode(s, StatusCode::OK, optional_debug_string);
}

bool InteropClient::AssertStatusCode(const Status& s, StatusCode expected_code,
                                     const std::string& optional_debug_string) {
  if (s.error_code() == expected_code) {
    return true;
  }

  LOG(ERROR) << "Error status code: " << s.error_code()
             << " (expected: " << expected_code
             << "), message: " << s.error_message()
             << ", debug string: " << optional_debug_string;

  // In case of transient transient/retryable failures (like a broken
  // connection) we may or may not abort (see TransientFailureOrAbort())
  if (s.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return TransientFailureOrAbort();
  }

  abort();
}

bool InteropClient::DoEmpty() {
  VLOG(2) << "Sending an empty rpc...";

  Empty request;
  Empty response;
  ClientContext context;

  Status s = serviceStub_.Get()->EmptyCall(&context, request, &response);

  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  VLOG(2) << "Empty rpc done.";
  return true;
}

bool InteropClient::PerformLargeUnary(SimpleRequest* request,
                                      SimpleResponse* response) {
  return PerformLargeUnary(request, response, NoopChecks);
}

bool InteropClient::PerformLargeUnary(SimpleRequest* request,
                                      SimpleResponse* response,
                                      const CheckerFn& custom_checks_fn) {
  ClientContext context;
  InteropClientContextInspector inspector(context);
  request->set_response_size(kLargeResponseSize);
  std::string payload(kLargeRequestSize, '\0');
  request->mutable_payload()->set_body(payload.c_str(), kLargeRequestSize);
  if (request->has_expect_compressed()) {
    if (request->expect_compressed().value()) {
      context.set_compression_algorithm(GRPC_COMPRESS_GZIP);
    } else {
      context.set_compression_algorithm(GRPC_COMPRESS_NONE);
    }
  }

  Status s = serviceStub_.Get()->UnaryCall(&context, *request, response);
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  custom_checks_fn(inspector, request, response);

  // Payload related checks.
  CHECK(response->payload().body() == std::string(kLargeResponseSize, '\0'));
  return true;
}

bool InteropClient::DoComputeEngineCreds(
    const std::string& default_service_account,
    const std::string& oauth_scope) {
  VLOG(2) << "Sending a large unary rpc with compute engine credentials ...";
  SimpleRequest request;
  SimpleResponse response;
  request.set_fill_username(true);
  request.set_fill_oauth_scope(true);

  if (!PerformLargeUnary(&request, &response)) {
    return false;
  }

  VLOG(2) << "Got username " << response.username();
  VLOG(2) << "Got oauth_scope " << response.oauth_scope();
  CHECK(!response.username().empty());
  CHECK(response.username() == default_service_account);
  CHECK(!response.oauth_scope().empty());
  const char* oauth_scope_str = response.oauth_scope().c_str();
  CHECK(absl::StrContains(oauth_scope, oauth_scope_str));
  VLOG(2) << "Large unary with compute engine creds done.";
  return true;
}

bool InteropClient::DoOauth2AuthToken(const std::string& username,
                                      const std::string& oauth_scope) {
  VLOG(2) << "Sending a unary rpc with raw oauth2 access token credentials ...";
  SimpleRequest request;
  SimpleResponse response;
  request.set_fill_username(true);
  request.set_fill_oauth_scope(true);

  ClientContext context;

  Status s = serviceStub_.Get()->UnaryCall(&context, request, &response);

  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  CHECK(!response.username().empty());
  CHECK(!response.oauth_scope().empty());
  CHECK(username == response.username());
  const char* oauth_scope_str = response.oauth_scope().c_str();
  CHECK(absl::StrContains(oauth_scope, oauth_scope_str));
  VLOG(2) << "Unary with oauth2 access token credentials done.";
  return true;
}

bool InteropClient::DoPerRpcCreds(const std::string& json_key) {
  VLOG(2) << "Sending a unary rpc with per-rpc JWT access token ...";
  SimpleRequest request;
  SimpleResponse response;
  request.set_fill_username(true);

  ClientContext context;
  std::chrono::seconds token_lifetime = std::chrono::hours(1);
  std::shared_ptr<CallCredentials> creds =
      ServiceAccountJWTAccessCredentials(json_key, token_lifetime.count());

  context.set_credentials(creds);

  Status s = serviceStub_.Get()->UnaryCall(&context, request, &response);

  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  CHECK(!response.username().empty());
  CHECK(json_key.find(response.username()) != std::string::npos);
  VLOG(2) << "Unary with per-rpc JWT access token done.";
  return true;
}

bool InteropClient::DoJwtTokenCreds(const std::string& username) {
  VLOG(2) << "Sending a large unary rpc with JWT token credentials ...";
  SimpleRequest request;
  SimpleResponse response;
  request.set_fill_username(true);

  if (!PerformLargeUnary(&request, &response)) {
    return false;
  }

  CHECK(!response.username().empty());
  CHECK(username.find(response.username()) != std::string::npos);
  VLOG(2) << "Large unary with JWT token creds done.";
  return true;
}

bool InteropClient::DoGoogleDefaultCredentials(
    const std::string& default_service_account) {
  VLOG(2) << "Sending a large unary rpc with GoogleDefaultCredentials...";
  SimpleRequest request;
  SimpleResponse response;
  request.set_fill_username(true);

  if (!PerformLargeUnary(&request, &response)) {
    return false;
  }

  VLOG(2) << "Got username " << response.username();
  CHECK(!response.username().empty());
  CHECK(response.username() == default_service_account);
  VLOG(2) << "Large unary rpc with GoogleDefaultCredentials done.";
  return true;
}

bool InteropClient::DoLargeUnary() {
  VLOG(2) << "Sending a large unary rpc...";
  SimpleRequest request;
  SimpleResponse response;
  if (!PerformLargeUnary(&request, &response)) {
    return false;
  }
  VLOG(2) << "Large unary done.";
  return true;
}

bool InteropClient::DoClientCompressedUnary() {
  // Probing for compression-checks support.
  ClientContext probe_context;
  SimpleRequest probe_req;
  SimpleResponse probe_res;

  probe_context.set_compression_algorithm(GRPC_COMPRESS_NONE);
  probe_req.mutable_expect_compressed()->set_value(true);  // lies!

  probe_req.set_response_size(kLargeResponseSize);
  probe_req.mutable_payload()->set_body(std::string(kLargeRequestSize, '\0'));

  VLOG(2) << "Sending probe for compressed unary request.";
  const Status s =
      serviceStub_.Get()->UnaryCall(&probe_context, probe_req, &probe_res);
  if (s.error_code() != grpc::StatusCode::INVALID_ARGUMENT) {
    // The server isn't able to evaluate incoming compression, making the rest
    // of this test moot.
    VLOG(2) << "Compressed unary request probe failed";
    return false;
  }
  VLOG(2) << "Compressed unary request probe succeeded. Proceeding.";

  const std::vector<bool> compressions = {true, false};
  for (size_t i = 0; i < compressions.size(); i++) {
    std::string log_suffix =
        absl::StrFormat("(compression=%s)", compressions[i] ? "true" : "false");

    VLOG(2) << "Sending compressed unary request " << log_suffix;
    SimpleRequest request;
    SimpleResponse response;
    request.mutable_expect_compressed()->set_value(compressions[i]);
    if (!PerformLargeUnary(&request, &response, UnaryCompressionChecks)) {
      LOG(ERROR) << "Compressed unary request failed " << log_suffix;
      return false;
    }

    VLOG(2) << "Compressed unary request failed " << log_suffix;
  }

  return true;
}

bool InteropClient::DoServerCompressedUnary() {
  const std::vector<bool> compressions = {true, false};
  for (size_t i = 0; i < compressions.size(); i++) {
    std::string log_suffix =
        absl::StrFormat("(compression=%s)", compressions[i] ? "true" : "false");

    VLOG(2) << "Sending unary request for compressed response " << log_suffix;
    SimpleRequest request;
    SimpleResponse response;
    request.mutable_response_compressed()->set_value(compressions[i]);

    if (!PerformLargeUnary(&request, &response, UnaryCompressionChecks)) {
      LOG(ERROR) << "Request for compressed unary failed " << log_suffix;
      return false;
    }

    VLOG(2) << "Request for compressed unary failed " << log_suffix;
  }

  return true;
}

// Either abort() (unless do_not_abort_on_transient_failures_ is true) or return
// false
bool InteropClient::TransientFailureOrAbort() {
  if (do_not_abort_on_transient_failures_) {
    return false;
  }

  abort();
}

bool InteropClient::DoRequestStreaming() {
  VLOG(2) << "Sending request steaming rpc ...";

  ClientContext context;
  StreamingInputCallRequest request;
  StreamingInputCallResponse response;

  std::unique_ptr<ClientWriter<StreamingInputCallRequest>> stream(
      serviceStub_.Get()->StreamingInputCall(&context, &response));

  int aggregated_payload_size = 0;
  for (size_t i = 0; i < request_stream_sizes.size(); ++i) {
    Payload* payload = request.mutable_payload();
    payload->set_body(std::string(request_stream_sizes[i], '\0'));
    if (!stream->Write(request)) {
      LOG(ERROR) << "DoRequestStreaming(): stream->Write() failed.";
      return TransientFailureOrAbort();
    }
    aggregated_payload_size += request_stream_sizes[i];
  }
  CHECK(stream->WritesDone());

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  CHECK(response.aggregated_payload_size() == aggregated_payload_size);
  return true;
}

bool InteropClient::DoResponseStreaming() {
  VLOG(2) << "Receiving response streaming rpc ...";

  ClientContext context;
  StreamingOutputCallRequest request;
  for (unsigned int i = 0; i < response_stream_sizes.size(); ++i) {
    ResponseParameters* response_parameter = request.add_response_parameters();
    response_parameter->set_size(response_stream_sizes[i]);
  }
  StreamingOutputCallResponse response;
  std::unique_ptr<ClientReader<StreamingOutputCallResponse>> stream(
      serviceStub_.Get()->StreamingOutputCall(&context, request));

  unsigned int i = 0;
  while (stream->Read(&response)) {
    CHECK(response.payload().body() ==
          std::string(response_stream_sizes[i], '\0'));
    ++i;
  }

  if (i < response_stream_sizes.size()) {
    // stream->Read() failed before reading all the expected messages. This is
    // most likely due to connection failure.
    LOG(ERROR) << "DoResponseStreaming(): Read fewer streams (" << i
               << ") than response_stream_sizes.size() ("
               << response_stream_sizes.size() << ")";
    return TransientFailureOrAbort();
  }

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  VLOG(2) << "Response streaming done.";
  return true;
}

bool InteropClient::DoClientCompressedStreaming() {
  // Probing for compression-checks support.
  ClientContext probe_context;
  StreamingInputCallRequest probe_req;
  StreamingInputCallResponse probe_res;

  probe_context.set_compression_algorithm(GRPC_COMPRESS_NONE);
  probe_req.mutable_expect_compressed()->set_value(true);  // lies!
  probe_req.mutable_payload()->set_body(std::string(27182, '\0'));

  VLOG(2) << "Sending probe for compressed streaming request.";

  std::unique_ptr<ClientWriter<StreamingInputCallRequest>> probe_stream(
      serviceStub_.Get()->StreamingInputCall(&probe_context, &probe_res));

  if (!probe_stream->Write(probe_req)) {
    LOG(ERROR) << __func__ << "(): stream->Write() failed";
    return TransientFailureOrAbort();
  }
  Status s = probe_stream->Finish();
  if (s.error_code() != grpc::StatusCode::INVALID_ARGUMENT) {
    // The server isn't able to evaluate incoming compression, making the rest
    // of this test moot.
    VLOG(2) << "Compressed streaming request probe failed";
    return false;
  }
  VLOG(2) << "Compressed streaming request probe succeeded. Proceeding.";

  ClientContext context;
  StreamingInputCallRequest request;
  StreamingInputCallResponse response;

  context.set_compression_algorithm(GRPC_COMPRESS_GZIP);
  std::unique_ptr<ClientWriter<StreamingInputCallRequest>> stream(
      serviceStub_.Get()->StreamingInputCall(&context, &response));

  request.mutable_payload()->set_body(std::string(27182, '\0'));
  request.mutable_expect_compressed()->set_value(true);
  VLOG(2) << "Sending streaming request with compression enabled";
  if (!stream->Write(request)) {
    LOG(ERROR) << __func__ << "(): stream->Write() failed.";
    return TransientFailureOrAbort();
  }

  WriteOptions wopts;
  wopts.set_no_compression();
  request.mutable_payload()->set_body(std::string(45904, '\0'));
  request.mutable_expect_compressed()->set_value(false);
  VLOG(2) << "Sending streaming request with compression disabled";
  if (!stream->Write(request, wopts)) {
    LOG(ERROR) << __func__ << "(): stream->Write() failed";
    return TransientFailureOrAbort();
  }
  CHECK(stream->WritesDone());

  s = stream->Finish();
  return AssertStatusOk(s, context.debug_error_string());
}

bool InteropClient::DoServerCompressedStreaming() {
  const std::vector<bool> compressions = {true, false};
  const std::vector<int> sizes = {31415, 92653};

  ClientContext context;
  InteropClientContextInspector inspector(context);
  StreamingOutputCallRequest request;

  CHECK(compressions.size() == sizes.size());
  for (size_t i = 0; i < sizes.size(); i++) {
    std::string log_suffix =
        absl::StrFormat("(compression=%s; size=%d)",
                        compressions[i] ? "true" : "false", sizes[i]);

    VLOG(2) << "Sending request streaming rpc " << log_suffix;

    ResponseParameters* const response_parameter =
        request.add_response_parameters();
    response_parameter->mutable_compressed()->set_value(compressions[i]);
    response_parameter->set_size(sizes[i]);
  }
  std::unique_ptr<ClientReader<StreamingOutputCallResponse>> stream(
      serviceStub_.Get()->StreamingOutputCall(&context, request));

  size_t k = 0;
  StreamingOutputCallResponse response;
  while (stream->Read(&response)) {
    // Payload size checks.
    CHECK(response.payload().body() ==
          std::string(request.response_parameters(k).size(), '\0'));

    // Compression checks.
    CHECK(request.response_parameters(k).has_compressed());
    if (request.response_parameters(k).compressed().value()) {
      CHECK(inspector.GetCallCompressionAlgorithm() > GRPC_COMPRESS_NONE);
      CHECK(inspector.WasCompressed());
    } else {
      // requested *no* compression.
      CHECK(!(inspector.WasCompressed()));
    }
    ++k;
  }

  if (k < sizes.size()) {
    // stream->Read() failed before reading all the expected messages. This
    // is most likely due to a connection failure.
    LOG(ERROR) << __func__ << "(): Responses read (k=" << k
               << ") is less than the expected number of  messages ("
               << sizes.size() << ").";
    return TransientFailureOrAbort();
  }

  Status s = stream->Finish();
  return AssertStatusOk(s, context.debug_error_string());
}

bool InteropClient::DoResponseStreamingWithSlowConsumer() {
  VLOG(2) << "Receiving response streaming rpc with slow consumer ...";

  ClientContext context;
  StreamingOutputCallRequest request;

  for (int i = 0; i < kNumResponseMessages; ++i) {
    ResponseParameters* response_parameter = request.add_response_parameters();
    response_parameter->set_size(kResponseMessageSize);
  }
  StreamingOutputCallResponse response;
  std::unique_ptr<ClientReader<StreamingOutputCallResponse>> stream(
      serviceStub_.Get()->StreamingOutputCall(&context, request));

  int i = 0;
  while (stream->Read(&response)) {
    CHECK(response.payload().body() == std::string(kResponseMessageSize, '\0'));
    VLOG(2) << "received message " << i;
    gpr_sleep_until(gpr_time_add(
        gpr_now(GPR_CLOCK_REALTIME),
        gpr_time_from_millis(kReceiveDelayMilliSeconds, GPR_TIMESPAN)));
    ++i;
  }

  if (i < kNumResponseMessages) {
    LOG(ERROR) << "DoResponseStreamingWithSlowConsumer(): Responses read (i="
               << i
               << ") is less than the expected messages (i.e "
                  "kNumResponseMessages = "
               << kNumResponseMessages << ").";

    return TransientFailureOrAbort();
  }

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  VLOG(2) << "Response streaming done.";
  return true;
}

bool InteropClient::DoHalfDuplex() {
  VLOG(2) << "Sending half-duplex streaming rpc ...";

  ClientContext context;
  std::unique_ptr<ClientReaderWriter<StreamingOutputCallRequest,
                                     StreamingOutputCallResponse>>
      stream(serviceStub_.Get()->HalfDuplexCall(&context));

  StreamingOutputCallRequest request;
  ResponseParameters* response_parameter = request.add_response_parameters();
  for (unsigned int i = 0; i < response_stream_sizes.size(); ++i) {
    response_parameter->set_size(response_stream_sizes[i]);

    if (!stream->Write(request)) {
      LOG(ERROR) << "DoHalfDuplex(): stream->Write() failed. i=" << i;
      return TransientFailureOrAbort();
    }
  }
  stream->WritesDone();

  unsigned int i = 0;
  StreamingOutputCallResponse response;
  while (stream->Read(&response)) {
    CHECK(response.payload().body() ==
          std::string(response_stream_sizes[i], '\0'));
    ++i;
  }

  if (i < response_stream_sizes.size()) {
    // stream->Read() failed before reading all the expected messages. This is
    // most likely due to a connection failure
    LOG(ERROR) << "DoHalfDuplex(): Responses read (i=" << i
               << ") are less than the expected number of messages "
                  "response_stream_sizes.size() ("
               << response_stream_sizes.size() << ").";
    return TransientFailureOrAbort();
  }

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  VLOG(2) << "Half-duplex streaming rpc done.";
  return true;
}

bool InteropClient::DoPingPong() {
  VLOG(2) << "Sending Ping Pong streaming rpc ...";

  ClientContext context;
  std::unique_ptr<ClientReaderWriter<StreamingOutputCallRequest,
                                     StreamingOutputCallResponse>>
      stream(serviceStub_.Get()->FullDuplexCall(&context));

  StreamingOutputCallRequest request;
  ResponseParameters* response_parameter = request.add_response_parameters();
  Payload* payload = request.mutable_payload();
  StreamingOutputCallResponse response;

  for (unsigned int i = 0; i < request_stream_sizes.size(); ++i) {
    response_parameter->set_size(response_stream_sizes[i]);
    payload->set_body(std::string(request_stream_sizes[i], '\0'));

    if (!stream->Write(request)) {
      LOG(ERROR) << "DoPingPong(): stream->Write() failed. i: " << i;
      return TransientFailureOrAbort();
    }

    if (!stream->Read(&response)) {
      LOG(ERROR) << "DoPingPong(): stream->Read() failed. i:" << i;
      return TransientFailureOrAbort();
    }

    CHECK(response.payload().body() ==
          std::string(response_stream_sizes[i], '\0'));
  }

  stream->WritesDone();

  CHECK(!stream->Read(&response));

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  VLOG(2) << "Ping pong streaming done.";
  return true;
}

bool InteropClient::DoCancelAfterBegin() {
  VLOG(2) << "Sending request streaming rpc ...";

  ClientContext context;
  StreamingInputCallRequest request;
  StreamingInputCallResponse response;

  std::unique_ptr<ClientWriter<StreamingInputCallRequest>> stream(
      serviceStub_.Get()->StreamingInputCall(&context, &response));

  VLOG(2) << "Trying to cancel...";
  context.TryCancel();
  Status s = stream->Finish();

  if (!AssertStatusCode(s, StatusCode::CANCELLED,
                        context.debug_error_string())) {
    return false;
  }

  VLOG(2) << "Canceling streaming done.";
  return true;
}

bool InteropClient::DoCancelAfterFirstResponse() {
  VLOG(2) << "Sending Ping Pong streaming rpc ...";

  ClientContext context;
  std::unique_ptr<ClientReaderWriter<StreamingOutputCallRequest,
                                     StreamingOutputCallResponse>>
      stream(serviceStub_.Get()->FullDuplexCall(&context));

  StreamingOutputCallRequest request;
  ResponseParameters* response_parameter = request.add_response_parameters();
  response_parameter->set_size(31415);
  request.mutable_payload()->set_body(std::string(27182, '\0'));
  StreamingOutputCallResponse response;

  if (!stream->Write(request)) {
    LOG(ERROR) << "DoCancelAfterFirstResponse(): stream->Write() failed";
    return TransientFailureOrAbort();
  }

  if (!stream->Read(&response)) {
    LOG(ERROR) << "DoCancelAfterFirstResponse(): stream->Read failed";
    return TransientFailureOrAbort();
  }
  CHECK(response.payload().body() == std::string(31415, '\0'));

  VLOG(2) << "Trying to cancel...";
  context.TryCancel();

  Status s = stream->Finish();
  VLOG(2) << "Canceling pingpong streaming done.";
  return true;
}

bool InteropClient::DoTimeoutOnSleepingServer() {
  VLOG(2) << "Sending Ping Pong streaming rpc with a short deadline...";

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::milliseconds(1);
  context.set_deadline(deadline);
  std::unique_ptr<ClientReaderWriter<StreamingOutputCallRequest,
                                     StreamingOutputCallResponse>>
      stream(serviceStub_.Get()->FullDuplexCall(&context));

  StreamingOutputCallRequest request;
  request.mutable_payload()->set_body(std::string(27182, '\0'));
  stream->Write(request);

  Status s = stream->Finish();
  if (!AssertStatusCode(s, StatusCode::DEADLINE_EXCEEDED,
                        context.debug_error_string())) {
    return false;
  }

  VLOG(2) << "Pingpong streaming timeout done.";
  return true;
}

bool InteropClient::DoEmptyStream() {
  VLOG(2) << "Starting empty_stream.";

  ClientContext context;
  std::unique_ptr<ClientReaderWriter<StreamingOutputCallRequest,
                                     StreamingOutputCallResponse>>
      stream(serviceStub_.Get()->FullDuplexCall(&context));
  stream->WritesDone();
  StreamingOutputCallResponse response;
  CHECK(stream->Read(&response) == false);

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  VLOG(2) << "empty_stream done.";
  return true;
}

bool InteropClient::DoStatusWithMessage() {
  VLOG(2) << "Sending RPC with a request for status code 2 and message";

  const grpc::StatusCode test_code = grpc::StatusCode::UNKNOWN;
  const std::string test_msg = "This is a test message";

  // Test UnaryCall.
  ClientContext context;
  SimpleRequest request;
  SimpleResponse response;
  EchoStatus* requested_status = request.mutable_response_status();
  requested_status->set_code(test_code);
  requested_status->set_message(test_msg);
  Status s = serviceStub_.Get()->UnaryCall(&context, request, &response);
  if (!AssertStatusCode(s, grpc::StatusCode::UNKNOWN,
                        context.debug_error_string())) {
    return false;
  }
  CHECK(s.error_message() == test_msg);

  // Test FullDuplexCall.
  ClientContext stream_context;
  std::shared_ptr<ClientReaderWriter<StreamingOutputCallRequest,
                                     StreamingOutputCallResponse>>
      stream(serviceStub_.Get()->FullDuplexCall(&stream_context));
  StreamingOutputCallRequest streaming_request;
  requested_status = streaming_request.mutable_response_status();
  requested_status->set_code(test_code);
  requested_status->set_message(test_msg);
  stream->Write(streaming_request);
  stream->WritesDone();
  StreamingOutputCallResponse streaming_response;
  while (stream->Read(&streaming_response)) {
  }
  s = stream->Finish();
  if (!AssertStatusCode(s, grpc::StatusCode::UNKNOWN,
                        context.debug_error_string())) {
    return false;
  }
  CHECK(s.error_message() == test_msg);

  VLOG(2) << "Done testing Status and Message";
  return true;
}

bool InteropClient::DoSpecialStatusMessage() {
  VLOG(2) << "Sending RPC with a request for status code 2 and message - "
             "\\t\\ntest "
             "with whitespace\\r\\nand Unicode BMP ☺ and non-BMP 😈\\t\\n";
  const grpc::StatusCode test_code = grpc::StatusCode::UNKNOWN;
  const std::string test_msg =
      "\t\ntest with whitespace\r\nand Unicode BMP ☺ and non-BMP 😈\t\n";
  ClientContext context;
  SimpleRequest request;
  SimpleResponse response;
  EchoStatus* requested_status = request.mutable_response_status();
  requested_status->set_code(test_code);
  requested_status->set_message(test_msg);
  Status s = serviceStub_.Get()->UnaryCall(&context, request, &response);
  if (!AssertStatusCode(s, grpc::StatusCode::UNKNOWN,
                        context.debug_error_string())) {
    return false;
  }
  CHECK(s.error_message() == test_msg);
  VLOG(2) << "Done testing Special Status Message";
  return true;
}

bool InteropClient::DoPickFirstUnary() {
  const int rpcCount = 100;
  SimpleRequest request;
  SimpleResponse response;
  std::string server_id;
  request.set_fill_server_id(true);
  for (int i = 0; i < rpcCount; i++) {
    ClientContext context;
    Status s = serviceStub_.Get()->UnaryCall(&context, request, &response);
    if (!AssertStatusOk(s, context.debug_error_string())) {
      return false;
    }
    if (i == 0) {
      server_id = response.server_id();
      continue;
    }
    if (response.server_id() != server_id) {
      LOG(ERROR) << "#" << i << " rpc hits server_id " << response.server_id()
                 << ", expect server_id " << server_id;
      return false;
    }
  }
  VLOG(2) << "pick first unary successfully finished";
  return true;
}

bool InteropClient::DoOrcaPerRpc() {
  load_report_tracker_.ResetCollectedLoadReports();
  grpc_core::CoreConfiguration::RegisterBuilder(RegisterBackendMetricsLbPolicy);
  VLOG(2) << "testing orca per rpc";
  SimpleRequest request;
  SimpleResponse response;
  ClientContext context;
  auto orca_report = request.mutable_orca_per_query_report();
  orca_report->set_cpu_utilization(0.8210);
  orca_report->set_memory_utilization(0.5847);
  orca_report->mutable_request_cost()->emplace("cost", 3456.32);
  orca_report->mutable_utilization()->emplace("util", 0.30499);
  auto status = serviceStub_.Get()->UnaryCall(&context, request, &response);
  if (!AssertStatusOk(status, context.debug_error_string())) {
    return false;
  }
  auto report = load_report_tracker_.GetNextLoadReport();
  CHECK(report.has_value());
  CHECK(report->has_value());
  auto comparison_result = OrcaLoadReportsDiff(report->value(), *orca_report);
  LOG_IF(FATAL, comparison_result.has_value()) << comparison_result->c_str();
  CHECK(!load_report_tracker_.GetNextLoadReport().has_value());
  VLOG(2) << "orca per rpc successfully finished";
  return true;
}

bool InteropClient::DoOrcaOob() {
  static constexpr auto kTimeout = absl::Seconds(10);
  LOG(INFO) << "testing orca oob";
  load_report_tracker_.ResetCollectedLoadReports();
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  grpc_core::ConfigVars::Overrides overrides;
  overrides.client_channel_backup_poll_interval_ms = 250;
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_core::CoreConfiguration::RegisterBuilder(RegisterBackendMetricsLbPolicy);
  ClientContext context;
  std::unique_ptr<ClientReaderWriter<StreamingOutputCallRequest,
                                     StreamingOutputCallResponse>>
      stream(serviceStub_.Get()->FullDuplexCall(&context));
  auto stream_cleanup = absl::MakeCleanup([&]() {
    CHECK(stream->WritesDone());
    CHECK(stream->Finish().ok());
  });
  {
    StreamingOutputCallRequest request;
    request.add_response_parameters()->set_size(1);
    TestOrcaReport* orca_report = request.mutable_orca_oob_report();
    orca_report->set_cpu_utilization(0.8210);
    orca_report->set_memory_utilization(0.5847);
    orca_report->mutable_utilization()->emplace("util", 0.30499);
    StreamingOutputCallResponse response;
    if (!stream->Write(request)) {
      LOG(ERROR) << "DoOrcaOob(): stream->Write() failed";
      return TransientFailureOrAbort();
    }
    if (!stream->Read(&response)) {
      LOG(ERROR) << "DoOrcaOob(): stream->Read failed";
      return TransientFailureOrAbort();
    }
    CHECK(load_report_tracker_
              .WaitForOobLoadReport(
                  [orca_report](const auto& actual) {
                    auto value = OrcaLoadReportsDiff(*orca_report, actual);
                    if (value.has_value()) {
                      VLOG(2) << "Reports mismatch: " << value->c_str();
                      return false;
                    }
                    return true;
                  },
                  kTimeout, 10)
              .has_value());
  }
  {
    StreamingOutputCallRequest request;
    request.add_response_parameters()->set_size(1);
    TestOrcaReport* orca_report = request.mutable_orca_oob_report();
    orca_report->set_cpu_utilization(0.29309);
    orca_report->set_memory_utilization(0.2);
    orca_report->mutable_utilization()->emplace("util", 0.2039);
    StreamingOutputCallResponse response;
    if (!stream->Write(request)) {
      LOG(ERROR) << "DoOrcaOob(): stream->Write() failed";
      return TransientFailureOrAbort();
    }
    if (!stream->Read(&response)) {
      LOG(ERROR) << "DoOrcaOob(): stream->Read failed";
      return TransientFailureOrAbort();
    }
    CHECK(
        load_report_tracker_
            .WaitForOobLoadReport(
                [orca_report](const auto& report) {
                  return !OrcaLoadReportsDiff(*orca_report, report).has_value();
                },
                kTimeout, 10)
            .has_value());
  }
  LOG(INFO) << "orca oob successfully finished";
  return true;
}

bool InteropClient::DoCustomMetadata() {
  const std::string kEchoInitialMetadataKey("x-grpc-test-echo-initial");
  const std::string kInitialMetadataValue("test_initial_metadata_value");
  const std::string kEchoTrailingBinMetadataKey(
      "x-grpc-test-echo-trailing-bin");
  const std::string kTrailingBinValue("\x0a\x0b\x0a\x0b\x0a\x0b");

  {
    VLOG(2) << "Sending RPC with custom metadata";
    ClientContext context;
    context.AddMetadata(kEchoInitialMetadataKey, kInitialMetadataValue);
    context.AddMetadata(kEchoTrailingBinMetadataKey, kTrailingBinValue);
    SimpleRequest request;
    SimpleResponse response;
    request.set_response_size(kLargeResponseSize);
    std::string payload(kLargeRequestSize, '\0');
    request.mutable_payload()->set_body(payload.c_str(), kLargeRequestSize);

    Status s = serviceStub_.Get()->UnaryCall(&context, request, &response);
    if (!AssertStatusOk(s, context.debug_error_string())) {
      return false;
    }

    const auto& server_initial_metadata = context.GetServerInitialMetadata();
    auto iter = server_initial_metadata.find(kEchoInitialMetadataKey);
    CHECK(iter != server_initial_metadata.end());
    CHECK(iter->second == kInitialMetadataValue);
    const auto& server_trailing_metadata = context.GetServerTrailingMetadata();
    iter = server_trailing_metadata.find(kEchoTrailingBinMetadataKey);
    CHECK(iter != server_trailing_metadata.end());
    CHECK(std::string(iter->second.begin(), iter->second.end()) ==
          kTrailingBinValue);

    VLOG(2) << "Done testing RPC with custom metadata";
  }

  {
    VLOG(2) << "Sending stream with custom metadata";
    ClientContext context;
    context.AddMetadata(kEchoInitialMetadataKey, kInitialMetadataValue);
    context.AddMetadata(kEchoTrailingBinMetadataKey, kTrailingBinValue);
    std::unique_ptr<ClientReaderWriter<StreamingOutputCallRequest,
                                       StreamingOutputCallResponse>>
        stream(serviceStub_.Get()->FullDuplexCall(&context));

    StreamingOutputCallRequest request;
    ResponseParameters* response_parameter = request.add_response_parameters();
    response_parameter->set_size(kLargeResponseSize);
    std::string payload(kLargeRequestSize, '\0');
    request.mutable_payload()->set_body(payload.c_str(), kLargeRequestSize);
    StreamingOutputCallResponse response;

    if (!stream->Write(request)) {
      LOG(ERROR) << "DoCustomMetadata(): stream->Write() failed";
      return TransientFailureOrAbort();
    }

    stream->WritesDone();

    if (!stream->Read(&response)) {
      LOG(ERROR) << "DoCustomMetadata(): stream->Read() failed";
      return TransientFailureOrAbort();
    }

    CHECK(response.payload().body() == std::string(kLargeResponseSize, '\0'));

    CHECK(!stream->Read(&response));

    Status s = stream->Finish();
    if (!AssertStatusOk(s, context.debug_error_string())) {
      return false;
    }

    const auto& server_initial_metadata = context.GetServerInitialMetadata();
    auto iter = server_initial_metadata.find(kEchoInitialMetadataKey);
    CHECK(iter != server_initial_metadata.end());
    CHECK(iter->second == kInitialMetadataValue);
    const auto& server_trailing_metadata = context.GetServerTrailingMetadata();
    iter = server_trailing_metadata.find(kEchoTrailingBinMetadataKey);
    CHECK(iter != server_trailing_metadata.end());
    CHECK(std::string(iter->second.begin(), iter->second.end()) ==
          kTrailingBinValue);

    VLOG(2) << "Done testing stream with custom metadata";
  }

  return true;
}

std::tuple<bool, int32_t, std::string, std::string>
InteropClient::PerformOneSoakTestIteration(
    const bool reset_channel,
    const int32_t max_acceptable_per_iteration_latency_ms,
    const int32_t request_size, const int32_t response_size) {
  gpr_timespec start = gpr_now(GPR_CLOCK_MONOTONIC);
  SimpleRequest request;
  SimpleResponse response;
  // Don't set the deadline on the RPC, and instead just
  // record how long the RPC took and compare. This makes
  // debugging easier when looking at failure results.
  ClientContext context;
  InteropClientContextInspector inspector(context);
  request.set_response_size(response_size);
  std::string payload(request_size, '\0');
  request.mutable_payload()->set_body(payload.c_str(), request_size);
  if (reset_channel) {
    serviceStub_.ResetChannel();
  }
  Status s = serviceStub_.Get()->UnaryCall(&context, request, &response);
  gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
  int32_t elapsed_ms = gpr_time_to_millis(gpr_time_sub(now, start));
  if (!s.ok()) {
    return std::tuple(false, elapsed_ms, context.debug_error_string(),
                      context.peer());
  } else if (elapsed_ms > max_acceptable_per_iteration_latency_ms) {
    std::string debug_string = absl::StrFormat(
        "%d ms exceeds max acceptable latency: %d ms, peer: %s", elapsed_ms,
        max_acceptable_per_iteration_latency_ms, context.peer());
    return std::tuple(false, elapsed_ms, std::move(debug_string),
                      context.peer());
  } else {
    return std::tuple(true, elapsed_ms, "", context.peer());
  }
}

void InteropClient::PerformSoakTest(
    const std::string& server_uri, const bool reset_channel_per_iteration,
    const int32_t soak_iterations, const int32_t max_failures,
    const int32_t max_acceptable_per_iteration_latency_ms,
    const int32_t min_time_ms_between_rpcs,
    const int32_t overall_timeout_seconds, const int32_t request_size,
    const int32_t response_size) {
  std::vector<std::tuple<bool, int32_t, std::string, std::string>> results;
  grpc_histogram* latencies_ms_histogram = grpc_histogram_create(
      1 /* resolution */,
      500 * 1e3 /* largest bucket; 500 seconds is unlikely */);
  gpr_timespec overall_deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(overall_timeout_seconds, GPR_TIMESPAN));
  int32_t iterations_ran = 0;
  int total_failures = 0;
  for (int i = 0;
       i < soak_iterations &&
       gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), overall_deadline) < 0;
       ++i) {
    gpr_timespec earliest_next_start = gpr_time_add(
        gpr_now(GPR_CLOCK_MONOTONIC),
        gpr_time_from_millis(min_time_ms_between_rpcs, GPR_TIMESPAN));
    auto result = PerformOneSoakTestIteration(
        reset_channel_per_iteration, max_acceptable_per_iteration_latency_ms,
        request_size, response_size);
    bool success = std::get<0>(result);
    int32_t elapsed_ms = std::get<1>(result);
    std::string debug_string = std::get<2>(result);
    std::string peer = std::get<3>(result);
    results.push_back(result);
    if (!success) {
      LOG(INFO) << "soak iteration: " << i << " elapsed_ms: " << elapsed_ms
                << " peer: " << peer << " server_uri: " << server_uri
                << " failed: " << debug_string;
      total_failures++;
    } else {
      LOG(INFO) << "soak iteration: " << i << " elapsed_ms: " << elapsed_ms
                << " peer: " << peer << " server_uri: " << server_uri
                << " succeeded";
    }
    grpc_histogram_add(latencies_ms_histogram, std::get<1>(result));
    iterations_ran++;
    gpr_sleep_until(earliest_next_start);
  }
  double latency_ms_median =
      grpc_histogram_percentile(latencies_ms_histogram, 50);
  double latency_ms_90th =
      grpc_histogram_percentile(latencies_ms_histogram, 90);
  double latency_ms_worst = grpc_histogram_maximum(latencies_ms_histogram);
  grpc_histogram_destroy(latencies_ms_histogram);
  if (iterations_ran < soak_iterations) {
    LOG(ERROR) << "(server_uri: " << server_uri << ") soak test consumed all "
               << overall_timeout_seconds
               << " seconds of time and quit early, only having ran "
               << iterations_ran << " out of desired " << soak_iterations
               << " iterations. total_failures: " << total_failures
               << ". max_failures_threshold: " << max_failures
               << ". median_soak_iteration_latency: " << latency_ms_median
               << " ms. 90th_soak_iteration_latency: " << latency_ms_90th
               << " ms. worst_soak_iteration_latency: " << latency_ms_worst
               << " ms. Some or all of the iterations that did run were "
                  "unexpectedly slow. See breakdown above for which iterations "
                  "succeeded, failed, and why for more info.";
    CHECK(0);
  } else if (total_failures > max_failures) {
    LOG(ERROR) << "(server_uri: " << server_uri
               << ") soak test ran: " << soak_iterations
               << " iterations. total_failures: " << total_failures
               << " exceeds max_failures_threshold: " << max_failures
               << ". median_soak_iteration_latency: " << latency_ms_median
               << " ms. 90th_soak_iteration_latency: " << latency_ms_90th
               << " ms. worst_soak_iteration_latency: " << latency_ms_worst
               << " ms. See breakdown above for which iterations succeeded, "
                  "failed, and why for more info.";
    CHECK(0);
  } else {
    LOG(INFO) << "(server_uri: " << server_uri
              << ") soak test ran: " << soak_iterations
              << " iterations. total_failures: " << total_failures
              << " is within max_failures_threshold: " << max_failures
              << ". median_soak_iteration_latency: " << latency_ms_median
              << " ms. 90th_soak_iteration_latency: " << latency_ms_90th
              << " ms. worst_soak_iteration_latency: " << latency_ms_worst
              << " ms. See breakdown above for which iterations succeeded, "
                 "failed, and why for more info.";
  }
}

bool InteropClient::DoRpcSoakTest(
    const std::string& server_uri, int32_t soak_iterations,
    int32_t max_failures, int64_t max_acceptable_per_iteration_latency_ms,
    int32_t soak_min_time_ms_between_rpcs, int32_t overall_timeout_seconds,
    int32_t request_size, int32_t response_size) {
  VLOG(2) << "Sending " << soak_iterations << " RPCs...";
  CHECK_GT(soak_iterations, 0);
  PerformSoakTest(server_uri, false /* reset channel per iteration */,
                  soak_iterations, max_failures,
                  max_acceptable_per_iteration_latency_ms,
                  soak_min_time_ms_between_rpcs, overall_timeout_seconds,
                  request_size, response_size);
  VLOG(2) << "rpc_soak test done.";
  return true;
}

bool InteropClient::DoChannelSoakTest(
    const std::string& server_uri, int32_t soak_iterations,
    int32_t max_failures, int64_t max_acceptable_per_iteration_latency_ms,
    int32_t soak_min_time_ms_between_rpcs, int32_t overall_timeout_seconds,
    int32_t request_size, int32_t response_size) {
  VLOG(2) << "Sending " << soak_iterations
          << " RPCs, tearing down the channel each time...";
  CHECK_GT(soak_iterations, 0);
  PerformSoakTest(server_uri, true /* reset channel per iteration */,
                  soak_iterations, max_failures,
                  max_acceptable_per_iteration_latency_ms,
                  soak_min_time_ms_between_rpcs, overall_timeout_seconds,
                  request_size, response_size);
  VLOG(2) << "channel_soak test done.";
  return true;
}

bool InteropClient::DoLongLivedChannelTest(int32_t soak_iterations,
                                           int32_t iteration_interval) {
  VLOG(2) << "Sending " << soak_iterations << " RPCs...";
  CHECK_GT(soak_iterations, 0);
  CHECK_GT(iteration_interval, 0);
  SimpleRequest request;
  SimpleResponse response;
  int num_failures = 0;
  for (int i = 0; i < soak_iterations; ++i) {
    VLOG(2) << "Sending RPC number " << i << "...";
    if (!PerformLargeUnary(&request, &response)) {
      LOG(ERROR) << "Iteration " << i << " failed.";
      num_failures++;
    }
    gpr_sleep_until(
        gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                     gpr_time_from_seconds(iteration_interval, GPR_TIMESPAN)));
  }
  if (num_failures == 0) {
    VLOG(2) << "long_lived_channel test done.";
    return true;
  } else {
    VLOG(2) << "long_lived_channel test failed with " << num_failures
            << " rpc failures.";
    return false;
  }
}

bool InteropClient::DoUnimplementedService() {
  VLOG(2) << "Sending a request for an unimplemented service...";

  Empty request;
  Empty response;
  ClientContext context;

  UnimplementedService::Stub* stub = serviceStub_.GetUnimplementedServiceStub();

  Status s = stub->UnimplementedCall(&context, request, &response);

  if (!AssertStatusCode(s, StatusCode::UNIMPLEMENTED,
                        context.debug_error_string())) {
    return false;
  }

  VLOG(2) << "unimplemented service done.";
  return true;
}

bool InteropClient::DoUnimplementedMethod() {
  VLOG(2) << "Sending a request for an unimplemented rpc...";

  Empty request;
  Empty response;
  ClientContext context;

  Status s =
      serviceStub_.Get()->UnimplementedCall(&context, request, &response);

  if (!AssertStatusCode(s, StatusCode::UNIMPLEMENTED,
                        context.debug_error_string())) {
    return false;
  }

  VLOG(2) << "unimplemented rpc done.";
  return true;
}

}  // namespace testing
}  // namespace grpc
