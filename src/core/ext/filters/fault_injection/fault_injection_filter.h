//
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
//

#ifndef GRPC_CORE_EXT_FILTERS_FAULT_INJECTION_FAULT_INJECTION_FILTER_H
#define GRPC_CORE_EXT_FILTERS_FAULT_INJECTION_FAULT_INJECTION_FILTER_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/fault_injection/service_config_parser.h"
#include "src/core/lib/channel/channel_stack.h"

// Channel arg key for enabling parsing fault injection via method config.
#define GRPC_ARG_PARSE_FAULT_INJECTION_METHOD_CONFIG \
  "grpc.parse_fault_injection_method_config"

namespace grpc_core {

// This channel filter is intended to be used by the dynamic filters, instead
// of the ordinary channel stack. The fault injection filter fetches fault
// injection policy from the method config of service config returned by the
// resolver, and enforces the fault injection policy.
extern const grpc_channel_filter FaultInjectionFilterVtable;

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_FAULT_INJECTION_FAULT_INJECTION_FILTER_H
