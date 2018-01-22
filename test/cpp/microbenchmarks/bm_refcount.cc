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

#include <benchmark/benchmark.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "test/cpp/microbenchmarks/helpers.h"

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc {
namespace testing {

struct Small {
  int word;
};

struct SmallRef : public grpc_core::RefCounted {
  struct Small obj;
};

struct Medium {
  int word[1000];
};

struct MediumRef : public grpc_core::RefCounted {
  struct Medium obj;
};

struct Large {
  int word[1000000];
};

struct LargeRef : public grpc_core::RefCounted {
  struct Large obj;
};

static void BM_RefCountedAllocateSmall(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    auto rcp = grpc_core::MakeRefCounted<SmallRef>();
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_RefCountedAllocateSmall);

static void BM_RefCountedAllocateMedium(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    auto rcp = grpc_core::MakeRefCounted<MediumRef>();
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_RefCountedAllocateMedium);

static void BM_RefCountedAllocateLarge(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    auto rcp = grpc_core::MakeRefCounted<LargeRef>();
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_RefCountedAllocateLarge);

static void BM_SharedAllocateSmall(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::Allocator<Small> alloc;
  while (state.KeepRunning()) {
    auto sp = std::allocate_shared<Small>(alloc);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_SharedAllocateSmall);

static void BM_SharedAllocateMedium(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::Allocator<Medium> alloc;
  while (state.KeepRunning()) {
    auto sp = std::allocate_shared<Medium>(alloc);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_SharedAllocateMedium);

static void BM_SharedAllocateLarge(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::Allocator<Large> alloc;
  while (state.KeepRunning()) {
    auto sp = std::allocate_shared<Large>(alloc);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_SharedAllocateLarge);

}  // namespace testing
}  // namespace grpc

BENCHMARK_MAIN();
