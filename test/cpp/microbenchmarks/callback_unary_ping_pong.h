/*
 *
 * Copyright 2019 gRPC authors.
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

/* Benchmark gRPC end2end in various configurations */

#ifndef TEST_CPP_MICROBENCHMARKS_CALLBACK_UNARY_PING_PONG_H
#define TEST_CPP_MICROBENCHMARKS_CALLBACK_UNARY_PING_PONG_H

#include <benchmark/benchmark.h>
#include <sstream>
#include "src/core/lib/profiling/timers.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/microbenchmarks/callback_test_service.h"
#include "test/cpp/microbenchmarks/fullstack_context_mutators.h"
#include "test/cpp/microbenchmarks/fullstack_fixtures.h"

namespace grpc {
namespace testing {

/*******************************************************************************
 * BENCHMARKING KERNELS
 */

template <class Fixture, class ClientContextMutator, class ServerContextMutator>
static void BM_CallbackUnaryPingPong(benchmark::State& state) {
  CallbackStreamingTestService service;
  std::unique_ptr<Fixture> fixture(new Fixture(&service));
  std::unique_ptr<EchoTestService::Stub> stub_(
      EchoTestService::NewStub(fixture->channel()));
  EchoRequest request;
  EchoResponse response;

  if (state.range(0) > 0) {
    request.set_message(std::string(state.range(0), 'a'));
  } else {
    request.set_message("");
  }
  if (state.range(1) > 0) {
    response.set_message(std::string(state.range(1), 'a'));
  } else {
    response.set_message("");
  }

  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    ClientContext cli_ctx;
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    stub_->experimental_async()->Echo(&cli_ctx, &request, &response,
                                      [&done, &mu, &cv](Status s) {
                                        GPR_ASSERT(s.ok());
                                        std::lock_guard<std::mutex> l(mu);
                                        done = true;
                                        cv.notify_one();
                                      });
    std::unique_lock<std::mutex> l(mu);
    while (!done) {
      cv.wait(l);
    }
  }
  fixture->Finish(state);
  fixture.reset();
  state.SetBytesProcessed(state.range(0) * state.iterations() +
                          state.range(1) * state.iterations());
}
}  // namespace testing
}  // namespace grpc

#endif  // TEST_CPP_MICROBENCHMARKS_FULLSTACK_UNARY_PING_PONG_H
