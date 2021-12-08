// Copyright 2022 gRPC authors.
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

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "helloworld.grpc.pb.h"

#include <grpc++/grpc++.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

class GreeterServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    std::string prefix("Hello ");

    reply->set_message(prefix + request->name());

    return Status::OK;
  }
};

void read(const std::string& filename, std::string& data) {
  std::ifstream file(filename.c_str(), std::ios::in);

  if (file.is_open()) {
    std::stringstream ss;
    ss << file.rdbuf();

    file.close();

    data = ss.str();
  }

  return;
}

void runServer() {
  /**
   * [!] Be carefull here using one cert with the CN != localhost. [!]
   **/
  std::string server_address("localhost:50051");

  std::string key;
  std::string cert;
  std::string root;

  read("server.crt", cert);
  read("server.key", key);
  read("ca.crt", root);

  ServerBuilder builder;

  grpc::SslServerCredentialsOptions::PemKeyCertPair keycert = {key, cert};

  grpc::SslServerCredentialsOptions sslOps;
  sslOps.pem_root_certs = root;
  sslOps.pem_key_cert_pairs.push_back(keycert);

  builder.AddListeningPort(server_address, grpc::SslServerCredentials(sslOps));

  GreeterServiceImpl service;
  builder.RegisterService(&service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  server->Wait();
}

int main(int argc, char** argv) {
  runServer();

  return 0;
}
