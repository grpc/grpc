/*
 *
 * Copyright 2015-2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "test/cpp/interop/interop_client.h"

#include <cinttypes>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/strings/match.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/security/credentials.h>

#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/core/util/histogram.h"
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
      gpr_log(GPR_ERROR,
              "Failure: Requested compression but got uncompressed response "
              "from server.");
      abort();
    }
    GPR_ASSERT(inspector.WasCompressed());
  } else {
    // Didn't request compression -> make sure the response is uncompressed
    GPR_ASSERT(!(inspector.WasCompressed()));
  }
}
}  // namespace

InteropClient::ServiceStub::ServiceStub(
    ChannelCreationFunc channel_creation_func, bool new_stub_every_call)
    : channel_creation_func_(std::move(channel_creation_func)),
      channel_(channel_creation_func_()),
      new_stub_every_call_(new_stub_every_call) {
  // If new_stub_every_call is false, then this is our chance to initialize
  // stub_. (see Get())
  if (!new_stub_every_call) {
    stub_ = TestService::NewStub(channel_);
  }
}

TestService::Stub* InteropClient::ServiceStub::Get() {
  if (new_stub_every_call_) {
    stub_ = TestService::NewStub(channel_);
  }

  return stub_.get();
}

UnimplementedService::Stub*
InteropClient::ServiceStub::GetUnimplementedServiceStub() {
  if (unimplemented_service_stub_ == nullptr) {
    unimplemented_service_stub_ = UnimplementedService::NewStub(channel_);
  }
  return unimplemented_service_stub_.get();
}

void InteropClient::ServiceStub::ResetChannel() {
  channel_ = channel_creation_func_();
  if (!new_stub_every_call_) {
    stub_ = TestService::NewStub(channel_);
  }
}

InteropClient::InteropClient(ChannelCreationFunc channel_creation_func,
                             bool new_stub_every_test_case,
                             bool do_not_abort_on_transient_failures)
    : serviceStub_(std::move(channel_creation_func), new_stub_every_test_case),
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

  gpr_log(GPR_ERROR,
          "Error status code: %d (expected: %d), message: %s,"
          " debug string: %s",
          s.error_code(), expected_code, s.error_message().c_str(),
          optional_debug_string.c_str());

  // In case of transient transient/retryable failures (like a broken
  // connection) we may or may not abort (see TransientFailureOrAbort())
  if (s.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return TransientFailureOrAbort();
  }

  abort();
}

bool InteropClient::DoEmpty() {
  gpr_log(GPR_DEBUG, "Sending an empty rpc...");

  Empty request;
  Empty response;
  ClientContext context;

  Status s = serviceStub_.Get()->EmptyCall(&context, request, &response);

  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  gpr_log(GPR_DEBUG, "Empty rpc done.");
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
  GPR_ASSERT(response->payload().body() ==
             std::string(kLargeResponseSize, '\0'));
  return true;
}

bool InteropClient::DoComputeEngineCreds(
    const std::string& default_service_account,
    const std::string& oauth_scope) {
  gpr_log(GPR_DEBUG,
          "Sending a large unary rpc with compute engine credentials ...");
  SimpleRequest request;
  SimpleResponse response;
  request.set_fill_username(true);
  request.set_fill_oauth_scope(true);

  if (!PerformLargeUnary(&request, &response)) {
    return false;
  }

  gpr_log(GPR_DEBUG, "Got username %s", response.username().c_str());
  gpr_log(GPR_DEBUG, "Got oauth_scope %s", response.oauth_scope().c_str());
  GPR_ASSERT(!response.username().empty());
  GPR_ASSERT(response.username().c_str() == default_service_account);
  GPR_ASSERT(!response.oauth_scope().empty());
  const char* oauth_scope_str = response.oauth_scope().c_str();
  GPR_ASSERT(absl::StrContains(oauth_scope, oauth_scope_str));
  gpr_log(GPR_DEBUG, "Large unary with compute engine creds done.");
  return true;
}

bool InteropClient::DoOauth2AuthToken(const std::string& username,
                                      const std::string& oauth_scope) {
  gpr_log(GPR_DEBUG,
          "Sending a unary rpc with raw oauth2 access token credentials ...");
  SimpleRequest request;
  SimpleResponse response;
  request.set_fill_username(true);
  request.set_fill_oauth_scope(true);

  ClientContext context;

  Status s = serviceStub_.Get()->UnaryCall(&context, request, &response);

  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  GPR_ASSERT(!response.username().empty());
  GPR_ASSERT(!response.oauth_scope().empty());
  GPR_ASSERT(username == response.username());
  const char* oauth_scope_str = response.oauth_scope().c_str();
  GPR_ASSERT(absl::StrContains(oauth_scope, oauth_scope_str));
  gpr_log(GPR_DEBUG, "Unary with oauth2 access token credentials done.");
  return true;
}

bool InteropClient::DoPerRpcCreds(const std::string& json_key) {
  gpr_log(GPR_DEBUG, "Sending a unary rpc with per-rpc JWT access token ...");
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

  GPR_ASSERT(!response.username().empty());
  GPR_ASSERT(json_key.find(response.username()) != std::string::npos);
  gpr_log(GPR_DEBUG, "Unary with per-rpc JWT access token done.");
  return true;
}

bool InteropClient::DoJwtTokenCreds(const std::string& username) {
  gpr_log(GPR_DEBUG,
          "Sending a large unary rpc with JWT token credentials ...");
  SimpleRequest request;
  SimpleResponse response;
  request.set_fill_username(true);

  if (!PerformLargeUnary(&request, &response)) {
    return false;
  }

  GPR_ASSERT(!response.username().empty());
  GPR_ASSERT(username.find(response.username()) != std::string::npos);
  gpr_log(GPR_DEBUG, "Large unary with JWT token creds done.");
  return true;
}

bool InteropClient::DoGoogleDefaultCredentials(
    const std::string& default_service_account) {
  gpr_log(GPR_DEBUG,
          "Sending a large unary rpc with GoogleDefaultCredentials...");
  SimpleRequest request;
  SimpleResponse response;
  request.set_fill_username(true);

  if (!PerformLargeUnary(&request, &response)) {
    return false;
  }

  gpr_log(GPR_DEBUG, "Got username %s", response.username().c_str());
  GPR_ASSERT(!response.username().empty());
  GPR_ASSERT(response.username().c_str() == default_service_account);
  gpr_log(GPR_DEBUG, "Large unary rpc with GoogleDefaultCredentials done.");
  return true;
}

bool InteropClient::DoLargeUnary() {
  gpr_log(GPR_DEBUG, "Sending a large unary rpc...");
  SimpleRequest request;
  SimpleResponse response;
  if (!PerformLargeUnary(&request, &response)) {
    return false;
  }
  gpr_log(GPR_DEBUG, "Large unary done.");
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

  gpr_log(GPR_DEBUG, "Sending probe for compressed unary request.");
  const Status s =
      serviceStub_.Get()->UnaryCall(&probe_context, probe_req, &probe_res);
  if (s.error_code() != grpc::StatusCode::INVALID_ARGUMENT) {
    // The server isn't able to evaluate incoming compression, making the rest
    // of this test moot.
    gpr_log(GPR_DEBUG, "Compressed unary request probe failed");
    return false;
  }
  gpr_log(GPR_DEBUG, "Compressed unary request probe succeeded. Proceeding.");

  const std::vector<bool> compressions = {true, false};
  for (size_t i = 0; i < compressions.size(); i++) {
    std::string log_suffix =
        absl::StrFormat("(compression=%s)", compressions[i] ? "true" : "false");

    gpr_log(GPR_DEBUG, "Sending compressed unary request %s.",
            log_suffix.c_str());
    SimpleRequest request;
    SimpleResponse response;
    request.mutable_expect_compressed()->set_value(compressions[i]);
    if (!PerformLargeUnary(&request, &response, UnaryCompressionChecks)) {
      gpr_log(GPR_ERROR, "Compressed unary request failed %s",
              log_suffix.c_str());
      return false;
    }

    gpr_log(GPR_DEBUG, "Compressed unary request failed %s",
            log_suffix.c_str());
  }

  return true;
}

bool InteropClient::DoServerCompressedUnary() {
  const std::vector<bool> compressions = {true, false};
  for (size_t i = 0; i < compressions.size(); i++) {
    std::string log_suffix =
        absl::StrFormat("(compression=%s)", compressions[i] ? "true" : "false");

    gpr_log(GPR_DEBUG, "Sending unary request for compressed response %s.",
            log_suffix.c_str());
    SimpleRequest request;
    SimpleResponse response;
    request.mutable_response_compressed()->set_value(compressions[i]);

    if (!PerformLargeUnary(&request, &response, UnaryCompressionChecks)) {
      gpr_log(GPR_ERROR, "Request for compressed unary failed %s",
              log_suffix.c_str());
      return false;
    }

    gpr_log(GPR_DEBUG, "Request for compressed unary failed %s",
            log_suffix.c_str());
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
  gpr_log(GPR_DEBUG, "Sending request steaming rpc ...");

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
      gpr_log(GPR_ERROR, "DoRequestStreaming(): stream->Write() failed");
      return TransientFailureOrAbort();
    }
    aggregated_payload_size += request_stream_sizes[i];
  }
  GPR_ASSERT(stream->WritesDone());

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  GPR_ASSERT(response.aggregated_payload_size() == aggregated_payload_size);
  return true;
}

bool InteropClient::DoResponseStreaming() {
  gpr_log(GPR_DEBUG, "Receiving response streaming rpc ...");

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
    GPR_ASSERT(response.payload().body() ==
               std::string(response_stream_sizes[i], '\0'));
    ++i;
  }

  if (i < response_stream_sizes.size()) {
    // stream->Read() failed before reading all the expected messages. This is
    // most likely due to connection failure.
    gpr_log(GPR_ERROR,
            "DoResponseStreaming(): Read fewer streams (%d) than "
            "response_stream_sizes.size() (%" PRIuPTR ")",
            i, response_stream_sizes.size());
    return TransientFailureOrAbort();
  }

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  gpr_log(GPR_DEBUG, "Response streaming done.");
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

  gpr_log(GPR_DEBUG, "Sending probe for compressed streaming request.");

  std::unique_ptr<ClientWriter<StreamingInputCallRequest>> probe_stream(
      serviceStub_.Get()->StreamingInputCall(&probe_context, &probe_res));

  if (!probe_stream->Write(probe_req)) {
    gpr_log(GPR_ERROR, "%s(): stream->Write() failed", __func__);
    return TransientFailureOrAbort();
  }
  Status s = probe_stream->Finish();
  if (s.error_code() != grpc::StatusCode::INVALID_ARGUMENT) {
    // The server isn't able to evaluate incoming compression, making the rest
    // of this test moot.
    gpr_log(GPR_DEBUG, "Compressed streaming request probe failed");
    return false;
  }
  gpr_log(GPR_DEBUG,
          "Compressed streaming request probe succeeded. Proceeding.");

  ClientContext context;
  StreamingInputCallRequest request;
  StreamingInputCallResponse response;

  context.set_compression_algorithm(GRPC_COMPRESS_GZIP);
  std::unique_ptr<ClientWriter<StreamingInputCallRequest>> stream(
      serviceStub_.Get()->StreamingInputCall(&context, &response));

  request.mutable_payload()->set_body(std::string(27182, '\0'));
  request.mutable_expect_compressed()->set_value(true);
  gpr_log(GPR_DEBUG, "Sending streaming request with compression enabled");
  if (!stream->Write(request)) {
    gpr_log(GPR_ERROR, "%s(): stream->Write() failed", __func__);
    return TransientFailureOrAbort();
  }

  WriteOptions wopts;
  wopts.set_no_compression();
  request.mutable_payload()->set_body(std::string(45904, '\0'));
  request.mutable_expect_compressed()->set_value(false);
  gpr_log(GPR_DEBUG, "Sending streaming request with compression disabled");
  if (!stream->Write(request, wopts)) {
    gpr_log(GPR_ERROR, "%s(): stream->Write() failed", __func__);
    return TransientFailureOrAbort();
  }
  GPR_ASSERT(stream->WritesDone());

  s = stream->Finish();
  return AssertStatusOk(s, context.debug_error_string());
}

bool InteropClient::DoServerCompressedStreaming() {
  const std::vector<bool> compressions = {true, false};
  const std::vector<int> sizes = {31415, 92653};

  ClientContext context;
  InteropClientContextInspector inspector(context);
  StreamingOutputCallRequest request;

  GPR_ASSERT(compressions.size() == sizes.size());
  for (size_t i = 0; i < sizes.size(); i++) {
    std::string log_suffix =
        absl::StrFormat("(compression=%s; size=%d)",
                        compressions[i] ? "true" : "false", sizes[i]);

    gpr_log(GPR_DEBUG, "Sending request streaming rpc %s.", log_suffix.c_str());

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
    GPR_ASSERT(response.payload().body() ==
               std::string(request.response_parameters(k).size(), '\0'));

    // Compression checks.
    GPR_ASSERT(request.response_parameters(k).has_compressed());
    if (request.response_parameters(k).compressed().value()) {
      GPR_ASSERT(inspector.GetCallCompressionAlgorithm() > GRPC_COMPRESS_NONE);
      GPR_ASSERT(inspector.WasCompressed());
    } else {
      // requested *no* compression.
      GPR_ASSERT(!(inspector.WasCompressed()));
    }
    ++k;
  }

  if (k < sizes.size()) {
    // stream->Read() failed before reading all the expected messages. This
    // is most likely due to a connection failure.
    gpr_log(GPR_ERROR,
            "%s(): Responses read (k=%" PRIuPTR
            ") is less than the expected number of  messages (%" PRIuPTR ").",
            __func__, k, sizes.size());
    return TransientFailureOrAbort();
  }

  Status s = stream->Finish();
  return AssertStatusOk(s, context.debug_error_string());
}

bool InteropClient::DoResponseStreamingWithSlowConsumer() {
  gpr_log(GPR_DEBUG, "Receiving response streaming rpc with slow consumer ...");

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
    GPR_ASSERT(response.payload().body() ==
               std::string(kResponseMessageSize, '\0'));
    gpr_log(GPR_DEBUG, "received message %d", i);
    gpr_sleep_until(gpr_time_add(
        gpr_now(GPR_CLOCK_REALTIME),
        gpr_time_from_millis(kReceiveDelayMilliSeconds, GPR_TIMESPAN)));
    ++i;
  }

  if (i < kNumResponseMessages) {
    gpr_log(GPR_ERROR,
            "DoResponseStreamingWithSlowConsumer(): Responses read (i=%d) is "
            "less than the expected messages (i.e kNumResponseMessages = %d)",
            i, kNumResponseMessages);

    return TransientFailureOrAbort();
  }

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  gpr_log(GPR_DEBUG, "Response streaming done.");
  return true;
}

bool InteropClient::DoHalfDuplex() {
  gpr_log(GPR_DEBUG, "Sending half-duplex streaming rpc ...");

  ClientContext context;
  std::unique_ptr<ClientReaderWriter<StreamingOutputCallRequest,
                                     StreamingOutputCallResponse>>
      stream(serviceStub_.Get()->HalfDuplexCall(&context));

  StreamingOutputCallRequest request;
  ResponseParameters* response_parameter = request.add_response_parameters();
  for (unsigned int i = 0; i < response_stream_sizes.size(); ++i) {
    response_parameter->set_size(response_stream_sizes[i]);

    if (!stream->Write(request)) {
      gpr_log(GPR_ERROR, "DoHalfDuplex(): stream->Write() failed. i=%d", i);
      return TransientFailureOrAbort();
    }
  }
  stream->WritesDone();

  unsigned int i = 0;
  StreamingOutputCallResponse response;
  while (stream->Read(&response)) {
    GPR_ASSERT(response.payload().body() ==
               std::string(response_stream_sizes[i], '\0'));
    ++i;
  }

  if (i < response_stream_sizes.size()) {
    // stream->Read() failed before reading all the expected messages. This is
    // most likely due to a connection failure
    gpr_log(GPR_ERROR,
            "DoHalfDuplex(): Responses read (i=%d) are less than the expected "
            "number of messages response_stream_sizes.size() (%" PRIuPTR ")",
            i, response_stream_sizes.size());
    return TransientFailureOrAbort();
  }

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  gpr_log(GPR_DEBUG, "Half-duplex streaming rpc done.");
  return true;
}

bool InteropClient::DoPingPong() {
  gpr_log(GPR_DEBUG, "Sending Ping Pong streaming rpc ...");

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
      gpr_log(GPR_ERROR, "DoPingPong(): stream->Write() failed. i: %d", i);
      return TransientFailureOrAbort();
    }

    if (!stream->Read(&response)) {
      gpr_log(GPR_ERROR, "DoPingPong(): stream->Read() failed. i:%d", i);
      return TransientFailureOrAbort();
    }

    GPR_ASSERT(response.payload().body() ==
               std::string(response_stream_sizes[i], '\0'));
  }

  stream->WritesDone();

  GPR_ASSERT(!stream->Read(&response));

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  gpr_log(GPR_DEBUG, "Ping pong streaming done.");
  return true;
}

bool InteropClient::DoCancelAfterBegin() {
  gpr_log(GPR_DEBUG, "Sending request streaming rpc ...");

  ClientContext context;
  StreamingInputCallRequest request;
  StreamingInputCallResponse response;

  std::unique_ptr<ClientWriter<StreamingInputCallRequest>> stream(
      serviceStub_.Get()->StreamingInputCall(&context, &response));

  gpr_log(GPR_DEBUG, "Trying to cancel...");
  context.TryCancel();
  Status s = stream->Finish();

  if (!AssertStatusCode(s, StatusCode::CANCELLED,
                        context.debug_error_string())) {
    return false;
  }

  gpr_log(GPR_DEBUG, "Canceling streaming done.");
  return true;
}

bool InteropClient::DoCancelAfterFirstResponse() {
  gpr_log(GPR_DEBUG, "Sending Ping Pong streaming rpc ...");

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
    gpr_log(GPR_ERROR, "DoCancelAfterFirstResponse(): stream->Write() failed");
    return TransientFailureOrAbort();
  }

  if (!stream->Read(&response)) {
    gpr_log(GPR_ERROR, "DoCancelAfterFirstResponse(): stream->Read failed");
    return TransientFailureOrAbort();
  }
  GPR_ASSERT(response.payload().body() == std::string(31415, '\0'));

  gpr_log(GPR_DEBUG, "Trying to cancel...");
  context.TryCancel();

  Status s = stream->Finish();
  gpr_log(GPR_DEBUG, "Canceling pingpong streaming done.");
  return true;
}

bool InteropClient::DoTimeoutOnSleepingServer() {
  gpr_log(GPR_DEBUG,
          "Sending Ping Pong streaming rpc with a short deadline...");

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

  gpr_log(GPR_DEBUG, "Pingpong streaming timeout done.");
  return true;
}

bool InteropClient::DoEmptyStream() {
  gpr_log(GPR_DEBUG, "Starting empty_stream.");

  ClientContext context;
  std::unique_ptr<ClientReaderWriter<StreamingOutputCallRequest,
                                     StreamingOutputCallResponse>>
      stream(serviceStub_.Get()->FullDuplexCall(&context));
  stream->WritesDone();
  StreamingOutputCallResponse response;
  GPR_ASSERT(stream->Read(&response) == false);

  Status s = stream->Finish();
  if (!AssertStatusOk(s, context.debug_error_string())) {
    return false;
  }

  gpr_log(GPR_DEBUG, "empty_stream done.");
  return true;
}

bool InteropClient::DoStatusWithMessage() {
  gpr_log(GPR_DEBUG,
          "Sending RPC with a request for status code 2 and message");

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
  GPR_ASSERT(s.error_message() == test_msg);

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
  GPR_ASSERT(s.error_message() == test_msg);

  gpr_log(GPR_DEBUG, "Done testing Status and Message");
  return true;
}

bool InteropClient::DoSpecialStatusMessage() {
  gpr_log(
      GPR_DEBUG,
      "Sending RPC with a request for status code 2 and message - \\t\\ntest "
      "with whitespace\\r\\nand Unicode BMP ☺ and non-BMP 😈\\t\\n");
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
  GPR_ASSERT(s.error_message() == test_msg);
  gpr_log(GPR_DEBUG, "Done testing Special Status Message");
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
      gpr_log(GPR_ERROR, "#%d rpc hits server_id %s, expect server_id %s", i,
              response.server_id().c_str(), server_id.c_str());
      return false;
    }
  }
  gpr_log(GPR_DEBUG, "pick first unary successfully finished");
  return true;
}

bool InteropClient::DoCustomMetadata() {
  const std::string kEchoInitialMetadataKey("x-grpc-test-echo-initial");
  const std::string kInitialMetadataValue("test_initial_metadata_value");
  const std::string kEchoTrailingBinMetadataKey(
      "x-grpc-test-echo-trailing-bin");
  const std::string kTrailingBinValue("\x0a\x0b\x0a\x0b\x0a\x0b");

  {
    gpr_log(GPR_DEBUG, "Sending RPC with custom metadata");
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
    GPR_ASSERT(iter != server_initial_metadata.end());
    GPR_ASSERT(iter->second == kInitialMetadataValue);
    const auto& server_trailing_metadata = context.GetServerTrailingMetadata();
    iter = server_trailing_metadata.find(kEchoTrailingBinMetadataKey);
    GPR_ASSERT(iter != server_trailing_metadata.end());
    GPR_ASSERT(std::string(iter->second.begin(), iter->second.end()) ==
               kTrailingBinValue);

    gpr_log(GPR_DEBUG, "Done testing RPC with custom metadata");
  }

  {
    gpr_log(GPR_DEBUG, "Sending stream with custom metadata");
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
      gpr_log(GPR_ERROR, "DoCustomMetadata(): stream->Write() failed");
      return TransientFailureOrAbort();
    }

    stream->WritesDone();

    if (!stream->Read(&response)) {
      gpr_log(GPR_ERROR, "DoCustomMetadata(): stream->Read() failed");
      return TransientFailureOrAbort();
    }

    GPR_ASSERT(response.payload().body() ==
               std::string(kLargeResponseSize, '\0'));

    GPR_ASSERT(!stream->Read(&response));

    Status s = stream->Finish();
    if (!AssertStatusOk(s, context.debug_error_string())) {
      return false;
    }

    const auto& server_initial_metadata = context.GetServerInitialMetadata();
    auto iter = server_initial_metadata.find(kEchoInitialMetadataKey);
    GPR_ASSERT(iter != server_initial_metadata.end());
    GPR_ASSERT(iter->second == kInitialMetadataValue);
    const auto& server_trailing_metadata = context.GetServerTrailingMetadata();
    iter = server_trailing_metadata.find(kEchoTrailingBinMetadataKey);
    GPR_ASSERT(iter != server_trailing_metadata.end());
    GPR_ASSERT(std::string(iter->second.begin(), iter->second.end()) ==
               kTrailingBinValue);

    gpr_log(GPR_DEBUG, "Done testing stream with custom metadata");
  }

  return true;
}

std::tuple<bool, int32_t, std::string>
InteropClient::PerformOneSoakTestIteration(
    const bool reset_channel,
    const int32_t max_acceptable_per_iteration_latency_ms) {
  gpr_timespec start = gpr_now(GPR_CLOCK_MONOTONIC);
  SimpleRequest request;
  SimpleResponse response;
  // Don't set the deadline on the RPC, and instead just
  // record how long the RPC took and compare. This makes
  // debugging easier when looking at failure results.
  ClientContext context;
  InteropClientContextInspector inspector(context);
  request.set_response_size(kLargeResponseSize);
  std::string payload(kLargeRequestSize, '\0');
  request.mutable_payload()->set_body(payload.c_str(), kLargeRequestSize);
  if (reset_channel) {
    serviceStub_.ResetChannel();
  }
  Status s = serviceStub_.Get()->UnaryCall(&context, request, &response);
  gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
  int32_t elapsed_ms = gpr_time_to_millis(gpr_time_sub(now, start));
  if (!s.ok()) {
    return std::make_tuple(false, elapsed_ms, context.debug_error_string());
  } else if (elapsed_ms > max_acceptable_per_iteration_latency_ms) {
    std::string debug_string = absl::StrFormat(
        "%d ms exceeds max acceptable latency: %d ms, peer: %s", elapsed_ms,
        max_acceptable_per_iteration_latency_ms, context.peer());
    return std::make_tuple(false, elapsed_ms, std::move(debug_string));
  } else {
    return std::make_tuple(true, elapsed_ms, "");
  }
}

void InteropClient::PerformSoakTest(
    const bool reset_channel_per_iteration, const int32_t soak_iterations,
    const int32_t max_failures,
    const int32_t max_acceptable_per_iteration_latency_ms,
    const int32_t min_time_ms_between_rpcs,
    const int32_t overall_timeout_seconds) {
  std::vector<std::tuple<bool, int32_t, std::string>> results;
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
        reset_channel_per_iteration, max_acceptable_per_iteration_latency_ms);
    bool success = std::get<0>(result);
    int32_t elapsed_ms = std::get<1>(result);
    std::string debug_string = std::get<2>(result);
    results.push_back(result);
    if (!success) {
      gpr_log(GPR_DEBUG, "soak iteration: %d elapsed_ms: %d failed: %s", i,
              elapsed_ms, debug_string.c_str());
      total_failures++;
    } else {
      gpr_log(GPR_DEBUG, "soak iteration: %d elapsed_ms: %d succeeded", i,
              elapsed_ms);
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
    gpr_log(
        GPR_ERROR,
        "soak test consumed all %d seconds of time and quit early, only "
        "having ran %d out of desired %d iterations. "
        "total_failures: %d. "
        "max_failures_threshold: %d. "
        "median_soak_iteration_latency: %lf ms. "
        "90th_soak_iteration_latency: %lf ms. "
        "worst_soak_iteration_latency: %lf ms. "
        "Some or all of the iterations that did run were unexpectedly slow. "
        "See breakdown above for which iterations succeeded, failed, and "
        "why for more info.",
        overall_timeout_seconds, iterations_ran, soak_iterations,
        total_failures, max_failures, latency_ms_median, latency_ms_90th,
        latency_ms_worst);
    GPR_ASSERT(0);
  } else if (total_failures > max_failures) {
    gpr_log(GPR_ERROR,
            "soak test ran: %d iterations. total_failures: %d exceeds "
            "max_failures_threshold: %d. "
            "median_soak_iteration_latency: %lf ms. "
            "90th_soak_iteration_latency: %lf ms. "
            "worst_soak_iteration_latency: %lf ms. "
            "See breakdown above for which iterations succeeded, failed, and "
            "why for more info.",
            soak_iterations, total_failures, max_failures, latency_ms_median,
            latency_ms_90th, latency_ms_worst);
    GPR_ASSERT(0);
  } else {
    gpr_log(GPR_INFO,
            "soak test ran: %d iterations. total_failures: %d is within "
            "max_failures_threshold: %d. "
            "median_soak_iteration_latency: %lf ms. "
            "90th_soak_iteration_latency: %lf ms. "
            "worst_soak_iteration_latency: %lf ms. "
            "See breakdown above for which iterations succeeded, failed, and "
            "why for more info.",
            soak_iterations, total_failures, max_failures, latency_ms_median,
            latency_ms_90th, latency_ms_worst);
  }
}

bool InteropClient::DoRpcSoakTest(
    int32_t soak_iterations, int32_t max_failures,
    int64_t max_acceptable_per_iteration_latency_ms,
    int32_t soak_min_time_ms_between_rpcs, int32_t overall_timeout_seconds) {
  gpr_log(GPR_DEBUG, "Sending %d RPCs...", soak_iterations);
  GPR_ASSERT(soak_iterations > 0);
  PerformSoakTest(false /* reset channel per iteration */, soak_iterations,
                  max_failures, max_acceptable_per_iteration_latency_ms,
                  soak_min_time_ms_between_rpcs, overall_timeout_seconds);
  gpr_log(GPR_DEBUG, "rpc_soak test done.");
  return true;
}

bool InteropClient::DoChannelSoakTest(
    int32_t soak_iterations, int32_t max_failures,
    int64_t max_acceptable_per_iteration_latency_ms,
    int32_t soak_min_time_ms_between_rpcs, int32_t overall_timeout_seconds) {
  gpr_log(GPR_DEBUG, "Sending %d RPCs, tearing down the channel each time...",
          soak_iterations);
  GPR_ASSERT(soak_iterations > 0);
  PerformSoakTest(true /* reset channel per iteration */, soak_iterations,
                  max_failures, max_acceptable_per_iteration_latency_ms,
                  soak_min_time_ms_between_rpcs, overall_timeout_seconds);
  gpr_log(GPR_DEBUG, "channel_soak test done.");
  return true;
}

bool InteropClient::DoLongLivedChannelTest(int32_t soak_iterations,
                                           int32_t iteration_interval) {
  gpr_log(GPR_DEBUG, "Sending %d RPCs...", soak_iterations);
  GPR_ASSERT(soak_iterations > 0);
  GPR_ASSERT(iteration_interval > 0);
  SimpleRequest request;
  SimpleResponse response;
  int num_failures = 0;
  for (int i = 0; i < soak_iterations; ++i) {
    gpr_log(GPR_DEBUG, "Sending RPC number %d...", i);
    if (!PerformLargeUnary(&request, &response)) {
      gpr_log(GPR_ERROR, "Iteration %d failed.", i);
      num_failures++;
    }
    gpr_sleep_until(
        gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                     gpr_time_from_seconds(iteration_interval, GPR_TIMESPAN)));
  }
  if (num_failures == 0) {
    gpr_log(GPR_DEBUG, "long_lived_channel test done.");
    return true;
  } else {
    gpr_log(GPR_DEBUG, "long_lived_channel test failed with %d rpc failures.",
            num_failures);
    return false;
  }
}

bool InteropClient::DoUnimplementedService() {
  gpr_log(GPR_DEBUG, "Sending a request for an unimplemented service...");

  Empty request;
  Empty response;
  ClientContext context;

  UnimplementedService::Stub* stub = serviceStub_.GetUnimplementedServiceStub();

  Status s = stub->UnimplementedCall(&context, request, &response);

  if (!AssertStatusCode(s, StatusCode::UNIMPLEMENTED,
                        context.debug_error_string())) {
    return false;
  }

  gpr_log(GPR_DEBUG, "unimplemented service done.");
  return true;
}

bool InteropClient::DoUnimplementedMethod() {
  gpr_log(GPR_DEBUG, "Sending a request for an unimplemented rpc...");

  Empty request;
  Empty response;
  ClientContext context;

  Status s =
      serviceStub_.Get()->UnimplementedCall(&context, request, &response);

  if (!AssertStatusCode(s, StatusCode::UNIMPLEMENTED,
                        context.debug_error_string())) {
    return false;
  }

  gpr_log(GPR_DEBUG, "unimplemented rpc done.");
  return true;
}

}  // namespace testing
}  // namespace grpc
