/*
 *
 * Copyright 2015, Google Inc.
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

/* Test various operations on grpc_error */

#include <memory>

extern "C" {
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/transport/error_utils.h"
}

#include "third_party/benchmark/include/benchmark/benchmark.h"

class ErrorDeleter {
 public:
  void operator()(grpc_error* error) { GRPC_ERROR_UNREF(error); }
};
typedef std::unique_ptr<grpc_error, ErrorDeleter> ErrorPtr;

static void BM_ErrorCreate(benchmark::State& state) {
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(GRPC_ERROR_CREATE("Error"));
  }
}
BENCHMARK(BM_ErrorCreate);

static void BM_ErrorCreateAndSetStatus(benchmark::State& state) {
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(grpc_error_set_int(GRPC_ERROR_CREATE("Error"),
                                        GRPC_ERROR_INT_GRPC_STATUS,
                                        GRPC_STATUS_ABORTED));
  }
}
BENCHMARK(BM_ErrorCreateAndSetStatus);

static void BM_ErrorRefUnref(benchmark::State& state) {
  grpc_error* error = GRPC_ERROR_CREATE("Error");
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}
BENCHMARK(BM_ErrorRefUnref);

static void BM_ErrorUnrefNone(benchmark::State& state) {
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(GRPC_ERROR_NONE);
  }
}
BENCHMARK(BM_ErrorUnrefNone);

static void BM_ErrorGetIntFromNoError(benchmark::State& state) {
  while (state.KeepRunning()) {
    intptr_t value;
    grpc_error_get_int(GRPC_ERROR_NONE, GRPC_ERROR_INT_GRPC_STATUS, &value);
  }
}
BENCHMARK(BM_ErrorGetIntFromNoError);

static void BM_ErrorGetMissingInt(benchmark::State& state) {
  ErrorPtr error(
      grpc_error_set_int(GRPC_ERROR_CREATE("Error"), GRPC_ERROR_INT_INDEX, 1));
  while (state.KeepRunning()) {
    intptr_t value;
    grpc_error_get_int(error.get(), GRPC_ERROR_INT_OFFSET, &value);
  }
}
BENCHMARK(BM_ErrorGetMissingInt);

static void BM_ErrorGetPresentInt(benchmark::State& state) {
  ErrorPtr error(
      grpc_error_set_int(GRPC_ERROR_CREATE("Error"), GRPC_ERROR_INT_OFFSET, 1));
  while (state.KeepRunning()) {
    intptr_t value;
    grpc_error_get_int(error.get(), GRPC_ERROR_INT_OFFSET, &value);
  }
}
BENCHMARK(BM_ErrorGetPresentInt);

// Fixtures for tests: generate different kinds of errors
class ErrorNone {
 public:
  gpr_timespec deadline() const { return deadline_; }
  grpc_error* error() const { return GRPC_ERROR_NONE; }

 private:
  const gpr_timespec deadline_ = gpr_inf_future(GPR_CLOCK_MONOTONIC);
};

class ErrorCancelled {
 public:
  gpr_timespec deadline() const { return deadline_; }
  grpc_error* error() const { return GRPC_ERROR_CANCELLED; }

 private:
  const gpr_timespec deadline_ = gpr_inf_future(GPR_CLOCK_MONOTONIC);
};

class SimpleError {
 public:
  gpr_timespec deadline() const { return deadline_; }
  grpc_error* error() const { return error_.get(); }

 private:
  const gpr_timespec deadline_ = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  ErrorPtr error_{GRPC_ERROR_CREATE("Error")};
};

class ErrorWithGrpcStatus {
 public:
  gpr_timespec deadline() const { return deadline_; }
  grpc_error* error() const { return error_.get(); }

 private:
  const gpr_timespec deadline_ = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  ErrorPtr error_{grpc_error_set_int(GRPC_ERROR_CREATE("Error"),
                                     GRPC_ERROR_INT_GRPC_STATUS,
                                     GRPC_STATUS_UNIMPLEMENTED)};
};

class ErrorWithHttpError {
 public:
  gpr_timespec deadline() const { return deadline_; }
  grpc_error* error() const { return error_.get(); }

 private:
  const gpr_timespec deadline_ = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  ErrorPtr error_{grpc_error_set_int(GRPC_ERROR_CREATE("Error"),
                                     GRPC_ERROR_INT_HTTP2_ERROR,
                                     GRPC_HTTP2_COMPRESSION_ERROR)};
};

class ErrorWithNestedGrpcStatus {
 public:
  gpr_timespec deadline() const { return deadline_; }
  grpc_error* error() const { return error_.get(); }

 private:
  const gpr_timespec deadline_ = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  ErrorPtr nested_error_{grpc_error_set_int(GRPC_ERROR_CREATE("Error"),
                                            GRPC_ERROR_INT_GRPC_STATUS,
                                            GRPC_STATUS_UNIMPLEMENTED)};
  grpc_error* nested_errors_[1] = {nested_error_.get()};
  ErrorPtr error_{GRPC_ERROR_CREATE_REFERENCING("Error", nested_errors_, 1)};
};

template <class Fixture>
static void BM_ErrorStringOnNewError(benchmark::State& state) {
  while (state.KeepRunning()) {
    Fixture fixture;
    grpc_error_string(fixture.error());
  }
}

template <class Fixture>
static void BM_ErrorStringRepeatedly(benchmark::State& state) {
  Fixture fixture;
  while (state.KeepRunning()) {
    grpc_error_string(fixture.error());
  }
}

template <class Fixture>
static void BM_ErrorGetStatus(benchmark::State& state) {
  Fixture fixture;
  while (state.KeepRunning()) {
    grpc_status_code status;
    const char* msg;
    grpc_error_get_status(fixture.error(), fixture.deadline(), &status, &msg,
                          NULL);
  }
}

template <class Fixture>
static void BM_ErrorGetStatusCode(benchmark::State& state) {
  Fixture fixture;
  while (state.KeepRunning()) {
    grpc_status_code status;
    grpc_error_get_status(fixture.error(), fixture.deadline(), &status, NULL,
                          NULL);
  }
}

template <class Fixture>
static void BM_ErrorHttpError(benchmark::State& state) {
  Fixture fixture;
  while (state.KeepRunning()) {
    grpc_http2_error_code error;
    grpc_error_get_status(fixture.error(), fixture.deadline(), NULL, NULL,
                          &error);
  }
}

template <class Fixture>
static void BM_HasClearGrpcStatus(benchmark::State& state) {
  Fixture fixture;
  while (state.KeepRunning()) {
    grpc_error_has_clear_grpc_status(fixture.error());
  }
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
