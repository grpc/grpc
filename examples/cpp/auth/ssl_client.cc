/*
 *
 * Copyright 2024 gRPC authors.
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

#include <grpcpp/grpcpp.h>

#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "helper.h"
#include "src/core/credentials/call/call_credentials.h"

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

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
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    Status status;
    stub_->async()->SayHello(&context, &request, &reply,
                             [&mu, &cv, &done, &status](Status s) {
                               status = std::move(s);
                               std::lock_guard<std::mutex> lock(mu);
                               done = true;
                               cv.notify_one();
                             });

    std::unique_lock<std::mutex> lock(mu);
    while (!done) {
      cv.wait(lock);
    }

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

#ifdef BAZEL_BUILD
constexpr char kRootCertificate[] = "examples/cpp/auth/credentials/root.crt";
#else
constexpr char kRootCertificate[] = "credentials/root.crt";
#endif

std::string ReadFile(const std::string& path) {
    std::ifstream t(path);
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  std::string target_str =
      absl::StrFormat("localhost:%d", absl::GetFlag(FLAGS_port));
  // Build a SSL options for the channel
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = LoadStringFromFile(kRootCertificate);

  auto channel_creds = grpc::SslCredentials(ssl_options);
  
  // 1. Load the JSON key string
  // std::string json_key = ReadFile("/usr/local/google/home/mcastelaz/creds/service-acct-key.json");
  // std::string json_key = ReadFile("/usr/local/google/home/mcastelaz/azure_credentials.json");
  std::string json_key = ReadFile("/usr/local/google/home/mcastelaz/aws-credentials.json");
    // std::string json_key = ReadFile("/usr/local/google/home/mcastelaz/saml-credentials.json");
  // std::string json_key = ReadFile("/usr/local/google/home/mcastelaz/credentials.json");
  // std::string json_key = ReadFile("/usr/local/google/home/mcastelaz/.config/gcloud/application_default_credentials.json");


  std::vector<std::string> scopes;
  scopes.push_back("https://www.googleapis.com/auth/cloud-platform");
  // scopes.push_back("https://www.googleapis.com/auth/userinfo.email");

  // 2. Create Service Account JWT credentials
  // The second argument is the token lifetime (usually 3600 seconds)
  auto call_creds = grpc::ExternalAccountCredentials(json_key, scopes);
  // auto call_creds = grpc::AwsExternalAccountCredentials(json_key, scopes);
  // auto call_creds = grpc::ServiceAccountJWTAccessCredentials(json_key, 3600);
  // auto call_creds = grpc::GoogleComputeEngineCredentials();

  // 3. Combine them into Composite Credentials
  auto composite_creds = grpc::CompositeChannelCredentials(channel_creds, call_creds);

  // Create a channel with SSL credentials
  GreeterClient greeter(
      grpc::CreateChannel(target_str, composite_creds));
  std::string user("world");
  std::cout << "Maiking first request without location header" << std::endl;
  std::string reply = greeter.SayHello(user);
  std::cout << "Greeter received: " << reply << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));
  std::cout << "Maiking second request - should include header and fail initially but retry without header" << std::endl;
  reply = greeter.SayHello(user);
  std::cout << "Greeter received: " << reply << " in the second call " << std::endl;
  // std::this_thread::sleep_for(std::chrono::seconds(20));
  // std::cout << "Maiking third request - cooldown period expired so should trigger RAB" << std::endl;
  // reply = greeter.SayHello(user);
  // std::cout << "Greeter received: " << reply << " in the third call " << std::endl;
  // std::this_thread::sleep_for(std::chrono::seconds(25));
  //   std::cout << "Maiking fourth request - cooldown period doubled now so this should not trigger RAB" << std::endl;
  // reply = greeter.SayHello(user);
  // std::cout << "Greeter received: " << reply << " in the fourth call " << std::endl;
  // std::this_thread::sleep_for(std::chrono::seconds(90));
  // std::cout << "Maiking fourth request" << std::endl;
  // reply = greeter.SayHello(user);
  // std::cout << "Greeter received: " << reply << " in the fourth call " << std::endl;
  return 0;
}
