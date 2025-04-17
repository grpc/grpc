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

#include <grpc/grpc.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>

#include <cstddef>
#include <ostream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");

using grpc::CallbackServerContext;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerUnaryReactor;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

namespace {

// Sends requests as quickly as possible and times how long it takes to perform
// the write operation.
class GreeterClientReactor final
    : public grpc::ClientBidiReactor<helloworld::HelloRequest,
                                     helloworld::HelloReply> {
 public:
  explicit GreeterClientReactor(int reqs, size_t req_size) : reqs_(reqs) {
    req_.set_name(std::string(req_size, '*'));
  }

  void Start() {
    absl::MutexLock lock(&mu_);
    StartCall();
    Write();
  }

  ~GreeterClientReactor() override {
    absl::MutexLock lock(&mu_);
    mu_.Await(absl::Condition(+[](bool* done) { return *done; }, &done_));
  }

  void OnWriteDone(bool ok) override {
    absl::MutexLock lock(&mu_);
    std::cout << "Writing took " << absl::Now() - *time_ << std::endl;
    time_ = std::nullopt;
    if (ok) {
      Write();
    }
  }

  void OnDone(const grpc::Status& status) override {
    if (status.ok()) {
      std::cout << "Done\n";
    } else {
      std::cout << "Done with error: [" << status.error_code() << "] "
                << status.error_message() << "\n";
    }
    absl::MutexLock lock(&mu_);
    done_ = true;
  }

 private:
  void Write() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
    if (reqs_ == 0) {
      StartWritesDone();
      return;
    }
    --reqs_;
    StartWrite(&req_);
    time_ = absl::Now();
  }

  absl::Mutex mu_;
  bool done_ ABSL_GUARDED_BY(&mu_) = false;
  HelloRequest req_;
  size_t reqs_;
  std::optional<absl::Time> time_ ABSL_GUARDED_BY(mu_);
};

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  grpc::ChannelArguments channel_arguments;
  auto channel = grpc::CreateCustomChannel(absl::GetFlag(FLAGS_target),
                                           grpc::InsecureChannelCredentials(),
                                           channel_arguments);
  auto stub = Greeter::NewStub(channel);
  // Send 10 requests with 3Mb payload. This will eventually fill the buffer
  // and make
  GreeterClientReactor reactor(10, 3 * 1024 * 1024);
  grpc::ClientContext context;
  stub->async()->SayHelloBidiStream(&context, &reactor);
  reactor.Start();
  return 0;
}
