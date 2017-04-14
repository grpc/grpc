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

// Repeatedly makes Streaming Bidi calls (exchanging a configurable number of
// messages in each call) in a loop on a single channel. Different from
// BM_StreamingPingPong we are using stream coalescing api, e.g. WriteLast,
// WriteAndFinish, set_initial_metadata_corked. These apis aim at saving
// sendmsg syscalls for streaming by coalescing 1. initial metadata with first
// message; 2. final streaming message with trailing metadata.
//
//  First parmeter (i.e state.range(0)):  Message size (in bytes) to use
//  Second parameter (i.e state.range(1)): Number of ping pong messages.
//      Note: One ping-pong means two messages (one from client to server and
//      the other from server to client):
//  Third parameter (i.e state.range(2)): Switch between using WriteAndFinish
//  API and WriteLast API for server.
template <class Fixture, class ClientContextMutator, class ServerContextMutator>
static void BM_StreamingPingPongWithCoalescingApi(benchmark::State& state) {
  const int msg_size = state.range(0);
  const int max_ping_pongs = state.range(1);
  // This options is used to test out server API: WriteLast and WriteAndFinish
  // respectively, since we can not use both of them on server side at the same
  // time. Value 1 means we are testing out the WriteAndFinish API, and
  // otherwise we are testing out the WriteLast API.
  const int write_and_finish = state.range(2);

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
      cli_ctx.set_initial_metadata_corked(true);
      // tag:1 here will never comes up, since we are not performing any op due
      // to initial metadata coalescing.
      auto request_rw = stub->AsyncBidiStream(&cli_ctx, fixture->cq(), tag(1));

      void* t;
      bool ok;
      int need_tags;

      // Send 'max_ping_pongs' number of ping pong messages
      int ping_pong_cnt = 0;
      while (ping_pong_cnt < max_ping_pongs) {
        if (ping_pong_cnt == max_ping_pongs - 1) {
          request_rw->WriteLast(send_request, WriteOptions(), tag(2));
        } else {
          request_rw->Write(send_request, tag(2));  // Start client send
        }

        need_tags = (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5);

        if (ping_pong_cnt == 0) {
          // wait for the server call structure (call_hook, etc.) to be
          // initialized (async stream between client side and server side
          // established). It is necessary when client init metadata is
          // coalesced
          GPR_ASSERT(fixture->cq()->Next(&t, &ok));
          while ((int)(intptr_t)t != 0) {
            // In some cases tag:2 comes before tag:0 (write tag comes out
            // first), this while loop is to make sure get tag:0.
            int i = (int)(intptr_t)t;
            GPR_ASSERT(need_tags & (1 << i));
            need_tags &= ~(1 << i);
            GPR_ASSERT(fixture->cq()->Next(&t, &ok));
          }
        }

        response_rw.Read(&recv_request, tag(3));   // Start server recv
        request_rw->Read(&recv_response, tag(4));  // Start client recv

        while (need_tags) {
          GPR_ASSERT(fixture->cq()->Next(&t, &ok));
          GPR_ASSERT(ok);
          int i = (int)(intptr_t)t;

          // If server recv is complete, start the server send operation
          if (i == 3) {
            if (ping_pong_cnt == max_ping_pongs - 1) {
              if (write_and_finish == 1) {
                response_rw.WriteAndFinish(send_response, WriteOptions(),
                                           Status::OK, tag(5));
              } else {
                response_rw.WriteLast(send_response, WriteOptions(), tag(5));
                // WriteLast buffers the write, so neither server write op nor
                // client read op will finish inside the while loop.
                need_tags &= ~(1 << 4);
                need_tags &= ~(1 << 5);
              }
            } else {
              response_rw.Write(send_response, tag(5));
            }
          }

          GPR_ASSERT(need_tags & (1 << i));
          need_tags &= ~(1 << i);
        }

        ping_pong_cnt++;
      }

      if (max_ping_pongs == 0) {
        need_tags = (1 << 6) | (1 << 7) | (1 << 8);
      } else {
        if (write_and_finish == 1) {
          need_tags = (1 << 8);
        } else {
          // server's buffered write and the client's read of the buffered write
          // tags should come up.
          need_tags = (1 << 4) | (1 << 5) | (1 << 7) | (1 << 8);
        }
      }

      // No message write or initial metadata write happened yet.
      if (max_ping_pongs == 0) {
        request_rw->WritesDone(tag(6));
        // wait for server call data structure(call_hook, etc.) to be
        // initialized, since initial metadata is corked.
        GPR_ASSERT(fixture->cq()->Next(&t, &ok));
        while ((int)(intptr_t)t != 0) {
          int i = (int)(intptr_t)t;
          GPR_ASSERT(need_tags & (1 << i));
          need_tags &= ~(1 << i);
          GPR_ASSERT(fixture->cq()->Next(&t, &ok));
        }
        response_rw.Finish(Status::OK, tag(7));
      } else {
        if (write_and_finish != 1) {
          response_rw.Finish(Status::OK, tag(7));
        }
      }

      Status recv_status;
      request_rw->Finish(&recv_status, tag(8));

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

// Generate Args for StreamingPingPongWithCoalescingApi benchmarks. Currently
// generates args for only "small streams" (i.e streams with 0, 1 or 2 messages)
static void StreamingPingPongWithCoalescingApiArgs(
    benchmark::internal::Benchmark* b) {
  int msg_size = 0;

  b->Args(
      {0, 0, 0});  // spl case: 0 ping-pong msgs (msg_size doesn't matter here)
  b->Args(
      {0, 0, 1});  // spl case: 0 ping-pong msgs (msg_size doesn't matter here)

  for (msg_size = 0; msg_size <= 128 * 1024 * 1024;
       msg_size == 0 ? msg_size++ : msg_size *= 8) {
    b->Args({msg_size, 1, 0});
    b->Args({msg_size, 2, 0});
    b->Args({msg_size, 1, 1});
    b->Args({msg_size, 2, 1});
  }
}

BENCHMARK_TEMPLATE(BM_StreamingPingPongWithCoalescingApi, InProcessCHTTP2,
                   NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongWithCoalescingApiArgs);

}  // namespace testing
}  // namespace grpc

BENCHMARK_MAIN();
