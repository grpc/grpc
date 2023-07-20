// Copyright 2023 gRPC authors.
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

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

#include <grpc/support/time.h>
#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/server_callback.h>

#include "src/core/lib/experiments/config.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

// A stress test which spins up a server with a small configured resource quota
// value. It then creates many channels which exchange large payloads with the
// server. This would drive the server to reach resource quota limits and
// trigger reclamation.

namespace grpc {
namespace testing {
namespace {
constexpr int kResourceQuotaSizeBytes = 1024 * 1024;
constexpr int kPayloadSizeBytes = 1024 * 1024;
constexpr int kNumParallelChannels = 10;
}  // namespace

class EchoClientUnaryReactor : public grpc::ClientUnaryReactor {
 public:
  EchoClientUnaryReactor(ClientContext* ctx, EchoTestService::Stub* stub,
                         const std::string payload, Status* status)
      : ctx_(ctx), payload_(payload), status_(status) {
    ctx_->set_wait_for_ready(true);
    request_.set_message(payload);
    stub->async()->Echo(ctx_, &request_, &response_, this);
    StartCall();
  }

  void Await() { notification_.WaitForNotification(); }

 protected:
  void OnReadInitialMetadataDone(bool /*ok*/) override {}

  void OnDone(const Status& s) override {
    *status_ = s;
    notification_.Notify();
  }

 private:
  ClientContext* const ctx_;
  EchoRequest request_;
  EchoResponse response_;
  const std::string payload_;
  grpc_core::Notification notification_;
  Status* const status_;
};

class EchoServerUnaryReactor : public ServerUnaryReactor {
 public:
  EchoServerUnaryReactor(CallbackServerContext* /*ctx*/,
                         const EchoRequest* request, EchoResponse* response) {
    response->set_message(request->message());
    Finish(grpc::Status::OK);
  }

 private:
  void OnDone() override { delete this; }
};

class GrpcCallbackServiceImpl : public EchoTestService::CallbackService {
 public:
  ServerUnaryReactor* Echo(CallbackServerContext* context,
                           const EchoRequest* request,
                           EchoResponse* response) override {
    return new EchoServerUnaryReactor(context, request, response);
  }
};

class End2EndResourceQuotaUnaryTest : public ::testing::Test {
 protected:
  End2EndResourceQuotaUnaryTest() {
    int port = grpc_pick_unused_port_or_die();
    server_address_ = absl::StrCat("localhost:", port);
    payload_ = std::string(kPayloadSizeBytes, 'a');
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, InsecureServerCredentials());
    builder.SetResourceQuota(
        grpc::ResourceQuota("TestService").Resize(kResourceQuotaSizeBytes));
    builder.RegisterService(&grpc_service_);
    server_ = builder.BuildAndStart();
  }

  ~End2EndResourceQuotaUnaryTest() override { server_->Shutdown(); }

  void MakeGrpcCall() {
    ClientContext ctx;
    Status status;
    auto stub = EchoTestService::NewStub(
        CreateChannel(server_address_, grpc::InsecureChannelCredentials()));
    EchoClientUnaryReactor reactor(&ctx, stub.get(), payload_, &status);
    reactor.Await();
  }

  void MakeGrpcCalls() {
    std::vector<std::thread> workers;
    workers.reserve(kNumParallelChannels);
    // Run MakeGrpcCall() many times concurrently.
    for (int i = 0; i < kNumParallelChannels; ++i) {
      workers.emplace_back([this]() { MakeGrpcCall(); });
    }
    for (int i = 0; i < kNumParallelChannels; ++i) {
      workers[i].join();
    }
  }

  int port_;
  std::unique_ptr<Server> server_;
  string server_address_;
  GrpcCallbackServiceImpl grpc_service_;
  std::string payload_;
};

TEST_F(End2EndResourceQuotaUnaryTest, MultipleUnaryRPCTest) { MakeGrpcCalls(); }

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
