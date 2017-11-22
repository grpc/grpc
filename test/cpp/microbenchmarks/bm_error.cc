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

/* Test various operations on grpc_error */

#include <benchmark/benchmark.h>
#include <memory>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/transport/error_utils.h"

#include "test/cpp/microbenchmarks/helpers.h"

auto& force_library_initialization = Library::get();

class ErrorDeleter {
 public:
  void operator()(grpc_error* error) { GRPC_ERROR_UNREF(error); }
};
typedef std::unique_ptr<grpc_error, ErrorDeleter> ErrorPtr;

static void BM_ErrorCreateFromStatic(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"));
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_ErrorCreateFromStatic);

static void BM_ErrorCreateFromCopied(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(GRPC_ERROR_CREATE_FROM_COPIED_STRING("Error not inline"));
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_ErrorCreateFromCopied);

static void BM_ErrorCreateAndSetStatus(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                           GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_ABORTED));
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_ErrorCreateAndSetStatus);

static void BM_ErrorCreateAndSetIntAndStr(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(grpc_error_set_str(
        grpc_error_set_int(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("GOAWAY received"),
            GRPC_ERROR_INT_HTTP2_ERROR, (intptr_t)0),
        GRPC_ERROR_STR_RAW_BYTES, grpc_slice_from_static_string("raw bytes")));
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_ErrorCreateAndSetIntAndStr);

static void BM_ErrorCreateAndSetIntLoop(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error");
  int n = 0;
  while (state.KeepRunning()) {
    error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS, n++);
  }
  GRPC_ERROR_UNREF(error);
  track_counters.Finish(state);
}
BENCHMARK(BM_ErrorCreateAndSetIntLoop);

static void BM_ErrorCreateAndSetStrLoop(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error");
  const char* str = "hello";
  while (state.KeepRunning()) {
    error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE,
                               grpc_slice_from_static_string(str));
  }
  GRPC_ERROR_UNREF(error);
  track_counters.Finish(state);
}
BENCHMARK(BM_ErrorCreateAndSetStrLoop);

static void BM_ErrorRefUnref(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error");
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
  track_counters.Finish(state);
}
BENCHMARK(BM_ErrorRefUnref);

static void BM_ErrorUnrefNone(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(GRPC_ERROR_NONE);
  }
}
BENCHMARK(BM_ErrorUnrefNone);

static void BM_ErrorGetIntFromNoError(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    intptr_t value;
    grpc_error_get_int(GRPC_ERROR_NONE, GRPC_ERROR_INT_GRPC_STATUS, &value);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_ErrorGetIntFromNoError);

static void BM_ErrorGetMissingInt(benchmark::State& state) {
  TrackCounters track_counters;
  ErrorPtr error(grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"), GRPC_ERROR_INT_INDEX, 1));
  while (state.KeepRunning()) {
    intptr_t value;
    grpc_error_get_int(error.get(), GRPC_ERROR_INT_OFFSET, &value);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_ErrorGetMissingInt);

static void BM_ErrorGetPresentInt(benchmark::State& state) {
  TrackCounters track_counters;
  ErrorPtr error(grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"), GRPC_ERROR_INT_OFFSET, 1));
  while (state.KeepRunning()) {
    intptr_t value;
    grpc_error_get_int(error.get(), GRPC_ERROR_INT_OFFSET, &value);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_ErrorGetPresentInt);

// Fixtures for tests: generate different kinds of errors
class ErrorNone {
 public:
  grpc_millis deadline() const { return deadline_; }
  grpc_error* error() const { return GRPC_ERROR_NONE; }

 private:
  const grpc_millis deadline_ = GRPC_MILLIS_INF_FUTURE;
};

class ErrorCancelled {
 public:
  grpc_millis deadline() const { return deadline_; }
  grpc_error* error() const { return GRPC_ERROR_CANCELLED; }

 private:
  const grpc_millis deadline_ = GRPC_MILLIS_INF_FUTURE;
};

class SimpleError {
 public:
  grpc_millis deadline() const { return deadline_; }
  grpc_error* error() const { return error_.get(); }

 private:
  const grpc_millis deadline_ = GRPC_MILLIS_INF_FUTURE;
  ErrorPtr error_{GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error")};
};

class ErrorWithGrpcStatus {
 public:
  grpc_millis deadline() const { return deadline_; }
  grpc_error* error() const { return error_.get(); }

 private:
  const grpc_millis deadline_ = GRPC_MILLIS_INF_FUTURE;
  ErrorPtr error_{grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"), GRPC_ERROR_INT_GRPC_STATUS,
      GRPC_STATUS_UNIMPLEMENTED)};
};

class ErrorWithHttpError {
 public:
  grpc_millis deadline() const { return deadline_; }
  grpc_error* error() const { return error_.get(); }

 private:
  const grpc_millis deadline_ = GRPC_MILLIS_INF_FUTURE;
  ErrorPtr error_{grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"), GRPC_ERROR_INT_HTTP2_ERROR,
      GRPC_HTTP2_COMPRESSION_ERROR)};
};

class ErrorWithNestedGrpcStatus {
 public:
  grpc_millis deadline() const { return deadline_; }
  grpc_error* error() const { return error_.get(); }

 private:
  const grpc_millis deadline_ = GRPC_MILLIS_INF_FUTURE;
  ErrorPtr nested_error_{grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"), GRPC_ERROR_INT_GRPC_STATUS,
      GRPC_STATUS_UNIMPLEMENTED)};
  grpc_error* nested_errors_[1] = {nested_error_.get()};
  ErrorPtr error_{GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
      "Error", nested_errors_, 1)};
};

template <class Fixture>
static void BM_ErrorStringOnNewError(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    Fixture fixture;
    grpc_error_string(fixture.error());
  }
  track_counters.Finish(state);
}

template <class Fixture>
static void BM_ErrorStringRepeatedly(benchmark::State& state) {
  TrackCounters track_counters;
  Fixture fixture;
  while (state.KeepRunning()) {
    grpc_error_string(fixture.error());
  }
  track_counters.Finish(state);
}

template <class Fixture>
static void BM_ErrorGetStatus(benchmark::State& state) {
  TrackCounters track_counters;
  Fixture fixture;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_status_code status;
    grpc_slice slice;
    grpc_error_get_status(&exec_ctx, fixture.error(), fixture.deadline(),
                          &status, &slice, nullptr, nullptr);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}

template <class Fixture>
static void BM_ErrorGetStatusCode(benchmark::State& state) {
  TrackCounters track_counters;
  Fixture fixture;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_status_code status;
    grpc_error_get_status(&exec_ctx, fixture.error(), fixture.deadline(),
                          &status, nullptr, nullptr, nullptr);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}

template <class Fixture>
static void BM_ErrorHttpError(benchmark::State& state) {
  TrackCounters track_counters;
  Fixture fixture;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_http2_error_code error;
    grpc_error_get_status(&exec_ctx, fixture.error(), fixture.deadline(),
                          nullptr, nullptr, &error, nullptr);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}

template <class Fixture>
static void BM_HasClearGrpcStatus(benchmark::State& state) {
  TrackCounters track_counters;
  Fixture fixture;
  while (state.KeepRunning()) {
    grpc_error_has_clear_grpc_status(fixture.error());
  }
  track_counters.Finish(state);
}

#define BENCHMARK_SUITE(fixture)                         \
  BENCHMARK_TEMPLATE(BM_ErrorStringOnNewError, fixture); \
  BENCHMARK_TEMPLATE(BM_ErrorStringRepeatedly, fixture); \
  BENCHMARK_TEMPLATE(BM_ErrorGetStatus, fixture);        \
  BENCHMARK_TEMPLATE(BM_ErrorGetStatusCode, fixture);    \
  BENCHMARK_TEMPLATE(BM_ErrorHttpError, fixture);        \
  BENCHMARK_TEMPLATE(BM_HasClearGrpcStatus, fixture)

BENCHMARK_SUITE(ErrorNone);
BENCHMARK_SUITE(ErrorCancelled);
BENCHMARK_SUITE(SimpleError);
BENCHMARK_SUITE(ErrorWithGrpcStatus);
BENCHMARK_SUITE(ErrorWithHttpError);
BENCHMARK_SUITE(ErrorWithNestedGrpcStatus);

BENCHMARK_MAIN();
