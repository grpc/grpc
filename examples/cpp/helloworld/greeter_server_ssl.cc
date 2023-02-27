/*
 *
 * Copyright 2022 gRPC authors.
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

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif
#include "greeter_utils.h"

using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::helloworld::Greeter;
using ::helloworld::HelloReply;
using ::helloworld::HelloRequest;

class GreeterServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    reply->set_message("Hello " + request->name());

    return Status::OK;
  }
};

void run_server() {
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  // Use the gen_certs.sh for the generation of required certificates
  // [!] Be carefull here using a server.crt with the CN != localhost [!]
  std::string cert = read_file("client.crt");
  std::string key = read_file("client.key");
  std::string root = read_file("ca.crt");

  // Configure SSL options
  grpc::SslServerCredentialsOptions::PemKeyCertPair keycert = {key, cert};
  grpc::SslServerCredentialsOptions sslOps;
  sslOps.pem_root_certs = root;
  sslOps.pem_key_cert_pairs.push_back(keycert);

  ServerBuilder builder;

  // Listen on the given address.
  std::string server_address{"localhost:50051"};
  builder.AddListeningPort(server_address, grpc::SslServerCredentials(sslOps));

  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  GreeterServiceImpl service;
  builder.RegisterService(&service);

  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int, char**) {
  try {
    run_server();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
  }

  return 0;
}
