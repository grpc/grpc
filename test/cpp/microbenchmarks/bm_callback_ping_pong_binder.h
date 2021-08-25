// Copyright 2021 gRPC authors.
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

#ifndef GRPC_TEST_CPP_MICROBENCHMARKS_BM_CALLBACK_PING_PONG_BINDER_H
#define GRPC_TEST_CPP_MICROBENCHMARKS_BM_CALLBACK_PING_PONG_BINDER_H

#include <grpc/impl/codegen/port_platform.h>

#ifdef GPR_ANDROID

#include <sstream>

#include <benchmark/benchmark.h>

#include "absl/time/time.h"

#include "src/core/ext/transport/binder/client/channel_create.h"
#include "src/core/ext/transport/binder/server/binder_server_credentials.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/microbenchmarks/callback_streaming_ping_pong.h"
#include "test/cpp/microbenchmarks/callback_unary_ping_pong.h"

namespace grpc {
namespace testing {

class BinderFixture {
 public:
  explicit BinderFixture(
      JNIEnv* env, jobject application,
      const FixtureConfiguration& config = FixtureConfiguration()) {
    ChannelArguments args;
    config.ApplyCommonChannelArguments(&args);
    channel_ = ::grpc::experimental::CreateCustomBinderChannel(env, application,
                                                               "", "", args);
  }

  std::shared_ptr<Channel> channel() { return channel_; }

 private:
  std::shared_ptr<Channel> channel_;
};

class CallbackPingPongBinderServer {
 public:
  explicit CallbackPingPongBinderServer(
      const std::string& binder_address,
      const FixtureConfiguration& config = FixtureConfiguration()) {
    ServerBuilder b;
    if (!binder_address.empty()) {
      b.AddListeningPort(binder_address,
                         experimental::BinderServerCredentials());
    }
    cq_ = b.AddCompletionQueue(true);
    b.RegisterService(&service_);
    config.ApplyCommonServerBuilderConfig(&b);
    server_ = b.BuildAndStart();
  }

 private:
  std::unique_ptr<ServerCompletionQueue> cq_;
  std::unique_ptr<Server> server_;
  CallbackStreamingTestService service_;
};

static void BM_CallbackUnaryPingPongBinder(benchmark::State& state, JNIEnv* env,
                                           jobject application) {
  std::unique_ptr<BinderFixture> fixture(new BinderFixture(env, application));
  std::unique_ptr<EchoTestService::Stub> stub(
      EchoTestService::NewStub(fixture->channel()));
  EchoRequest request;
  EchoResponse response;
  ClientContext cli_ctx;

  if (state.range(0) > 0) {
    request.set_message(std::string(state.range(0), 'a'));
  } else {
    request.set_message("");
  }

  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  if (state.KeepRunning()) {
    SendCallbackUnaryPingPong(&state, &cli_ctx, &request, &response, stub.get(),
                              &done, &mu, &cv);
  }
  std::unique_lock<std::mutex> l(mu);
  while (!done) {
    cv.wait(l);
  }
  gpr_log(GPR_ERROR, "Done");
  fixture.reset();
  state.SetBytesProcessed(state.range(0) * state.iterations() +
                          state.range(1) * state.iterations());
}

static void BM_CallbackBidiStreamingBinder(benchmark::State& state, JNIEnv* env,
                                           jobject application) {
  int message_size = state.range(0);
  int max_ping_pongs = state.range(1);
  CallbackStreamingTestService service;
  std::unique_ptr<BinderFixture> fixture(new BinderFixture(env, application));
  std::unique_ptr<EchoTestService::Stub> stub_(
      EchoTestService::NewStub(fixture->channel()));
  EchoRequest request;
  EchoResponse response;
  ClientContext cli_ctx;
  if (message_size > 0) {
    request.set_message(std::string(message_size, 'a'));
  } else {
    request.set_message("");
  }
  if (state.KeepRunning()) {
    BidiClient test{&state, stub_.get(), &cli_ctx, &request, &response};
    test.Await();
  }
  fixture.reset();
  state.SetBytesProcessed(2 * message_size * max_ping_pongs *
                          state.iterations());
}

void RunCallbackPingPongBinderBenchmarks(JNIEnv* env, jobject application);

}  // namespace testing
}  // namespace grpc

#endif  // GPR_ANDROID
#endif  // GRPC_TEST_CPP_MICROBENCHMARKS_BM_CALLBACK_PING_PONG_H
