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

template <class Fixture, class ClientContextMutator, class ServerContextMutator>
static void BM_UnaryPingPong(benchmark::State& state) {
  EchoTestService::AsyncService service;
  std::unique_ptr<Fixture> fixture(new Fixture(&service));
  EchoRequest send_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  if (state.range(0) > 0) {
    send_request.set_message(std::string(state.range(0), 'a'));
  }
  if (state.range(1) > 0) {
    send_response.set_message(std::string(state.range(1), 'a'));
  }
  Status recv_status;
  struct ServerEnv {
    ServerContext ctx;
    EchoRequest recv_request;
    grpc::ServerAsyncResponseWriter<EchoResponse> response_writer;
    ServerEnv() : response_writer(&ctx) {}
  };
  uint8_t server_env_buffer[2 * sizeof(ServerEnv)];
  ServerEnv* server_env[2] = {
      reinterpret_cast<ServerEnv*>(server_env_buffer),
      reinterpret_cast<ServerEnv*>(server_env_buffer + sizeof(ServerEnv))};
  new (server_env[0]) ServerEnv;
  new (server_env[1]) ServerEnv;
  service.RequestEcho(&server_env[0]->ctx, &server_env[0]->recv_request,
                      &server_env[0]->response_writer, fixture->cq(),
                      fixture->cq(), tag(0));
  service.RequestEcho(&server_env[1]->ctx, &server_env[1]->recv_request,
                      &server_env[1]->response_writer, fixture->cq(),
                      fixture->cq(), tag(1));
  std::unique_ptr<EchoTestService::Stub> stub(
      EchoTestService::NewStub(fixture->channel()));
  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    recv_response.Clear();
    ClientContext cli_ctx;
    ClientContextMutator cli_ctx_mut(&cli_ctx);
    std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
        stub->AsyncEcho(&cli_ctx, send_request, fixture->cq()));
    void* t;
    bool ok;
    GPR_ASSERT(fixture->cq()->Next(&t, &ok));
    GPR_ASSERT(ok);
    GPR_ASSERT(t == tag(0) || t == tag(1));
    intptr_t slot = reinterpret_cast<intptr_t>(t);
    ServerEnv* senv = server_env[slot];
    ServerContextMutator svr_ctx_mut(&senv->ctx);
    senv->response_writer.Finish(send_response, Status::OK, tag(3));
    response_reader->Finish(&recv_response, &recv_status, tag(4));
    for (int i = (1 << 3) | (1 << 4); i != 0;) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      GPR_ASSERT(ok);
      int tagnum = (int)reinterpret_cast<intptr_t>(t);
      GPR_ASSERT(i & (1 << tagnum));
      i -= 1 << tagnum;
    }
    GPR_ASSERT(recv_status.ok());

    senv->~ServerEnv();
    senv = new (senv) ServerEnv();
    service.RequestEcho(&senv->ctx, &senv->recv_request, &senv->response_writer,
                        fixture->cq(), fixture->cq(), tag(slot));
  }
  fixture->Finish(state);
  fixture.reset();
  server_env[0]->~ServerEnv();
  server_env[1]->~ServerEnv();
  state.SetBytesProcessed(state.range(0) * state.iterations() +
                          state.range(1) * state.iterations());
}

/*******************************************************************************
 * CONFIGURATIONS
 */

static void SweepSizesArgs(benchmark::internal::Benchmark* b) {
  b->Args({0, 0});
  for (int i = 1; i <= 128 * 1024 * 1024; i *= 8) {
    b->Args({i, 0});
    b->Args({0, i});
    b->Args({i, i});
  }
}

BENCHMARK_TEMPLATE(BM_UnaryPingPong, TCP, NoOpMutator, NoOpMutator)
    ->Apply(SweepSizesArgs);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, UDS, NoOpMutator, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, SockPair, NoOpMutator, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator, NoOpMutator)
    ->Apply(SweepSizesArgs);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 1>,
                   NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 2>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 2>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 2>,
                   NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<10>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<31>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<100>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomAsciiMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomAsciiMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomAsciiMetadata<100>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<10>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<31>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<100>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<10>, 100>)
    ->Args({0, 0});

}  // namespace testing
}  // namespace grpc

BENCHMARK_MAIN();
