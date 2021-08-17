/*
 *
 * Copyright 2015 gRPC authors.
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

/* This benchmark exists to ensure that immediately-firing alarms are fast */

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>
#include <grpcpp/alarm.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/impl/grpc_library.h>
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

static void BM_Alarm_Tag_Immediate(benchmark::State& state) {
  TrackCounters track_counters;
  CompletionQueue cq;
  Alarm alarm;
  void* output_tag;
  bool ok;
  auto deadline = grpc_timeout_seconds_to_deadline(0);
  for (auto _ : state) {
    alarm.Set(&cq, deadline, nullptr);
    cq.Next(&output_tag, &ok);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_Alarm_Tag_Immediate);

}  // namespace testing
}  // namespace grpc

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
