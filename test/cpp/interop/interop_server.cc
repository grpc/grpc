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

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <thread>

#include "absl/flags/flag.h"
#include "src/core/lib/gpr/string.h"
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/cpp/interop/server_helper.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(bool, use_alts, false,
          "Whether to use alts. Enable alts will disable tls.");
ABSL_FLAG(bool, use_tls, false, "Whether to use tls.");
ABSL_FLAG(std::string, custom_credentials_type, "",
          "User provided credentials type.");
ABSL_FLAG(int32_t, port, 0, "Server port.");
ABSL_FLAG(int32_t, max_send_message_size, -1, "The maximum send message size.");

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCredentials;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using grpc::WriteOptions;
using grpc::testing::InteropServerContextInspector;
using grpc::testing::Payload;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::StreamingInputCallRequest;
using grpc::testing::StreamingInputCallResponse;
using grpc::testing::StreamingOutputCallRequest;
using grpc::testing::StreamingOutputCallResponse;
using grpc::testing::TestService;

const char kEchoInitialMetadataKey[] = "x-grpc-test-echo-initial";
const char kEchoTrailingBinMetadataKey[] = "x-grpc-test-echo-trailing-bin";
const char kEchoUserAgentKey[] = "x-grpc-test-echo-useragent";

void MaybeEchoMetadata(ServerContext* context) {
  const auto& client_metadata = context->client_metadata();
  GPR_ASSERT(client_metadata.count(kEchoInitialMetadataKey) <= 1);
  GPR_ASSERT(client_metadata.count(kEchoTrailingBinMetadataKey) <= 1);

  auto iter = client_metadata.find(kEchoInitialMetadataKey);
  if (iter != client_metadata.end()) {
    context->AddInitialMetadata(
        kEchoInitialMetadataKey,
        std::string(iter->second.begin(), iter->second.end()));
  }
  iter = client_metadata.find(kEchoTrailingBinMetadataKey);
  if (iter != client_metadata.end()) {
    context->AddTrailingMetadata(
        kEchoTrailingBinMetadataKey,
        std::string(iter->second.begin(), iter->second.end()));
  }
  // Check if client sent a magic key in the header that makes us echo
  // back the user-agent (for testing purpose)
  iter = client_metadata.find(kEchoUserAgentKey);
  if (iter != client_metadata.end()) {
    iter = client_metadata.find("user-agent");
    if (iter != client_metadata.end()) {
      context->AddInitialMetadata(
          kEchoUserAgentKey,
          std::string(iter->second.begin(), iter->second.end()));
    }
  }
}

bool SetPayload(int size, Payload* payload) {
  std::unique_ptr<char[]> body(new char[size]());
  payload->set_body(body.get(), size);
  return true;
}

bool CheckExpectedCompression(const ServerContext& context,
                              const bool compression_expected) {
  const InteropServerContextInspector inspector(context);
  const grpc_compression_algorithm received_compression =
      inspector.GetCallCompressionAlgorithm();

  if (compression_expected) {
    if (received_compression == GRPC_COMPRESS_NONE) {
      // Expected some compression, got NONE. This is an error.
      gpr_log(GPR_ERROR,
              "Expected compression but got uncompressed request from client.");
      return false;
    }
    if (!(inspector.WasCompressed())) {
      gpr_log(GPR_ERROR,
              "Failure: Requested compression in a compressable request, but "
              "compression bit in message flags not set.");
      return false;
    }
  } else {
    // Didn't expect compression -> make sure the request is uncompressed
    if (inspector.WasCompressed()) {
      gpr_log(GPR_ERROR,
              "Failure: Didn't requested compression, but compression bit in "
              "message flags set.");
      return false;
    }
  }
  return true;
}

class TestServiceImpl : public TestService::Service {
 public:
  Status EmptyCall(ServerContext* context,
                   const grpc::testing::Empty* /*request*/,
                   grpc::testing::Empty* /*response*/) override {
    MaybeEchoMetadata(context);
    return Status::OK;
  }

  // Response contains current timestamp. We ignore everything in the request.
  Status CacheableUnaryCall(ServerContext* context,
                            const SimpleRequest* /*request*/,
                            SimpleResponse* response) override {
    gpr_timespec ts = gpr_now(GPR_CLOCK_PRECISE);
    std::string timestamp = std::to_string(ts.tv_nsec);
    response->mutable_payload()->set_body(timestamp.c_str(), timestamp.size());
    context->AddInitialMetadata("cache-control", "max-age=60, public");
    return Status::OK;
  }

  Status UnaryCall(ServerContext* context, const SimpleRequest* request,
                   SimpleResponse* response) override {
    MaybeEchoMetadata(context);
    if (request->has_response_compressed()) {
      const bool compression_requested = request->response_compressed().value();
      gpr_log(GPR_DEBUG, "Request for compression (%s) present for %s",
              compression_requested ? "enabled" : "disabled", __func__);
      if (compression_requested) {
        // Any level would do, let's go for HIGH because we are overachievers.
        context->set_compression_level(GRPC_COMPRESS_LEVEL_HIGH);
      } else {
        context->set_compression_level(GRPC_COMPRESS_LEVEL_NONE);
      }
    }
    if (!CheckExpectedCompression(*context,
                                  request->expect_compressed().value())) {
      return Status(grpc::StatusCode::INVALID_ARGUMENT,
                    "Compressed request expectation not met.");
    }
    if (request->response_size() > 0) {
      if (!SetPayload(request->response_size(), response->mutable_payload())) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      "Error creating payload.");
      }
    }

    if (request->has_response_status()) {
      return Status(
          static_cast<grpc::StatusCode>(request->response_status().code()),
          request->response_status().message());
    }

    return Status::OK;
  }

  Status StreamingOutputCall(
      ServerContext* context, const StreamingOutputCallRequest* request,
      ServerWriter<StreamingOutputCallResponse>* writer) override {
    StreamingOutputCallResponse response;
    bool write_success = true;
    for (int i = 0; write_success && i < request->response_parameters_size();
         i++) {
      if (!SetPayload(request->response_parameters(i).size(),
                      response.mutable_payload())) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      "Error creating payload.");
      }
      WriteOptions wopts;
      if (request->response_parameters(i).has_compressed()) {
        // Compress by default. Disabled on a per-message basis.
        context->set_compression_level(GRPC_COMPRESS_LEVEL_HIGH);
        const bool compression_requested =
            request->response_parameters(i).compressed().value();
        gpr_log(GPR_DEBUG, "Request for compression (%s) present for %s",
                compression_requested ? "enabled" : "disabled", __func__);
        if (!compression_requested) {
          wopts.set_no_compression();
        }  // else, compression is already enabled via the context.
      }
      int time_us;
      if ((time_us = request->response_parameters(i).interval_us()) > 0) {
        // Sleep before response if needed
        gpr_timespec sleep_time =
            gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                         gpr_time_from_micros(time_us, GPR_TIMESPAN));
        gpr_sleep_until(sleep_time);
      }
      write_success = writer->Write(response, wopts);
    }
    if (write_success) {
      return Status::OK;
    } else {
      return Status(grpc::StatusCode::INTERNAL, "Error writing response.");
    }
  }

  Status StreamingInputCall(ServerContext* context,
                            ServerReader<StreamingInputCallRequest>* reader,
                            StreamingInputCallResponse* response) override {
    StreamingInputCallRequest request;
    int aggregated_payload_size = 0;
    while (reader->Read(&request)) {
      if (!CheckExpectedCompression(*context,
                                    request.expect_compressed().value())) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      "Compressed request expectation not met.");
      }
      if (request.has_payload()) {
        aggregated_payload_size += request.payload().body().size();
      }
    }
    response->set_aggregated_payload_size(aggregated_payload_size);
    return Status::OK;
  }

  Status FullDuplexCall(
      ServerContext* context,
      ServerReaderWriter<StreamingOutputCallResponse,
                         StreamingOutputCallRequest>* stream) override {
    MaybeEchoMetadata(context);
    StreamingOutputCallRequest request;
    StreamingOutputCallResponse response;
    bool write_success = true;
    while (write_success && stream->Read(&request)) {
      if (request.has_response_status()) {
        return Status(
            static_cast<grpc::StatusCode>(request.response_status().code()),
            request.response_status().message());
      }
      if (request.response_parameters_size() != 0) {
        response.mutable_payload()->set_type(request.payload().type());
        response.mutable_payload()->set_body(
            std::string(request.response_parameters(0).size(), '\0'));
        int time_us;
        if ((time_us = request.response_parameters(0).interval_us()) > 0) {
          // Sleep before response if needed
          gpr_timespec sleep_time =
              gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                           gpr_time_from_micros(time_us, GPR_TIMESPAN));
          gpr_sleep_until(sleep_time);
        }
        write_success = stream->Write(response);
      }
    }
    if (write_success) {
      return Status::OK;
    } else {
      return Status(grpc::StatusCode::INTERNAL, "Error writing response.");
    }
  }

  Status HalfDuplexCall(
      ServerContext* /*context*/,
      ServerReaderWriter<StreamingOutputCallResponse,
                         StreamingOutputCallRequest>* stream) override {
    std::vector<StreamingOutputCallRequest> requests;
    StreamingOutputCallRequest request;
    while (stream->Read(&request)) {
      requests.push_back(request);
    }

    StreamingOutputCallResponse response;
    bool write_success = true;
    for (unsigned int i = 0; write_success && i < requests.size(); i++) {
      response.mutable_payload()->set_type(requests[i].payload().type());
      if (requests[i].response_parameters_size() == 0) {
        return Status(grpc::StatusCode::INTERNAL,
                      "Request does not have response parameters.");
      }
      response.mutable_payload()->set_body(
          std::string(requests[i].response_parameters(0).size(), '\0'));
      write_success = stream->Write(response);
    }
    if (write_success) {
      return Status::OK;
    } else {
      return Status(grpc::StatusCode::INTERNAL, "Error writing response.");
    }
  }
};

void grpc::testing::interop::RunServer(
    const std::shared_ptr<ServerCredentials>& creds) {
  RunServer(creds, absl::GetFlag(FLAGS_port), nullptr, nullptr);
}

void grpc::testing::interop::RunServer(
    const std::shared_ptr<ServerCredentials>& creds,
    std::unique_ptr<std::vector<std::unique_ptr<ServerBuilderOption>>>
        server_options) {
  RunServer(creds, absl::GetFlag(FLAGS_port), nullptr,
            std::move(server_options));
}

void grpc::testing::interop::RunServer(
    const std::shared_ptr<ServerCredentials>& creds, const int port,
    ServerStartedCondition* server_started_condition) {
  RunServer(creds, port, server_started_condition, nullptr);
}

void grpc::testing::interop::RunServer(
    const std::shared_ptr<ServerCredentials>& creds, const int port,
    ServerStartedCondition* server_started_condition,
    std::unique_ptr<std::vector<std::unique_ptr<ServerBuilderOption>>>
        server_options) {
  GPR_ASSERT(port != 0);
  std::ostringstream server_address;
  server_address << "0.0.0.0:" << port;
  TestServiceImpl service;

  SimpleRequest request;
  SimpleResponse response;

  ServerBuilder builder;
  builder.RegisterService(&service);
  builder.AddListeningPort(server_address.str(), creds);
  if (server_options != nullptr) {
    for (size_t i = 0; i < server_options->size(); i++) {
      builder.SetOption(std::move((*server_options)[i]));
    }
  }
  if (absl::GetFlag(FLAGS_max_send_message_size) >= 0) {
    builder.SetMaxSendMessageSize(absl::GetFlag(FLAGS_max_send_message_size));
  }
  std::unique_ptr<Server> server(builder.BuildAndStart());
  gpr_log(GPR_INFO, "Server listening on %s", server_address.str().c_str());

  // Signal that the server has started.
  if (server_started_condition) {
    std::unique_lock<std::mutex> lock(server_started_condition->mutex);
    server_started_condition->server_started = true;
    server_started_condition->condition.notify_all();
  }

  while (!gpr_atm_no_barrier_load(&g_got_sigint)) {
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_seconds(5, GPR_TIMESPAN)));
  }
}
