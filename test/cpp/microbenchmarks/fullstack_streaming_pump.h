/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef TEST_CPP_MICROBENCHMARKS_FULLSTACK_STREAMING_PUMP_H
#define TEST_CPP_MICROBENCHMARKS_FULLSTACK_STREAMING_PUMP_H

#include <benchmark/benchmark.h>
#include <sstream>
#include "src/core/lib/profiling/timers.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/microbenchmarks/fullstack_context_mutators.h"
#include "test/cpp/microbenchmarks/fullstack_fixtures.h"

namespace grpc {
namespace testing {

/*******************************************************************************
 * BENCHMARKING KERNELS
 */

static void* tag(intptr_t x) { return reinterpret_cast<void*>(x); }

template <class Fixture>
static void BM_PumpStreamClientToServer(benchmark::State& state) {
  EchoTestService::AsyncService service;
  std::unique_ptr<Fixture> fixture(new Fixture(&service));
  {
    EchoRequest send_request;
    EchoRequest recv_request;
    if (state.range(0) > 0) {
      send_request.set_message(std::string(state.range(0), 'a'));
    }
    Status recv_status;
    ServerContext svr_ctx;
    ServerAsyncReaderWriter<EchoResponse, EchoRequest> response_rw(&svr_ctx);
    service.RequestBidiStream(&svr_ctx, &response_rw, fixture->cq(),
                              fixture->cq(), tag(0));
    std::unique_ptr<EchoTestService::Stub> stub(
        EchoTestService::NewStub(fixture->channel()));
    ClientContext cli_ctx;
    auto request_rw = stub->AsyncBidiStream(&cli_ctx, fixture->cq(), tag(1));
    int need_tags = (1 << 0) | (1 << 1);
    void* t;
    bool ok;
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      GPR_ASSERT(ok);
      int i = static_cast<int>((intptr_t)t);
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
    response_rw.Read(&recv_request, tag(0));
    while (state.KeepRunning()) {
      GPR_TIMER_SCOPE("BenchmarkCycle", 0);
      request_rw->Write(send_request, tag(1));
      while (true) {
        GPR_ASSERT(fixture->cq()->Next(&t, &ok));
        if (t == tag(0)) {
          response_rw.Read(&recv_request, tag(0));
        } else if (t == tag(1)) {
          break;
        } else {
          GPR_ASSERT(false);
        }
      }
    }
    request_rw->WritesDone(tag(1));
    need_tags = (1 << 0) | (1 << 1);
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      int i = static_cast<int>((intptr_t)t);
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
    response_rw.Finish(Status::OK, tag(0));
    Status final_status;
    request_rw->Finish(&final_status, tag(1));
    need_tags = (1 << 0) | (1 << 1);
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      int i = static_cast<int>((intptr_t)t);
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
    GPR_ASSERT(final_status.ok());
  }
  fixture->Finish(state);
  fixture.reset();
  state.SetBytesProcessed(state.range(0) * state.iterations());
}

template <class Fixture>
static void BM_PumpStreamServerToClient(benchmark::State& state) {
  EchoTestService::AsyncService service;
  std::unique_ptr<Fixture> fixture(new Fixture(&service));
  {
    EchoResponse send_response;
    EchoResponse recv_response;
    if (state.range(0) > 0) {
      send_response.set_message(std::string(state.range(0), 'a'));
    }
    Status recv_status;
    ServerContext svr_ctx;
    ServerAsyncReaderWriter<EchoResponse, EchoRequest> response_rw(&svr_ctx);
    service.RequestBidiStream(&svr_ctx, &response_rw, fixture->cq(),
                              fixture->cq(), tag(0));
    std::unique_ptr<EchoTestService::Stub> stub(
        EchoTestService::NewStub(fixture->channel()));
    ClientContext cli_ctx;
    auto request_rw = stub->AsyncBidiStream(&cli_ctx, fixture->cq(), tag(1));
    int need_tags = (1 << 0) | (1 << 1);
    void* t;
    bool ok;
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      GPR_ASSERT(ok);
      int i = static_cast<int>((intptr_t)t);
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
    request_rw->Read(&recv_response, tag(0));
    while (state.KeepRunning()) {
      GPR_TIMER_SCOPE("BenchmarkCycle", 0);
      response_rw.Write(send_response, tag(1));
      while (true) {
        GPR_ASSERT(fixture->cq()->Next(&t, &ok));
        if (t == tag(0)) {
          request_rw->Read(&recv_response, tag(0));
        } else if (t == tag(1)) {
          break;
        } else {
          GPR_ASSERT(false);
        }
      }
    }
    response_rw.Finish(Status::OK, tag(1));
    need_tags = (1 << 0) | (1 << 1);
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      int i = static_cast<int>((intptr_t)t);
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
  }
  fixture->Finish(state);
  fixture.reset();
  state.SetBytesProcessed(state.range(0) * state.iterations());
}
}  // namespace testing
}  // namespace grpc

#endif  // TEST_CPP_MICROBENCHMARKS_FULLSTACK_FIXTURES_H
