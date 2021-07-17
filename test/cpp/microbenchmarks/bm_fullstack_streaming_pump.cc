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

#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/fullstack_streaming_pump.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

/*******************************************************************************
 * CONFIGURATIONS
 */

BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, TCP)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, UDS)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, InProcess)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, InProcessCHTTP2)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, TCP)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, UDS)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, InProcess)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, InProcessCHTTP2)
    ->Range(0, 128 * 1024 * 1024);
BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, MinTCP)->Arg(0);
BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, MinUDS)->Arg(0);
BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, MinInProcess)->Arg(0);
BENCHMARK_TEMPLATE(BM_PumpStreamClientToServer, MinInProcessCHTTP2)->Arg(0);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, MinTCP)->Arg(0);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, MinUDS)->Arg(0);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, MinInProcess)->Arg(0);
BENCHMARK_TEMPLATE(BM_PumpStreamServerToClient, MinInProcessCHTTP2)->Arg(0);

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
