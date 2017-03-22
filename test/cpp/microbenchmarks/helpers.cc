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

#include "test/cpp/microbenchmarks/helpers.h"

void TrackCounters::Finish(benchmark::State &state) {
  std::ostringstream out;
  AddToLabel(out, state);
  auto label = out.str();
  if (label.length() && label[0] == ' ') {
    label = label.substr(1);
  }
  state.SetLabel(label);
}

void TrackCounters::AddToLabel(std::ostream &out, benchmark::State &state) {
#ifdef GPR_LOW_LEVEL_COUNTERS
  grpc_memory_counters counters_at_end = grpc_memory_counters_snapshot();
  out << " locks/iter:" << ((double)(gpr_atm_no_barrier_load(&gpr_mu_locks) -
                                     mu_locks_at_start_) /
                            (double)state.iterations())
      << " atm_cas/iter:"
      << ((double)(gpr_atm_no_barrier_load(&gpr_counter_atm_cas) -
                   atm_cas_at_start_) /
          (double)state.iterations())
      << " atm_add/iter:"
      << ((double)(gpr_atm_no_barrier_load(&gpr_counter_atm_add) -
                   atm_add_at_start_) /
          (double)state.iterations())
      << " allocs/iter:"
      << ((double)(counters_at_end.total_allocs_absolute -
                   counters_at_start_.total_allocs_absolute) /
          (double)state.iterations());
#endif
}
