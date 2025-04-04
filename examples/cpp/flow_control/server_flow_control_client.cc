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

#include <iostream>
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
ABSL_FLAG(size_t, quota, 20,
          "Resource quota (in megabytes) that defines how much memory gRPC has "
          "available for buffers");

namespace {

class Reader final : public grpc::ClientReadReactor<helloworld::HelloReply> {
 public:
  void Start() {
    StartRead(&res_);
    StartCall();
  }

  grpc::Status WaitForDone() {
    absl::MutexLock lock(&mu_);
    mu_.Await(absl::Condition(
        +[](Reader* reader) { return reader->result_.has_value(); }, this));
    return *result_;
  }

  void OnReadDone(bool ok) override {
    if (!ok) {
      std::cout << "Done reading\n";
      return;
    }
    std::cout << "Read " << res_.message().length() << " bytes.\n";
    res_.set_message("");
    // A delay to slow down the client so it can't read responses quick enough
    sleep(1);
    StartRead(&res_);
  }

  void OnDone(const grpc::Status& status) override {
    absl::MutexLock lock(&mu_);
    result_ = status;
  }

 private:
  absl::Mutex mu_;
  std::optional<grpc::Status> result_;
  helloworld::HelloReply res_;
};

}  // namespace

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  grpc::ChannelArguments channel_arguments;
  grpc::ResourceQuota quota;
  quota.Resize(absl::GetFlag(FLAGS_quota) * 1024 * 1024);
  channel_arguments.SetResourceQuota(quota);
  auto channel = grpc::CreateCustomChannel(absl::GetFlag(FLAGS_target),
                                           grpc::InsecureChannelCredentials(),
                                           channel_arguments);
  auto greeter = helloworld::Greeter::NewStub(channel);
  grpc::ClientContext ctx;
  helloworld::HelloRequest req;
  req.set_name("World");
  Reader reader;
  greeter->async()->SayHelloStreamReply(&ctx, &req, &reader);
  reader.Start();
  auto status = reader.WaitForDone();
  if (status.ok()) {
    std::cout << "Success\n";
  } else {
    std::cerr << "Failed with error: " << status.error_message() << "\n";
  }
  return 0;
}
