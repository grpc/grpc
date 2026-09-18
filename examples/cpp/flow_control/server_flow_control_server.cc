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
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/support/status.h>

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/strings/str_cat.h"

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");
ABSL_FLAG(size_t, message_size, 3 * 1024 * 1024,
          "Size of the messages to send");
ABSL_FLAG(uint32_t, to_send, 10,
          "Messages to send in response to a single request");

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerUnaryReactor;
using grpc::Status;

namespace {

//
// Will write the replies as fast as it can, starting a new write as soon as
// previous one is done.
//
class HelloReactor final
    : public grpc::ServerWriteReactor<helloworld::HelloReply> {
 public:
  HelloReactor(size_t message_size, size_t to_send)
      : messages_to_send_(to_send) {
    res_.set_message(std::string(message_size, '#'));
    Write();
  }

  void Write() {
    absl::MutexLock lock(&mu_);
    StartWrite(&res_);
    --messages_to_send_;
    write_start_time_ = absl::Now();
  }

  void OnWriteDone(bool ok) override {
    bool more = false;
    {
      absl::MutexLock lock(&mu_);
      std::cout << "Write #" << messages_to_send_ << " done (Ok: " << ok
                << "): " << absl::Now() - *write_start_time_ << "\n";
      write_start_time_ = std::nullopt;
      more = ok && messages_to_send_ > 0;
    }
    if (more) {
      Write();
    } else {
      Finish(grpc::Status::OK);
      std::cout << "Done sending messages\n";
    }
  }

  void OnDone() override { delete this; }

 private:
  helloworld::HelloReply res_;
  size_t messages_to_send_;
  std::optional<absl::Time> write_start_time_;
  absl::Mutex mu_;
};

class GreeterService final : public helloworld::Greeter::CallbackService {
 public:
  GreeterService(size_t message_size, size_t to_send)
      : message_size_(message_size), to_send_(to_send) {}

  grpc::ServerWriteReactor<helloworld::HelloReply>* SayHelloStreamReply(
      grpc::CallbackServerContext* /*context*/,
      const helloworld::HelloRequest* request) override {
    return new HelloReactor(message_size_, to_send_);
  }

 private:
  size_t message_size_;
  size_t to_send_;
};

}  // namespace

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  std::string server_address =
      absl::StrCat("0.0.0.0:", absl::GetFlag(FLAGS_port));
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  GreeterService service(absl::GetFlag(FLAGS_message_size),
                         absl::GetFlag(FLAGS_to_send));
  ServerBuilder builder;
  builder.RegisterService(&service);
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
  return 0;
}
