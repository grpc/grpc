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
#include <string>
#include <sstream>

#include <grpc++/grpc++.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::string SayHello(const std::string& user) {
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

    // Act upon its status.
    if (status.ok()) {
      return reply.message();
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return "RPC failed";
    }
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

grpc::string LoadFromFile(grpc::string path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    std::cerr << "Unable to open file: " << path << std::endl;
    return "";
  }

  std::stringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

int main(int argc, char** argv) {
  const grpc::string kRootCA = "../../../src/core/tsi/test_creds/ca.pem";
  const grpc::string kClientCert =
      "../../../src/core/tsi/test_creds/client.pem";
  const grpc::string kClientKey =
      "../../../src/core/tsi/test_creds/client.key";
   grpc::SslCredentialsOptions options;
   options.pem_root_certs = LoadFromFile(kRootCA);
   options.pem_private_key = LoadFromFile(kClientKey);
   options.pem_cert_chain = LoadFromFile(kClientCert);

   // Set target override. This is only necessary since we're our server is
   // using "fake" credentials for testing purposes.
   grpc::ChannelArguments args;
   args.SetSslTargetNameOverride("foo.test.google.com.au");

  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint (in this case,
  // localhost at port 50051).
  //
  // Use a custom channel since we have to override the SSL target. This would
  // not be necessary for a production server using "real" credentials. If we
  // had a server using "real" credentials, we could create a channel like so:
  //   GreeterClient greeter(grpc::CreateChannel(
  //       "foo.test.google.com.au:50051", grpc::SslCredentials(options)));
  GreeterClient greeter(grpc::CreateCustomChannel(
      "localhost:50051", grpc::SslCredentials(options), args));
  std::string user("world");
  std::string reply = greeter.SayHello(user);
  std::cout << "Greeter received: " << reply << std::endl;

  return 0;
}
