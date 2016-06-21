/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <signal.h>
#include <unistd.h>

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
#include <grpc/support/useful.h>

#include "src/proto/grpc/testing/empty.grpc.pb.h"
#include "src/proto/grpc/testing/messages.grpc.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/cpp/interop/server_helper.h"
#include "test/cpp/util/test_config.h"

DEFINE_bool(use_tls, false, "Whether to use tls.");
DEFINE_int32(port, 0, "Server port.");

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCredentials;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::SslServerCredentialsOptions;
using grpc::testing::InteropServerContextInspector;
using grpc::testing::Payload;
using grpc::testing::PayloadType;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::StreamingInputCallRequest;
using grpc::testing::StreamingInputCallResponse;
using grpc::testing::StreamingOutputCallRequest;
using grpc::testing::StreamingOutputCallResponse;
using grpc::testing::TestService;
using grpc::Status;

static bool got_sigint = false;
static const char* kRandomFile = "test/cpp/interop/rnd.dat";

const char kEchoInitialMetadataKey[] = "x-grpc-test-echo-initial";
const char kEchoTrailingBinMetadataKey[] = "x-grpc-test-echo-trailing-bin";
const char kEchoUserAgentKey[] = "x-grpc-test-echo-useragent";

void MaybeEchoMetadata(ServerContext* context) {
  const auto& client_metadata = context->client_metadata();
  GPR_ASSERT(client_metadata.count(kEchoInitialMetadataKey) <= 1);
  GPR_ASSERT(client_metadata.count(kEchoTrailingBinMetadataKey) <= 1);

  auto iter = client_metadata.find(kEchoInitialMetadataKey);
  if (iter != client_metadata.end()) {
    context->AddInitialMetadata(kEchoInitialMetadataKey, iter->second.data());
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
      context->AddInitialMetadata(kEchoUserAgentKey, iter->second.data());
    }
  }
}

bool SetPayload(PayloadType response_type, int size, Payload* payload) {
  payload->set_type(response_type);
  switch (response_type) {
    case PayloadType::COMPRESSABLE: {
      std::unique_ptr<char[]> body(new char[size]());
      payload->set_body(body.get(), size);
    } break;
    case PayloadType::UNCOMPRESSABLE: {
      std::unique_ptr<char[]> body(new char[size]());
      std::ifstream rnd_file(kRandomFile);
      GPR_ASSERT(rnd_file.good());
      rnd_file.read(body.get(), size);
      GPR_ASSERT(!rnd_file.eof());  // Requested more rnd bytes than available
      payload->set_body(body.get(), size);
    } break;
    default:
      GPR_ASSERT(false);
  }
  return true;
}

template <typename RequestType>
void SetResponseCompression(ServerContext* context,
                            const RequestType& request) {
  if (request.request_compressed_response()) {
    // Any level would do, let's go for HIGH because we are overachievers.
    context->set_compression_level(GRPC_COMPRESS_LEVEL_HIGH);
  }
}

class TestServiceImpl : public TestService::Service {
 public:
  Status EmptyCall(ServerContext* context, const grpc::testing::Empty* request,
                   grpc::testing::Empty* response) {
    MaybeEchoMetadata(context);
    return Status::OK;
  }

  Status UnaryCall(ServerContext* context, const SimpleRequest* request,
                   SimpleResponse* response) {
    MaybeEchoMetadata(context);
    SetResponseCompression(context, *request);
    if (request->response_size() > 0) {
      if (!SetPayload(request->response_type(), request->response_size(),
                      response->mutable_payload())) {
        return Status(grpc::StatusCode::INTERNAL, "Error creating payload.");
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
    SetResponseCompression(context, *request);
    StreamingOutputCallResponse response;
    bool write_success = true;
    for (int i = 0; write_success && i < request->response_parameters_size();
         i++) {
      if (!SetPayload(request->response_type(),
                      request->response_parameters(i).size(),
                      response.mutable_payload())) {
        return Status(grpc::StatusCode::INTERNAL, "Error creating payload.");
      }
      int time_us;
      if ((time_us = request->response_parameters(i).interval_us()) > 0) {
        // Sleep before response if needed
        gpr_timespec sleep_time =
            gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                         gpr_time_from_micros(time_us, GPR_TIMESPAN));
        gpr_sleep_until(sleep_time);
      }
      write_success = writer->Write(response);
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
      SetResponseCompression(context, request);
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

void RunServer() {
  std::ostringstream server_address;
  server_address << "0.0.0.0:" << FLAGS_port;
  TestServiceImpl service;

  SimpleRequest request;
  SimpleResponse response;

  ServerBuilder builder;
  builder.RegisterService(&service);
  std::shared_ptr<ServerCredentials> creds =
      grpc::testing::CreateInteropServerCredentials();
  builder.AddListeningPort(server_address.str(), creds);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  gpr_log(GPR_INFO, "Server listening on %s", server_address.str().c_str());
  while (!got_sigint) {
    sleep(5);
  }
}

static void sigint_handler(int x) { got_sigint = true; }

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  signal(SIGINT, sigint_handler);

  GPR_ASSERT(FLAGS_port != 0);
  RunServer();

  return 0;
}
