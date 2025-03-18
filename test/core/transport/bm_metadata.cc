//
//
// Copyright 2017 gRPC authors.
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

#include <benchmark/benchmark.h>

#include "src/core/call/metadata.h"

namespace grpc_core {
namespace {

void BM_MetadataMapCreateDestroy(benchmark::State& state) {
  for (auto _ : state) {
    auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  }
}
BENCHMARK(BM_MetadataMapCreateDestroy);

void BM_MetadataMapCreateDestroyOnStack(benchmark::State& state) {
  for (auto _ : state) {
    ServerMetadata md;
  }
}
BENCHMARK(BM_MetadataMapCreateDestroyOnStack);

void BM_MetadataMapCreateDestroySetStatus(benchmark::State& state) {
  auto message = Slice::FromExternalString("message");
  for (auto _ : state) {
    auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
    md->Set(GrpcStatusMetadata(), GRPC_STATUS_UNKNOWN);
    md->Set(GrpcMessageMetadata(), message.Copy());
  }
}
BENCHMARK(BM_MetadataMapCreateDestroySetStatus);

void BM_MetadataMapCreateDestroySetStatusCancelled(benchmark::State& state) {
  auto message = Slice::FromExternalString("message");
  for (auto _ : state) {
    auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
    md->Set(GrpcStatusMetadata(), GRPC_STATUS_CANCELLED);
  }
}
BENCHMARK(BM_MetadataMapCreateDestroySetStatusCancelled);

void BM_MetadataMapFromAbslStatusCancelled(benchmark::State& state) {
  for (auto _ : state) {
    ServerMetadataFromStatus(absl::CancelledError());
  }
}
BENCHMARK(BM_MetadataMapFromAbslStatusCancelled);

void BM_MetadataMapFromAbslStatusOk(benchmark::State& state) {
  for (auto _ : state) {
    ServerMetadataFromStatus(absl::OkStatus());
  }
}
BENCHMARK(BM_MetadataMapFromAbslStatusOk);

}  // namespace
}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
