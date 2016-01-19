/*
 *
 * Copyright 2016, Google Inc.
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

#include "test/cpp/qps/limit_cores.h"

#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <vector>

namespace grpc {
namespace testing {

#ifdef GPR_CPU_LINUX
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
int LimitCores(std::vector<int> cores) {
  size_t num_cores = static_cast<size_t>(gpr_cpu_num_cores());
  if (num_cores > cores.size()) {
    cpu_set_t *cpup = CPU_ALLOC(num_cores);
    GPR_ASSERT(cpup);
    size_t size = CPU_ALLOC_SIZE(num_cores);
    CPU_ZERO_S(size, cpup);

    for (size_t i = 0; i < cores.size(); i++) {
      CPU_SET_S(cores[i], size, cpup);
    }
    GPR_ASSERT(sched_setaffinity(0, size, cpup) == 0);
    CPU_FREE(cpup);
    return cores.size();
  } else {
    return num_cores;
  }
}
#else
// LimitCores is not currently supported for non-Linux platforms
int LimitCores(std::vector<int> core_vec) { return gpr_cpu_num_cores(); }
#endif
}  // namespace testing
}  // namespace grpc
