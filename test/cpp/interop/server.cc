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

#include <memory>
#include <sstream>
#include <thread>

#include <signal.h>
#include <unistd.h>

#include <gflags/gflags.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "test/core/end2end/data/ssl_test_data.h"
#include <grpc++/config.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "test/cpp/interop/test.pb.h"
#include "test/cpp/interop/empty.pb.h"
#include "test/cpp/interop/messages.pb.h"

DEFINE_bool(enable_ssl, false, "Whether to use ssl/tls.");
DEFINE_int32(port, 0, "Server port.");

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCredentials;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::SslServerCredentialsOptions;
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

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

static bool got_sigint = false;

bool SetPayload(PayloadType type, int size, Payload* payload) {
  PayloadType response_type = type;
  // TODO(yangg): Support UNCOMPRESSABLE payload.
  if (type != PayloadType::COMPRESSABLE) {
    return false;
  }
  payload->set_type(response_type);
  std::unique_ptr<char[]> body(new char[size]());
  payload->set_body(body.get(), size);
  return true;
}

class TestServiceImpl : public TestService::Service {
 public:
  Status EmptyCall(ServerContext* context, const grpc::testing::Empty* request,
                   grpc::testing::Empty* response) {
    return Status::OK;
  }

  Status UnaryCall(ServerContext* context, const SimpleRequest* request,
                   SimpleResponse* response) {
    if (request->has_response_size() && request->response_size() > 0) {
      if (!SetPayload(request->response_type(), request->response_size(),
                      response->mutable_payload())) {
        return Status(grpc::StatusCode::INTERNAL, "Error creating payload.");
      }
    }
    return Status::OK;
  }

  Status StreamingOutputCall(
      ServerContext* context, const StreamingOutputCallRequest* request,
      ServerWriter<StreamingOutputCallResponse>* writer) {
    StreamingOutputCallResponse response;
    bool write_success = true;
    response.mutable_payload()->set_type(request->response_type());
    for (int i = 0; write_success && i < request->response_parameters_size();
         i++) {
      response.mutable_payload()->set_body(
          grpc::string(request->response_parameters(i).size(), '\0'));
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
      if (request.has_payload() && request.payload().has_body()) {
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
    StreamingOutputCallRequest request;
    StreamingOutputCallResponse response;
    bool write_success = true;
    while (write_success && stream->Read(&request)) {
      response.mutable_payload()->set_type(request.payload().type());
      if (request.response_parameters_size() == 0) {
        return Status(grpc::StatusCode::INTERNAL,
                      "Request does not have response parameters.");
      }
      response.mutable_payload()->set_body(
          grpc::string(request.response_parameters(0).size(), '\0'));
      write_success = stream->Write(response);
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
  std::shared_ptr<ServerCredentials> creds = grpc::InsecureServerCredentials();
  if (FLAGS_enable_ssl) {
    SslServerCredentialsOptions ssl_opts = {
        "", {{test_server1_key, test_server1_cert}}};
    creds = grpc::SslServerCredentials(ssl_opts);
  }
  builder.AddPort(server_address.str(), creds);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  gpr_log(GPR_INFO, "Server listening on %s", server_address.str().c_str());
  while (!got_sigint) {
    sleep(5);
  }
}

static void sigint_handler(int x) { got_sigint = true; }

int main(int argc, char** argv) {
  grpc_init();
  ParseCommandLineFlags(&argc, &argv, true);
  signal(SIGINT, sigint_handler);

  GPR_ASSERT(FLAGS_port != 0);
  RunServer();

  grpc_shutdown();
  return 0;
}
