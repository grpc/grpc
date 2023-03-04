//
//
// Copyright 2016 gRPC authors.
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

// Benchmark gRPC end2end in various configurations

#ifndef GRPC_TEST_CPP_MICROBENCHMARKS_FULLSTACK_STREAMING_PUMP_H
#define GRPC_TEST_CPP_MICROBENCHMARKS_FULLSTACK_STREAMING_PUMP_H

#include <sstream>

#include <benchmark/benchmark.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/microbenchmarks/fullstack_context_mutators.h"
#include "test/cpp/microbenchmarks/fullstack_fixtures.h"

namespace grpc {
namespace testing {

//******************************************************************************
// BENCHMARKING KERNELS
//

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
                              fixture->cq(), CoreTestFixture::tag(0));
    std::unique_ptr<EchoTestService::Stub> stub(
        EchoTestService::NewStub(fixture->channel()));
    ClientContext cli_ctx;
    auto request_rw =
        stub->AsyncBidiStream(&cli_ctx, fixture->cq(), CoreTestFixture::tag(1));
    int need_tags = (1 << 0) | (1 << 1);
    void* t;
    bool ok;
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      GPR_ASSERT(ok);
      int i = static_cast<int>(reinterpret_cast<intptr_t>(t));
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
    response_rw.Read(&recv_request, CoreTestFixture::tag(0));
    for (auto _ : state) {
      request_rw->Write(send_request, CoreTestFixture::tag(1));
      while (true) {
        GPR_ASSERT(fixture->cq()->Next(&t, &ok));
        if (t == CoreTestFixture::tag(0)) {
          response_rw.Read(&recv_request, CoreTestFixture::tag(0));
        } else if (t == CoreTestFixture::tag(1)) {
          break;
        } else {
          grpc_core::Crash("unreachable");
        }
      }
    }
    request_rw->WritesDone(CoreTestFixture::tag(1));
    need_tags = (1 << 0) | (1 << 1);
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      int i = static_cast<int>(reinterpret_cast<intptr_t>(t));
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
    response_rw.Finish(Status::OK, CoreTestFixture::tag(0));
    Status final_status;
    request_rw->Finish(&final_status, CoreTestFixture::tag(1));
    need_tags = (1 << 0) | (1 << 1);
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      int i = static_cast<int>(reinterpret_cast<intptr_t>(t));
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
    GPR_ASSERT(final_status.ok());
  }
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
                              fixture->cq(), CoreTestFixture::tag(0));
    std::unique_ptr<EchoTestService::Stub> stub(
        EchoTestService::NewStub(fixture->channel()));
    ClientContext cli_ctx;
    auto request_rw =
        stub->AsyncBidiStream(&cli_ctx, fixture->cq(), CoreTestFixture::tag(1));
    int need_tags = (1 << 0) | (1 << 1);
    void* t;
    bool ok;
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      GPR_ASSERT(ok);
      int i = static_cast<int>(reinterpret_cast<intptr_t>(t));
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
    request_rw->Read(&recv_response, CoreTestFixture::tag(0));
    for (auto _ : state) {
      response_rw.Write(send_response, CoreTestFixture::tag(1));
      while (true) {
        GPR_ASSERT(fixture->cq()->Next(&t, &ok));
        if (t == CoreTestFixture::tag(0)) {
          request_rw->Read(&recv_response, CoreTestFixture::tag(0));
        } else if (t == CoreTestFixture::tag(1)) {
          break;
        } else {
          grpc_core::Crash("unreachable");
        }
      }
    }
    response_rw.Finish(Status::OK, CoreTestFixture::tag(1));
    need_tags = (1 << 0) | (1 << 1);
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      int i = static_cast<int>(reinterpret_cast<intptr_t>(t));
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
  }
  fixture.reset();
  state.SetBytesProcessed(state.range(0) * state.iterations());
}
}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_MICROBENCHMARKS_FULLSTACK_STREAMING_PUMP_H
