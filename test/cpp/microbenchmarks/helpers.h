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

#ifndef TEST_CPP_MICROBENCHMARKS_COUNTERS_H
#define TEST_CPP_MICROBENCHMARKS_COUNTERS_H

#include <sstream>
#include <vector>

#include <grpc/support/port_platform.h>
#include "src/core/lib/debug/stats.h"
#include "test/core/util/memory_counters.h"

#include <benchmark/benchmark.h>
#include <grpcpp/impl/grpc_library.h>

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
extern gpr_atm gpr_mu_locks;
extern gpr_atm gpr_counter_atm_cas;
extern gpr_atm gpr_counter_atm_add;
extern gpr_atm gpr_now_call_count;
#endif

class TrackCounters {
 public:
  TrackCounters() { grpc_stats_collect(&stats_begin_); }
  virtual void Finish(benchmark::State& state);
  virtual void AddLabel(const grpc::string& label);
  virtual void AddToLabel(std::ostream& out, benchmark::State& state);

 private:
  grpc_stats_data stats_begin_;
  std::vector<grpc::string> labels_;
#ifdef GPR_LOW_LEVEL_COUNTERS
  const size_t mu_locks_at_start_ = gpr_atm_no_barrier_load(&gpr_mu_locks);
  const size_t atm_cas_at_start_ =
      gpr_atm_no_barrier_load(&gpr_counter_atm_cas);
  const size_t atm_add_at_start_ =
      gpr_atm_no_barrier_load(&gpr_counter_atm_add);
  const size_t now_calls_at_start_ =
      gpr_atm_no_barrier_load(&gpr_now_call_count);
  grpc_memory_counters counters_at_start_ = grpc_memory_counters_snapshot();
#endif
};

#endif
