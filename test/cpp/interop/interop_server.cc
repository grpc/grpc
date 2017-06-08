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

#include <fstream>
#include <memory>
#include <sstream>
#include <thread>

#include <gflags/gflags.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/byte_stream.h"
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/cpp/interop/server_helper.h"
#include "test/cpp/util/test_config.h"

DEFINE_bool(use_tls, false, "Whether to use tls.");
DEFINE_string(custom_credentials_type, "", "User provided credentials type.");
DEFINE_int32(port, 0, "Server port.");
DEFINE_int32(max_send_message_size, -1, "The maximum send message size.");

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCredentials;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::WriteOptions;
using grpc::SslServerCredentialsOptions;
using grpc::testing::InteropServerContextInspector;
using grpc::testing::Payload;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::StreamingInputCallRequest;
using grpc::testing::StreamingInputCallResponse;
using grpc::testing::StreamingOutputCallRequest;
using grpc::testing::StreamingOutputCallResponse;
using grpc::testing::TestService;
using grpc::Status;

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
        grpc::string(iter->second.begin(), iter->second.end()));
  }
  iter = client_metadata.find(kEchoTrailingBinMetadataKey);
  if (iter != client_metadata.end()) {
    context->AddTrailingMetadata(
        kEchoTrailingBinMetadataKey,
        grpc::string(iter->second.begin(), iter->second.end()));
  }
  // Check if client sent a magic key in the header that makes us echo
  // back the user-agent (for testing purpose)
  iter = client_metadata.find(kEchoUserAgentKey);
  if (iter != client_metadata.end()) {
    iter = client_metadata.find("user-agent");
    if (iter != client_metadata.end()) {
      context->AddInitialMetadata(
          kEchoUserAgentKey,
          grpc::string(iter->second.begin(), iter->second.end()));
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
    if (!(inspector.GetMessageFlags() & GRPC_WRITE_INTERNAL_COMPRESS)) {
      gpr_log(GPR_ERROR,
              "Failure: Requested compression in a compressable request, but "
              "compression bit in message flags not set.");
      return false;
    }
  } else {
    // Didn't expect compression -> make sure the request is uncompressed
    if (inspector.GetMessageFlags() & GRPC_WRITE_INTERNAL_COMPRESS) {
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
  Status EmptyCall(ServerContext* context, const grpc::testing::Empty* request,
                   grpc::testing::Empty* response) {
    MaybeEchoMetadata(context);
    return Status::OK;
  }

  // Response contains current timestamp. We ignore everything in the request.
  Status CacheableUnaryCall(ServerContext* context,
                            const SimpleRequest* request,
                            SimpleResponse* response) {
    gpr_timespec ts = gpr_now(GPR_CLOCK_PRECISE);
    std::string timestamp = std::to_string((long long unsigned)ts.tv_nsec);
    response->mutable_payload()->set_body(timestamp.c_str(), timestamp.size());
    context->AddInitialMetadata("cache-control", "max-age=60, public");
    return Status::OK;
  }

  Status UnaryCall(ServerContext* context, const SimpleRequest* request,
                   SimpleResponse* response) {
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
      ServerWriter<StreamingOutputCallResponse>* writer) {
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
                            StreamingInputCallResponse* response) {
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
                         StreamingOutputCallRequest>* stream) {
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
            grpc::string(request.response_parameters(0).size(), '\0'));
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
      ServerContext* context,
      ServerReaderWriter<StreamingOutputCallResponse,
                         StreamingOutputCallRequest>* stream) {
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
          grpc::string(requests[i].response_parameters(0).size(), '\0'));
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
    std::shared_ptr<ServerCredentials> creds) {
  GPR_ASSERT(FLAGS_port != 0);
  std::ostringstream server_address;
  server_address << "0.0.0.0:" << FLAGS_port;
  TestServiceImpl service;

  SimpleRequest request;
  SimpleResponse response;

  ServerBuilder builder;
  builder.RegisterService(&service);
  builder.AddListeningPort(server_address.str(), creds);
  if (FLAGS_max_send_message_size >= 0) {
    builder.SetMaxSendMessageSize(FLAGS_max_send_message_size);
  }
  std::unique_ptr<Server> server(builder.BuildAndStart());
  gpr_log(GPR_INFO, "Server listening on %s", server_address.str().c_str());
  while (!gpr_atm_no_barrier_load(&g_got_sigint)) {
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_seconds(5, GPR_TIMESPAN)));
  }
}
