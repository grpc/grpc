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

#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif
#include "greeter_utils.h"

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;

using ::helloworld::Greeter;
using ::helloworld::HelloReply;
using ::helloworld::HelloRequest;

class GreeterClient {
 public:
  GreeterClient(const std::string& cert, const std::string& key,
                const std::string& root, const std::string& server) {
    grpc::SslCredentialsOptions opts = {root, key, cert};

    stub_ = Greeter::NewStub(
        grpc::CreateChannel(server, grpc::SslCredentials(opts)));
  }

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::string say_hello(const std::string& user) {
    // Data we are sending to the server.
    HelloRequest request;
    request.set_name(user);

    // Container for the data we expect from the server.
    HelloReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = stub_->SayHello(&context, request, &reply);

    std::string msg{"RPC failed"};

    // Act upon its status.
    if (status.ok()) {
      msg = reply.message();
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
    }

    return msg;
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

int main(int, char**) {
  std::string server{"localhost:50051"};

  try {
    // Use the gen_certs.sh for the generation of required certificates
    std::string cert, key, root;

    read("client.crt", cert);
    read("client.key", key);
    read("ca.crt", root);

    GreeterClient greeter{cert, key, root, server};

    std::string reply = greeter.say_hello("world");

    std::cout << "Greeter received: " << reply << std::endl;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
  }

  return 0;
}
