//
//
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
//
//

#ifndef GRPC_CORE_LIB_CHANNEL_RESOLVED_ADDRESS_UTILS_H
#define GRPC_CORE_LIB_CHANNEL_RESOLVED_ADDRESS_UTILS_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/iomgr/resolved_address.h"

namespace grpc_core {

// Util to encapsulate a grpc_resolved_address in a channel arg.
grpc_arg grpc_resolved_address_to_arg(const char* key,
                                      grpc_resolved_address* addr);

// Util to get the resolved address  from a channel arg.
grpc_resolved_address* grpc_resolved_address_from_arg(const grpc_arg* arg);

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_CHANNEL_RESOLVED_ADDRESS_UTILS_H
