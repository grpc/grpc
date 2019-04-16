#ifndef TEST_CPP_CALLBACKMICROBENCHMARKS_CALLBACK_UNARY_PING_PONG_H
#define TEST_CPP_CALLBACKMICROBENCHMARKS_CALLBACK_UNARY_PING_PONG_H

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
static void BM_UnaryPingPong(benchmark::State& state) {
  CallbackStreamingTestService service;
  std::unique_ptr<Fixture> fixture(new Fixture(&service));
  std::unique_ptr<EchoTestService::Stub> stub_(
      EchoTestService::NewStub(fixture->channel()));
  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    EchoRequest* request = new EchoRequest;
    EchoResponse* response = new EchoResponse;
    ClientContext* cli_ctx = new ClientContext;
    int iteration = state.iterations();
    if (state.range(0) > 0) {
      request->set_message(std::string(state.range(0), 'a'));
    } else {
      request->set_message("");
    }
    stub_->experimental_async()->Echo(cli_ctx, request, response,
     [request, response, &iteration](Status s) {
//       if (!s.ok()){
//         LOG(ERROR) << "state error " << s.error_message();
//         LOG(ERROR) << "iteration " << iteration;
//
//       }
//       if (request->message() != response->message()) {
//         LOG(ERROR) << "response message error ";
//       }
    });
  }
  fixture->Finish(state);
  fixture.reset();
  state.SetBytesProcessed(2 * state.range(0) * state.iterations());
}
}  // namespace testing
}  // namespace grpc

#endif  // TEST_CPP_CALLBACKMICROBENCHMARKS_CALLBACK_UNARY_PING_PONG_H
