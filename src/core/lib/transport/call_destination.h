// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_CALL_DESTINATION_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_CALL_DESTINATION_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/transport/call_spine.h"

namespace grpc_core {

// UnstartedCallDestination is responsible for starting an UnstartedCallHandler
// and then processing operations on the resulting CallHandler.
//
// Examples of UnstartedCallDestinations include:
// - a load-balanced call in the client channel
// - a hijacking filter (see Interceptor)
class UnstartedCallDestination
    : public DualRefCounted<UnstartedCallDestination> {
 public:
  ~UnstartedCallDestination() override = default;
  // Start a call. The UnstartedCallHandler will be consumed by the Destination
  // and started.
  // Must be called from the party owned by the call, eg the following must
  // hold:
  // CHECK(GetContext<Activity>() == unstarted_call_handler.party());
  virtual void StartCall(UnstartedCallHandler unstarted_call_handler) = 0;
};

// CallDestination is responsible for handling processing of an already started
// call.
//
// Examples of CallDestinations include:
// - a client transport
// - the server API
class CallDestination : public DualRefCounted<CallDestination> {
 public:
  virtual void HandleCall(CallHandler unstarted_call_handler) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_CALL_DESTINATION_H
