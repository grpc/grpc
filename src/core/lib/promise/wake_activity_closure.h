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

#ifndef WAKE_ACTIVITY_CLOSURE_H
#define WAKE_ACTIVITY_CLOSURE_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/closure.h"

namespace grpc_core {

// Construct a closure that will wake the current activity when invoked.
grpc_closure* MakeWakeActivityClosure();

}  // namespace grpc_core

#endif
