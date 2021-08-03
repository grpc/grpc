/*
 *
 * Copyright 2015 gRPC authors.
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

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

//my include
#include <grpc/support/port_platform.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <openssl/ssl.h>

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"
//done

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;
using ::grpc::experimental::TlsServerAuthorizationCheckArg;
using ::grpc::experimental::TlsServerAuthorizationCheckConfig;
using ::grpc::experimental::TlsServerAuthorizationCheckInterface;

class TestTlsServerAuthorizationCheck
    : public TlsServerAuthorizationCheckInterface {
  int Schedule(TlsServerAuthorizationCheckArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    std::string cb_user_data = "cb_user_data";
    arg->set_cb_user_data(static_cast<void*>(gpr_strdup(cb_user_data.c_str())));
    arg->set_success(1);
    arg->set_target_name("sync_target_name");
    arg->set_peer_cert("sync_peer_cert");
    arg->set_status(GRPC_STATUS_OK);
    arg->set_error_details("sync_error_details");
    return 1;
  }

  void Cancel(TlsServerAuthorizationCheckArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    arg->set_status(GRPC_STATUS_PERMISSION_DENIED);
    arg->set_error_details("cancelled");
  }
};

// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    std::string prefix("Hello ");
    reply->set_message(prefix + request->name());
    return Status::OK;
  }

  Status SayHelloAgain(ServerContext* context, const HelloRequest* request,
                       HelloReply* reply) override {
    std::string prefix("Hello again ");
    reply->set_message(prefix + request->name());
    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  GreeterServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;

  constexpr const char* kCertName = "cert_name";
  constexpr const char* kRootCertName = "root_cert_name";
  constexpr const char* kIdentityCertName = "identity_cert_name";
//  auto certificate_provider = std::make_shared<grpc::experimental::FileWatcherCertificateProvider>(
//      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  grpc::experimental::TlsServerCredentialsOptions options(nullptr);
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  options.set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  //ServerBuilder builder;
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
//builder.AddListeningPort("[::]:<port>", server_credentials);
//std::unique_ptr<Server> server(builder.BuildAndStart());

  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, server_credentials);
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();

  return 0;
}
