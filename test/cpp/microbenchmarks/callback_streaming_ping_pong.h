#ifndef TEST_CPP_MICROBENCHMARKS_CALLBACK_STREAMING_PING_PONG_H
#define TEST_CPP_MICROBENCHMARKS_CALLBACK_STREAMING_PING_PONG_H


#include <benchmark/benchmark.h>
#include <sstream>
#include "src/core/lib/profiling/timers.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/microbenchmarks/fullstack_context_mutators.h"
#include "test/cpp/microbenchmarks/fullstack_fixtures.h"
#include "test/cpp/microbenchmarks/callback_test_service.h"

namespace grpc {
namespace testing {

/*******************************************************************************
 * BENCHMARKING KERNELS
 */

template <class Fixture, class ClientContextMutator, class ServerContextMutator>
static void BM_CallbackBidiStreaming(benchmark::State& state) {
  const int message_size = state.range(0);
  const int max_ping_pongs = state.range(1) > 0 ? 1 : state.range(1);
  CallbackStreamingTestService service;
  std::unique_ptr<Fixture> fixture(new Fixture(&service));
  std::unique_ptr<EchoTestService::Stub> stub_(
      EchoTestService::NewStub(fixture->channel()));
  EchoRequest* request = new EchoRequest;
  EchoResponse* response = new EchoResponse;
  if (state.range(0) > 0) {
    request->set_message(std::string(state.range(0), 'a'));
  } else {
    request->set_message("");
  }
  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    ClientContext* cli_ctx = new ClientContext;
    cli_ctx->AddMetadata(kServerFinishAfterNReads,
                         grpc::to_string(max_ping_pongs));
    cli_ctx->AddMetadata(kServerResponseStreamsToSend,
                             grpc::to_string(message_size));
    BidiClient test{stub_.get(), request, response, cli_ctx, max_ping_pongs};
    test.Await();
  }
  fixture->Finish(state);
  fixture.reset();
  state.SetBytesProcessed(2 * state.range(0) * state.iterations()
                          * state.range(1));
}

}  // namespace testing
}  // namespace grpc
#endif  // TEST_CPP_MICROBENCHMARKS_CALLBACK_STREAMING_PING_PONG_H
