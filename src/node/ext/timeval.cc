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

#include <cstdint>
#include <limits>

#include "grpc/grpc.h"
#include "grpc/support/time.h"
#include "timeval.h"

namespace grpc {
namespace node {

gpr_timespec MillisecondsToTimespec(double millis) {
  if (millis == std::numeric_limits<double>::infinity()) {
    return gpr_inf_future(GPR_CLOCK_REALTIME);
  } else if (millis == -std::numeric_limits<double>::infinity()) {
    return gpr_inf_past(GPR_CLOCK_REALTIME);
  } else {
    return gpr_time_from_micros(static_cast<int64_t>(millis * 1000),
                                GPR_CLOCK_REALTIME);
  }
}

double TimespecToMilliseconds(gpr_timespec timespec) {
  timespec = gpr_convert_clock_type(timespec, GPR_CLOCK_REALTIME);
  if (gpr_time_cmp(timespec, gpr_inf_future(GPR_CLOCK_REALTIME)) == 0) {
    return std::numeric_limits<double>::infinity();
  } else if (gpr_time_cmp(timespec, gpr_inf_past(GPR_CLOCK_REALTIME)) == 0) {
    return -std::numeric_limits<double>::infinity();
  } else {
    return (static_cast<double>(timespec.tv_sec) * 1000 +
            static_cast<double>(timespec.tv_nsec) / 1000000);
  }
}

}  // namespace node
}  // namespace grpc
