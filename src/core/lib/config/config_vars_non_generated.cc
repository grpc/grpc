// Copyright 2022 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <atomic>
#include <string>

#include "src/core/lib/config/config_vars.h"

namespace grpc_core {

std::atomic<ConfigVars*> ConfigVars::config_vars_{nullptr};

const ConfigVars& ConfigVars::Load() {
  // Called from get, so we know there's no existing config vars.
  // We might race for them though.
  auto vars = new ConfigVars({});
  ConfigVars* expected = nullptr;
  if (!config_vars_.compare_exchange_strong(expected, vars,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
    delete vars;
    return *expected;
  }
  return *vars;
}

void ConfigVars::Reset() {
  delete config_vars_.exchange(nullptr, std::memory_order_acq_rel);
}

void ConfigVars::SetOverrides(const Overrides& overrides) {
  delete config_vars_.exchange(new ConfigVars(overrides),
                               std::memory_order_acq_rel);
}

}  // namespace grpc_core
