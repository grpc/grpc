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

#include <sstream>

#include "src/core/lib/profiling/timers.h"
#include "src/cpp/client/create_channel_internal.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/microbenchmarks/fullstack_context_mutators.h"
#include "test/cpp/microbenchmarks/fullstack_fixtures.h"
#include "third_party/benchmark/include/benchmark/benchmark.h"

namespace grpc {
namespace testing {

// force library initialization
auto& force_library_initialization = Library::get();

/*******************************************************************************
 * BENCHMARKING KERNELS
 */

static void* tag(intptr_t x) { return reinterpret_cast<void*>(x); }

// Repeatedly makes Streaming Bidi calls (exchanging a configurable number of
// messages in each call) in a loop on a single channel
//
//  First parmeter (i.e state.range(0)):  Message size (in bytes) to use
//  Second parameter (i.e state.range(1)): Number of ping pong messages.
//      Note: One ping-pong means two messages (one from client to server and
//      the other from server to client):
template <class Fixture, class ClientContextMutator, class ServerContextMutator>
static void BM_StreamingPingPong(benchmark::State& state) {
  const int msg_size = state.range(0);
  const int max_ping_pongs = state.range(1);

  EchoTestService::AsyncService service;
  std::unique_ptr<Fixture> fixture(new Fixture(&service));
  {
    EchoResponse send_response;
    EchoResponse recv_response;
    EchoRequest send_request;
    EchoRequest recv_request;

    if (msg_size > 0) {
      send_request.set_message(std::string(msg_size, 'a'));
      send_response.set_message(std::string(msg_size, 'b'));
    }

    std::unique_ptr<EchoTestService::Stub> stub(
        EchoTestService::NewStub(fixture->channel()));

    while (state.KeepRunning()) {
      ServerContext svr_ctx;
      ServerContextMutator svr_ctx_mut(&svr_ctx);
      ServerAsyncReaderWriter<EchoResponse, EchoRequest> response_rw(&svr_ctx);
      service.RequestBidiStream(&svr_ctx, &response_rw, fixture->cq(),
                                fixture->cq(), tag(0));

      ClientContext cli_ctx;
      ClientContextMutator cli_ctx_mut(&cli_ctx);
      auto request_rw = stub->AsyncBidiStream(&cli_ctx, fixture->cq(), tag(1));

      // Establish async stream between client side and server side
      void* t;
      bool ok;
      int need_tags = (1 << 0) | (1 << 1);
      while (need_tags) {
        GPR_ASSERT(fixture->cq()->Next(&t, &ok));
        GPR_ASSERT(ok);
        int i = (int)(intptr_t)t;
        GPR_ASSERT(need_tags & (1 << i));
        need_tags &= ~(1 << i);
      }

      // Send 'max_ping_pongs' number of ping pong messages
      int ping_pong_cnt = 0;
      while (ping_pong_cnt < max_ping_pongs) {
        request_rw->Write(send_request, tag(0));   // Start client send
        response_rw.Read(&recv_request, tag(1));   // Start server recv
        request_rw->Read(&recv_response, tag(2));  // Start client recv

        need_tags = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);
        while (need_tags) {
          GPR_ASSERT(fixture->cq()->Next(&t, &ok));
          GPR_ASSERT(ok);
          int i = (int)(intptr_t)t;

          // If server recv is complete, start the server send operation
          if (i == 1) {
            response_rw.Write(send_response, tag(3));
          }

          GPR_ASSERT(need_tags & (1 << i));
          need_tags &= ~(1 << i);
        }

        ping_pong_cnt++;
      }

      request_rw->WritesDone(tag(0));
      response_rw.Finish(Status::OK, tag(1));

      Status recv_status;
      request_rw->Finish(&recv_status, tag(2));

      need_tags = (1 << 0) | (1 << 1) | (1 << 2);
      while (need_tags) {
        GPR_ASSERT(fixture->cq()->Next(&t, &ok));
        int i = (int)(intptr_t)t;
        GPR_ASSERT(need_tags & (1 << i));
        need_tags &= ~(1 << i);
      }

      GPR_ASSERT(recv_status.ok());
    }
  }

  fixture->Finish(state);
  fixture.reset();
  state.SetBytesProcessed(msg_size * state.iterations() * max_ping_pongs * 2);
}

// Repeatedly sends ping pong messages in a single streaming Bidi call in a loop
//     First parmeter (i.e state.range(0)):  Message size (in bytes) to use
template <class Fixture, class ClientContextMutator, class ServerContextMutator>
static void BM_StreamingPingPongMsgs(benchmark::State& state) {
  const int msg_size = state.range(0);

  EchoTestService::AsyncService service;
  std::unique_ptr<Fixture> fixture(new Fixture(&service));
  {
    EchoResponse send_response;
    EchoResponse recv_response;
    EchoRequest send_request;
    EchoRequest recv_request;

    if (msg_size > 0) {
      send_request.set_message(std::string(msg_size, 'a'));
      send_response.set_message(std::string(msg_size, 'b'));
    }

    std::unique_ptr<EchoTestService::Stub> stub(
        EchoTestService::NewStub(fixture->channel()));

    ServerContext svr_ctx;
    ServerContextMutator svr_ctx_mut(&svr_ctx);
    ServerAsyncReaderWriter<EchoResponse, EchoRequest> response_rw(&svr_ctx);
    service.RequestBidiStream(&svr_ctx, &response_rw, fixture->cq(),
                              fixture->cq(), tag(0));

    ClientContext cli_ctx;
    ClientContextMutator cli_ctx_mut(&cli_ctx);
    auto request_rw = stub->AsyncBidiStream(&cli_ctx, fixture->cq(), tag(1));

    // Establish async stream between client side and server side
    void* t;
    bool ok;
    int need_tags = (1 << 0) | (1 << 1);
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      GPR_ASSERT(ok);
      int i = (int)(intptr_t)t;
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }

    while (state.KeepRunning()) {
      GPR_TIMER_SCOPE("BenchmarkCycle", 0);
      request_rw->Write(send_request, tag(0));   // Start client send
      response_rw.Read(&recv_request, tag(1));   // Start server recv
      request_rw->Read(&recv_response, tag(2));  // Start client recv

      need_tags = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);
      while (need_tags) {
        GPR_ASSERT(fixture->cq()->Next(&t, &ok));
        GPR_ASSERT(ok);
        int i = (int)(intptr_t)t;

        // If server recv is complete, start the server send operation
        if (i == 1) {
          response_rw.Write(send_response, tag(3));
        }

        GPR_ASSERT(need_tags & (1 << i));
        need_tags &= ~(1 << i);
      }
    }

    request_rw->WritesDone(tag(0));
    response_rw.Finish(Status::OK, tag(1));
    Status recv_status;
    request_rw->Finish(&recv_status, tag(2));

    need_tags = (1 << 0) | (1 << 1) | (1 << 2);
    while (need_tags) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      int i = (int)(intptr_t)t;
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }

    GPR_ASSERT(recv_status.ok());
  }

  fixture->Finish(state);
  fixture.reset();
  state.SetBytesProcessed(msg_size * state.iterations() * 2);
}

/*******************************************************************************
 * CONFIGURATIONS
 */

// Generate Args for StreamingPingPong benchmarks. Currently generates args for
// only "small streams" (i.e streams with 0, 1 or 2 messages)
static void StreamingPingPongArgs(benchmark::internal::Benchmark* b) {
  int msg_size = 0;

  b->Args({0, 0});  // spl case: 0 ping-pong msgs (msg_size doesn't matter here)

  for (msg_size = 0; msg_size <= 128 * 1024 * 1024;
       msg_size == 0 ? msg_size++ : msg_size *= 8) {
    b->Args({msg_size, 1});
    b->Args({msg_size, 2});
  }
}

BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcessCHTTP2, NoOpMutator,
                   NoOpMutator)
    ->Apply(StreamingPingPongArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPong, TCP, NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongArgs);

BENCHMARK_TEMPLATE(BM_StreamingPingPongMsgs, InProcessCHTTP2, NoOpMutator,
                   NoOpMutator)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_StreamingPingPongMsgs, TCP, NoOpMutator, NoOpMutator)
    ->Range(0, 128 * 1024 * 1024);

}  // namespace testing
}  // namespace grpc

BENCHMARK_MAIN();
