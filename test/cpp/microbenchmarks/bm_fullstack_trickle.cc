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

namespace grpc { namespace testing{

static void TrickleCQNext(TrickledCHTTP2* fixture, void** t, bool* ok) {
  while (true) {
    switch (fixture->cq()->AsyncNext(
        t, ok, gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                            gpr_time_from_micros(100, GPR_TIMESPAN)))) {
      case CompletionQueue::TIMEOUT:
        fixture->Step();
        break;
      case CompletionQueue::SHUTDOWN:
        GPR_ASSERT(false);
        break;
      case CompletionQueue::GOT_EVENT:
        return;
    }
  }
}

static void BM_PumpStreamServerToClient_Trickle(benchmark::State& state) {
  EchoTestService::AsyncService service;
  std::unique_ptr<TrickledCHTTP2> fixture(
      new TrickledCHTTP2(&service, state.range(1)));
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
      TrickleCQNext(fixture.get(), &t, &ok);
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
        TrickleCQNext(fixture.get(), &t, &ok);
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
      TrickleCQNext(fixture.get(), &t, &ok);
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

static void TrickleArgs(benchmark::internal::Benchmark* b) {
  for (int i = 1; i <= 128 * 1024 * 1024; i *= 8) {
    for (int j = 1; j <= 128 * 1024 * 1024; j *= 8) {
      double expected_time =
          static_cast<double>(14 + i) / (125.0 * static_cast<double>(j));
      if (expected_time > 0.01) continue;
      b->Args({i, j});
    }
  }
}

BENCHMARK(BM_PumpStreamServerToClient_Trickle)->Apply(TrickleArgs);

BENCHMARK_MAIN();
