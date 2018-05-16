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

#include <thread>

#include <grpcpp/impl/codegen/config.h>

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <grpc/support/log.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"

#include <gtest/gtest.h>

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
                              (void*)1);
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
          gpr_log(GPR_INFO, "!ok on request %d", n);
          abort();
        }
        if (shutting_down && !ok) {
          // Failed connection due to shutdown, continue flushing the CQ.
          continue;
        }
      }

      // Send a simple response after a small delay that would ensure the client
      // deadline is exceeded.
      gpr_log(GPR_INFO, "Got request %d", n);
      testing::EchoResponse response;
      response.set_message("foobar");
      // A bit of sleep to make sure the deadline elapses.
      gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                   gpr_time_from_millis(50, GPR_TIMESPAN)));
      {
        std::lock_guard<std::mutex> lock(mu);
        if (shutting_down) {
          gpr_log(GPR_INFO,
                  "shut down while processing call, not calling Finish()");
          // Continue flushing the CQ.
          continue;
        }
        gpr_log(GPR_INFO, "Finishing request %d", n);
        responder.Finish(response, grpc::Status::OK, (void*)2);
        if (!cq->Next(&tag, &ok)) {
          break;
        }
        EXPECT_EQ((void*)2, tag);
      }
    }
  });

  auto stub = testing::EchoTestService::NewStub(
      CreateChannel(address, InsecureChannelCredentials()));

  for (int i = 0; i < 100; i++) {
    gpr_log(GPR_INFO, "Sending %d.", i);
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
    ::grpc::ClientContext ctx;
    ctx.set_fail_fast(false);
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::milliseconds(1));
    grpc::Status status = stub->Echo(&ctx, request, &response);
    EXPECT_EQ(DEADLINE_EXCEEDED, status.error_code());
    gpr_log(GPR_INFO, "Success.");
  }
  gpr_log(GPR_INFO, "Done sending RPCs.");

  // Shut down everything properly.
  gpr_log(GPR_INFO, "Shutting down.");
  {
    std::lock_guard<std::mutex> lock(mu);
    shutting_down = true;
  }
  server->Shutdown();
  cq->Shutdown();
  server->Wait();

  t.join();
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
