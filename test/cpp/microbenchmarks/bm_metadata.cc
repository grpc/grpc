/*
 *
 * Copyright 2017 gRPC authors.
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

/* Test out various metadata handling primitives */

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>

#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/static_metadata.h"

#include "test/cpp/microbenchmarks/helpers.h"

auto& force_library_initialization = Library::get();

static void BM_SliceFromStatic(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(grpc_slice_from_static_string("abc"));
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_SliceFromStatic);

static void BM_SliceFromCopied(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    grpc_slice_unref(grpc_slice_from_copied_string("abc"));
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_SliceFromCopied);

static void BM_SliceIntern(benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice slice = grpc_slice_from_static_string("abc");
  while (state.KeepRunning()) {
    grpc_slice_unref(grpc_slice_intern(slice));
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_SliceIntern);

static void BM_SliceReIntern(benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice slice = grpc_slice_intern(grpc_slice_from_static_string("abc"));
  while (state.KeepRunning()) {
    grpc_slice_unref(grpc_slice_intern(slice));
  }
  grpc_slice_unref(slice);
  track_counters.Finish(state);
}
BENCHMARK(BM_SliceReIntern);

static void BM_SliceInternStaticMetadata(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    grpc_slice_intern(GRPC_MDSTR_GZIP);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_SliceInternStaticMetadata);

static void BM_SliceInternEqualToStaticMetadata(benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice slice = grpc_slice_from_static_string("gzip");
  while (state.KeepRunning()) {
    grpc_slice_intern(slice);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_SliceInternEqualToStaticMetadata);

static void BM_MetadataFromNonInternedSlices(benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice k = grpc_slice_from_static_string("key");
  gpr_slice v = grpc_slice_from_static_string("value");
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(&exec_ctx, grpc_mdelem_create(&exec_ctx, k, v, nullptr));
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataFromNonInternedSlices);

static void BM_MetadataFromInternedSlices(benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice k = grpc_slice_intern(grpc_slice_from_static_string("key"));
  gpr_slice v = grpc_slice_intern(grpc_slice_from_static_string("value"));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(&exec_ctx, grpc_mdelem_create(&exec_ctx, k, v, nullptr));
  }
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_slice_unref(k);
  grpc_slice_unref(v);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataFromInternedSlices);

static void BM_MetadataFromInternedSlicesAlreadyInIndex(
    benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice k = grpc_slice_intern(grpc_slice_from_static_string("key"));
  gpr_slice v = grpc_slice_intern(grpc_slice_from_static_string("value"));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_mdelem seed = grpc_mdelem_create(&exec_ctx, k, v, nullptr);
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(&exec_ctx, grpc_mdelem_create(&exec_ctx, k, v, nullptr));
  }
  GRPC_MDELEM_UNREF(&exec_ctx, seed);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_slice_unref(k);
  grpc_slice_unref(v);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataFromInternedSlicesAlreadyInIndex);

static void BM_MetadataFromInternedKey(benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice k = grpc_slice_intern(grpc_slice_from_static_string("key"));
  gpr_slice v = grpc_slice_from_static_string("value");
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(&exec_ctx, grpc_mdelem_create(&exec_ctx, k, v, nullptr));
  }
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_slice_unref(k);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataFromInternedKey);

static void BM_MetadataFromNonInternedSlicesWithBackingStore(
    benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice k = grpc_slice_from_static_string("key");
  gpr_slice v = grpc_slice_from_static_string("value");
  char backing_store[sizeof(grpc_mdelem_data)];
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(
        &exec_ctx,
        grpc_mdelem_create(&exec_ctx, k, v,
                           reinterpret_cast<grpc_mdelem_data*>(backing_store)));
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataFromNonInternedSlicesWithBackingStore);

static void BM_MetadataFromInternedSlicesWithBackingStore(
    benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice k = grpc_slice_intern(grpc_slice_from_static_string("key"));
  gpr_slice v = grpc_slice_intern(grpc_slice_from_static_string("value"));
  char backing_store[sizeof(grpc_mdelem_data)];
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(
        &exec_ctx,
        grpc_mdelem_create(&exec_ctx, k, v,
                           reinterpret_cast<grpc_mdelem_data*>(backing_store)));
  }
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_slice_unref(k);
  grpc_slice_unref(v);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataFromInternedSlicesWithBackingStore);

static void BM_MetadataFromInternedKeyWithBackingStore(
    benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice k = grpc_slice_intern(grpc_slice_from_static_string("key"));
  gpr_slice v = grpc_slice_from_static_string("value");
  char backing_store[sizeof(grpc_mdelem_data)];
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(
        &exec_ctx,
        grpc_mdelem_create(&exec_ctx, k, v,
                           reinterpret_cast<grpc_mdelem_data*>(backing_store)));
  }
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_slice_unref(k);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataFromInternedKeyWithBackingStore);

static void BM_MetadataFromStaticMetadataStrings(benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice k = GRPC_MDSTR_STATUS;
  gpr_slice v = GRPC_MDSTR_200;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(&exec_ctx, grpc_mdelem_create(&exec_ctx, k, v, nullptr));
  }
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_slice_unref(k);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataFromStaticMetadataStrings);

static void BM_MetadataFromStaticMetadataStringsNotIndexed(
    benchmark::State& state) {
  TrackCounters track_counters;
  gpr_slice k = GRPC_MDSTR_STATUS;
  gpr_slice v = GRPC_MDSTR_GZIP;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(&exec_ctx, grpc_mdelem_create(&exec_ctx, k, v, nullptr));
  }
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_slice_unref(k);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataFromStaticMetadataStringsNotIndexed);

static void BM_MetadataRefUnrefExternal(benchmark::State& state) {
  TrackCounters track_counters;
  char backing_store[sizeof(grpc_mdelem_data)];
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_mdelem el =
      grpc_mdelem_create(&exec_ctx, grpc_slice_from_static_string("a"),
                         grpc_slice_from_static_string("b"),
                         reinterpret_cast<grpc_mdelem_data*>(backing_store));
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(&exec_ctx, GRPC_MDELEM_REF(el));
  }
  GRPC_MDELEM_UNREF(&exec_ctx, el);
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataRefUnrefExternal);

static void BM_MetadataRefUnrefInterned(benchmark::State& state) {
  TrackCounters track_counters;
  char backing_store[sizeof(grpc_mdelem_data)];
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_slice k = grpc_slice_intern(grpc_slice_from_static_string("key"));
  gpr_slice v = grpc_slice_intern(grpc_slice_from_static_string("value"));
  grpc_mdelem el = grpc_mdelem_create(
      &exec_ctx, k, v, reinterpret_cast<grpc_mdelem_data*>(backing_store));
  grpc_slice_unref(k);
  grpc_slice_unref(v);
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(&exec_ctx, GRPC_MDELEM_REF(el));
  }
  GRPC_MDELEM_UNREF(&exec_ctx, el);
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataRefUnrefInterned);

static void BM_MetadataRefUnrefAllocated(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_mdelem el =
      grpc_mdelem_create(&exec_ctx, grpc_slice_from_static_string("a"),
                         grpc_slice_from_static_string("b"), nullptr);
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(&exec_ctx, GRPC_MDELEM_REF(el));
  }
  GRPC_MDELEM_UNREF(&exec_ctx, el);
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataRefUnrefAllocated);

static void BM_MetadataRefUnrefStatic(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_mdelem el =
      grpc_mdelem_create(&exec_ctx, GRPC_MDSTR_STATUS, GRPC_MDSTR_200, nullptr);
  while (state.KeepRunning()) {
    GRPC_MDELEM_UNREF(&exec_ctx, GRPC_MDELEM_REF(el));
  }
  GRPC_MDELEM_UNREF(&exec_ctx, el);
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_MetadataRefUnrefStatic);

BENCHMARK_MAIN();
