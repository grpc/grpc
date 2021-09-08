// Copyright 2021 The gRPC Authors
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

#ifdef GRPC_USE_EVENT_ENGINE
#include <stdlib.h>

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/port.h"

grpc_endpoint_pair grpc_iomgr_create_endpoint_pair(
    const char* /* name */, grpc_channel_args* /* args */) {
  // TODO(hork): determine what's needed here in the long run
  GPR_ASSERT(
      false &&
      "grpc_iomgr_create_endpoint_pair is not suppoted with event_engine");
}

#endif  // GRPC_USE_EVENT_ENGINE
