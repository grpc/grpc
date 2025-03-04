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

#if !defined(GPR_LINUX) && !defined(GPR_WINDOWS)

#include "absl/log/log.h"
#include "src/core/credentials/transport/alts/check_gcp_environment.h"
#include "src/core/util/crash.h"

bool grpc_alts_is_running_on_gcp() {
  VLOG(2) << "ALTS: Platforms other than Linux and Windows are not supported";
  return false;
}

#endif  // !defined(LINUX) && !defined(GPR_WINDOWS)
