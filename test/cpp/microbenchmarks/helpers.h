/*
 *
 * Copyright 2017, Google Inc.
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

#ifndef TEST_CPP_MICROBENCHMARKS_COUNTERS_H
#define TEST_CPP_MICROBENCHMARKS_COUNTERS_H

#include <sstream>

extern "C" {
#include <grpc/support/port_platform.h>
#include "test/core/util/memory_counters.h"
}

#include <benchmark/benchmark.h>
#include <grpc++/impl/grpc_library.h>

class Library {
 public:
  static Library& get() {
    static Library lib;
    return lib;
  }

  grpc_resource_quota* rq() { return rq_; }

 private:
  Library() {
#ifdef GPR_LOW_LEVEL_COUNTERS
    grpc_memory_counters_init();
#endif
    init_lib_.init();
    rq_ = grpc_resource_quota_create("bm");
  }

  ~Library() { init_lib_.shutdown(); }

  grpc::internal::GrpcLibrary init_lib_;
  grpc_resource_quota* rq_;
};

#ifdef GPR_LOW_LEVEL_COUNTERS
extern "C" gpr_atm gpr_mu_locks;
extern "C" gpr_atm gpr_counter_atm_cas;
extern "C" gpr_atm gpr_counter_atm_add;
#endif

class TrackCounters {
 public:
  virtual void Finish(benchmark::State& state);
  virtual void AddToLabel(std::ostream& out, benchmark::State& state);

 private:
#ifdef GPR_LOW_LEVEL_COUNTERS
  const size_t mu_locks_at_start_ = gpr_atm_no_barrier_load(&gpr_mu_locks);
  const size_t atm_cas_at_start_ =
      gpr_atm_no_barrier_load(&gpr_counter_atm_cas);
  const size_t atm_add_at_start_ =
      gpr_atm_no_barrier_load(&gpr_counter_atm_add);
  grpc_memory_counters counters_at_start_ = grpc_memory_counters_snapshot();
#endif
};

#endif
