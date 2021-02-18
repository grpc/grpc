//
// Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_FILTER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_FILTER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_stack.h"

extern grpc_core::TraceFlag grpc_fault_injection_filter_trace;

// This channel filter is intended to be used by the dynamic filters, instead
// of the ordinary channel stack. The fault injection filter fetches fault
// injection policy from the method config of service config returned by the
// xDS config selector, and enforces the fault injection policy.
extern const grpc_channel_filter grpc_fault_injection_filter;

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_FILTER_H
