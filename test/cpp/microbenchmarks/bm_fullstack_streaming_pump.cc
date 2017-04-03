/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* Benchmark gRPC end2end in various configurations */

#include <benchmark/benchmark.h>
#include <sstream>
#include "src/core/lib/profiling/timers.h"
#include "src/cpp/client/create_channel_internal.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/microbenchmarks/fullstack_context_mutators.h"
#include "test/cpp/microbenchmarks/fullstack_fixtures.h"

namespace grpc {
namespace testing {

// force library initialization
auto& force_library_initialization = Library::get();

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
      int i = (int)(intptr_t)t;
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
      int i = (int)(intptr_t)t;
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
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
      int i = (int)(intptr_t)t;
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
      int i = (int)(intptr_t)t;
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
  }
  fixture->Finish(state);
  fixture.reset();
  state.SetBytesProcessed(state.range(0) * state.iterations());
}

/*******************************************************************************
 * CONFIGURATIONS
 */

BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, TCP)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, UDS)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, SockPair)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, InProcessCHTTP2)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, TCP)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, UDS)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, SockPair)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, InProcessCHTTP2)
    ->Range(0, 128 * 1024 * 1024);

}  // namespace testing
}  // namespace grpc

BENCHMARK_MAIN();
