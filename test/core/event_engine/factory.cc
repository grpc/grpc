// Copyright 2022 The gRPC Authors
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

#include "test/core/event_engine/factory.h"

#include "src/core/lib/event_engine/event_engine_factory.h"
#include "src/core/lib/gpr/string.h"

GPR_GLOBAL_CONFIG_DEFINE_STRING(
    testonly_grpc_eventengine_strategy, "default",
    "Declares which EventEngine to use with gRPC tests.")

namespace grpc_event_engine {
namespace experimental {

void InitTestEventEngineFactory() {
  grpc_core::UniquePtr<char> engine_name =
      GPR_GLOBAL_CONFIG_GET(testonly_grpc_eventengine_strategy);
  if (strlen(engine_name.get()) == 0 ||
      gpr_stricmp("default", engine_name.get()) == 0) {
    // Set the default EventEngine factory
    // TODO(hork): SetDefaultEventEngineFactory(LibuvEventEngineFactory)
    gpr_log(GPR_DEBUG, "Libuv EventEngine initialized.");
  } else {
    gpr_log(GPR_ERROR,
            "Invalid EventEngine '%s'. See doc/environment_variables.md",
            engine_name.get());
    GPR_ASSERT(false);
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine
