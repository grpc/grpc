/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_TEST_CORE_UTIL_MEMORY_COUNTERS_H
#define GRPC_TEST_CORE_UTIL_MEMORY_COUNTERS_H

#include <grpc/support/atm.h>

struct grpc_memory_counters {
  gpr_atm total_size_relative;
  gpr_atm total_size_absolute;
  gpr_atm total_allocs_relative;
  gpr_atm total_allocs_absolute;
};

void grpc_memory_counters_init();
void grpc_memory_counters_destroy();
struct grpc_memory_counters grpc_memory_counters_snapshot();

namespace grpc_core {
namespace testing {

// At destruction time, it will check there is no memory leak.
// The object should be created before grpc_init() is called and destroyed after
// grpc_shutdown() is returned.
class LeakDetector {
 public:
  explicit LeakDetector(bool enable);
  ~LeakDetector();

 private:
  const bool enabled_;
};

}  // namespace testing
}  // namespace grpc_core

#endif
