/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#ifdef GPR_LINUX

#include <cstdio>

#include "src/cpp/server/load_reporter/get_cpu_stats.h"

namespace grpc {
namespace load_reporter {

std::pair<uint64_t, uint64_t> GetCpuStatsImpl() {
  uint64_t busy = 0, total = 0;
  FILE* fp;
  fp = fopen("/proc/stat", "r");
  uint64_t user, nice, system, idle;
  if (fscanf(fp, "cpu %lu %lu %lu %lu", &user, &nice, &system, &idle) != 4) {
    // Something bad happened with the information, so assume it's all invalid
    user = nice = system = idle = 0;
  }
  fclose(fp);
  busy = user + nice + system;
  total = busy + idle;
  return std::make_pair(busy, total);
}

}  // namespace load_reporter
}  // namespace grpc

#endif  // GPR_LINUX
