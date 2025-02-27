//
//
// Copyright 2017 gRPC authors.
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
//
//

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/config.h>

#include <thread>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "gtest/gtest.h"
#include "src/core/util/crash.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace {

TEST(ServerRequestCallTest, ShortDeadlineDoesNotCauseOkayFalse) {
  std::mutex mu;
  bool shutting_down = false;

  // grpc server config.
  std::ostringstream s;
  int p = grpc_pick_unused_port_or_die();
  s << "[::1]:" << p;
  const string address = s.str();
  testing::EchoTestService::AsyncService service;
  ServerBuilder builder;
  builder.AddListeningPort(address, InsecureServerCredentials());
  auto cq = builder.AddCompletionQueue();
  builder.RegisterService(&service);
  auto server = builder.BuildAndStart();

  // server thread.
  std::thread t([address, &service, &cq, &mu, &shutting_down] {
    for (int n = 0; true; n++) {
      ServerContext ctx;
      testing::EchoRequest req;
      ServerAsyncResponseWriter<testing::EchoResponse> responder(&ctx);

      // if shutting down, don't enqueue a new request.
      {
        std::lock_guard<std::mutex> lock(mu);
        if (!shutting_down) {
          service.RequestEcho(&ctx, &req, &responder, cq.get(), cq.get(),
                              reinterpret_cast<void*>(1));
        }
      }

      bool ok;
      void* tag;
      if (!cq->Next(&tag, &ok)) {
        break;
      }

      EXPECT_EQ((void*)1, tag);
      // If not shutting down, ok must be true for new requests.
      {
        std::lock_guard<std::mutex> lock(mu);
        if (!shutting_down && !ok) {
          grpc_core::Crash(absl::StrFormat("!ok on request %d", n));
        }
        if (shutting_down && !ok) {
          // Failed connection due to shutdown, continue flushing the CQ.
          continue;
        }
      }

      // Send a simple response after a small delay that would ensure the client
      // deadline is exceeded.
      LOG(INFO) << "Got request " << n;
      testing::EchoResponse response;
      response.set_message("foobar");
      // A bit of sleep to make sure the deadline elapses.
      gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                   gpr_time_from_seconds(1, GPR_TIMESPAN)));
      {
        std::lock_guard<std::mutex> lock(mu);
        if (shutting_down) {
          LOG(INFO) << "shut down while processing call, not calling Finish()";
          // Continue flushing the CQ.
          continue;
        }
        LOG(INFO) << "Finishing request " << n;
        responder.Finish(response, grpc::Status::OK,
                         reinterpret_cast<void*>(2));
        if (!cq->Next(&tag, &ok)) {
          break;
        }
        EXPECT_EQ((void*)2, tag);
      }
    }
  });

  auto stub = testing::EchoTestService::NewStub(
      grpc::CreateChannel(address, InsecureChannelCredentials()));

  for (int i = 0; i < 100; i++) {
    LOG(INFO) << "Sending " << i;
    testing::EchoRequest request;

    /////////
    // Comment out the following line to get ok=false due to invalid request.
    // Otherwise, ok=false due to deadline being exceeded.
    /////////
    request.set_message("foobar");

    // A simple request with a short deadline. The server will always exceed the
    // deadline, whether due to the sleep or because the server was unable to
    // even fetch the request from the CQ before the deadline elapsed.
    testing::EchoResponse response;
    grpc::ClientContext ctx;
    ctx.set_fail_fast(false);
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::milliseconds(1));
    grpc::Status status = stub->Echo(&ctx, request, &response);
    EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, status.error_code());
    LOG(INFO) << "Success.";
  }
  LOG(INFO) << "Done sending RPCs.";

  // Shut down everything properly.
  LOG(INFO) << "Shutting down.";
  {
    std::lock_guard<std::mutex> lock(mu);
    shutting_down = true;
  }
  server->Shutdown();
  cq->Shutdown();
  server->Wait();

  t.join();
}

void ServerFunction(ServerCompletionQueue* cq, std::atomic_bool* shutdown) {
  for (;;) {
    bool ok;
    void* tag;
    if (!cq->Next(&tag, &ok)) {
      break;
    }
    if (shutdown->load()) {
      break;
    }
    // For UnimplementedAsyncRequest, the server handles it internally and never
    // returns from Next except when shutdown.
    grpc_core::Crash("unreached");
  }
}

void ClientFunction(testing::UnimplementedEchoService::Stub* stub) {
  constexpr int kNumRpcPerThreads = 5000;
  for (int i = 0; i < kNumRpcPerThreads; i++) {
    testing::EchoRequest request;
    request.set_message("foobar");
    testing::EchoResponse response;
    grpc::ClientContext ctx;
    grpc::Status status = stub->Unimplemented(&ctx, request, &response);
    EXPECT_EQ(StatusCode::UNIMPLEMENTED, status.error_code());
  }
}

TEST(ServerRequestCallTest, MultithreadedUnimplementedService) {
  std::atomic_bool shutdown(false);
  // grpc server config.
  std::ostringstream s;
  int p = grpc_pick_unused_port_or_die();
  s << "[::1]:" << p;
  const string address = s.str();
  testing::EchoTestService::AsyncService service;
  ServerBuilder builder;
  builder.AddListeningPort(address, InsecureServerCredentials());
  auto cq = builder.AddCompletionQueue();
  builder.RegisterService(&service);
  auto server = builder.BuildAndStart();

  ServerContext ctx;
  testing::EchoRequest req;
  ServerAsyncResponseWriter<testing::EchoResponse> responder(&ctx);
  service.RequestEcho(&ctx, &req, &responder, cq.get(), cq.get(),
                      reinterpret_cast<void*>(1));

  // server threads
  constexpr int kNumServerThreads = 2;
  std::vector<std::thread> server_threads;
  server_threads.reserve(kNumServerThreads);
  for (int i = 0; i < kNumServerThreads; i++) {
    server_threads.emplace_back(ServerFunction, cq.get(), &shutdown);
  }

  auto stub = testing::UnimplementedEchoService::NewStub(
      grpc::CreateChannel(address, InsecureChannelCredentials()));

  // client threads
  constexpr int kNumClientThreads = 2;
  std::vector<std::thread> client_threads;
  client_threads.reserve(kNumClientThreads);
  for (int i = 0; i < kNumClientThreads; i++) {
    client_threads.emplace_back(ClientFunction, stub.get());
  }
  for (auto& t : client_threads) {
    t.join();
  }

  // Shut down everything properly.
  LOG(INFO) << "Shutting down.";
  shutdown.store(true);
  server->Shutdown();
  cq->Shutdown();
  server->Wait();

  for (auto& t : server_threads) {
    t.join();
  }
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
