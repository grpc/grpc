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
