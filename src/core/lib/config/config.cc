// Copyright 2021 gRPC authors.
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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/lib/config/config.h"

namespace grpc_core {

std::atomic<CoreConfiguration*> CoreConfiguration::config_;

CoreConfiguration::Builder::Builder() = default;

CoreConfiguration* CoreConfiguration::Builder::Build() {
  return new CoreConfiguration;
}

CoreConfiguration::CoreConfiguration() = default;

const CoreConfiguration& CoreConfiguration::BuildNewAndMaybeSet() {
  Builder builder;
  BuildCoreConfiguration(&builder);
  CoreConfiguration* p = builder.Build();
  CoreConfiguration* expected = nullptr;
  if (!config_.compare_exchange_strong(expected, p, std::memory_order_release)) {
    delete p;
    return *expected;
  }
  return *p;
}

void CoreConfiguration::Reset() {
  delete config_.exchange(nullptr, std::memory_order_acquire);
}

}


