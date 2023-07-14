//
//
// Copyright 2018 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE

#include <mach/mach.h>

#include "src/cpp/server/load_reporter/get_cpu_stats.h"

namespace grpc {
namespace load_reporter {

std::pair<uint64_t, uint64_t> GetCpuStatsImpl() {
  uint64_t busy = 0, total = 0;
  host_cpu_load_info_data_t cpuinfo;
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                      (host_info_t)&cpuinfo, &count) == KERN_SUCCESS) {
    for (int i = 0; i < CPU_STATE_MAX; i++) total += cpuinfo.cpu_ticks[i];
    busy = total - cpuinfo.cpu_ticks[CPU_STATE_IDLE];
  }
  return std::make_pair(busy, total);
}

}  // namespace load_reporter
}  // namespace grpc

#endif  // GPR_APPLE
