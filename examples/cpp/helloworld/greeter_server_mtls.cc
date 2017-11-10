/*
 *
 * Copyright 2017 gRPC authors.
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
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <grpc++/grpc++.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;

// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    grpc::string prefix("Hello ");
    reply->set_message(prefix + request->name());
    return Status::OK;
  }
};

grpc::string LoadFromFile(const grpc::string& path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::cerr << "Unable to open file: " << path << std::endl;
    return "";
  }

  std::stringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

void RunServer() {
  grpc::string server_address("0.0.0.0:50051");
  GreeterServiceImpl service;

  const grpc::string kServerKey =
      "../../../src/core/tsi/test_creds/server0.key";
  const grpc::string kServerCert =
      "../../../src/core/tsi/test_creds/server0.pem";
  const grpc::string kRootCA = "../../../src/core/tsi/test_creds/ca.pem";

  grpc::SslServerCredentialsOptions::PemKeyCertPair server_cert_pair;
  server_cert_pair.private_key = LoadFromFile(kServerKey);
  server_cert_pair.cert_chain = LoadFromFile(kServerCert);

  grpc::SslServerCredentialsOptions options = grpc::SslServerCredentialsOptions(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  options.pem_root_certs = LoadFromFile(kRootCA);
  options.pem_key_cert_pairs.push_back(server_cert_pair);

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::SslServerCredentials(options));
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();

  return 0;
}
