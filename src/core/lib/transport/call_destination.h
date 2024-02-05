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

// CallDestination is responsible for the processing of a CallHandler.
// It might be a transport, the server API, or a subchannel on the client (for
// instance).
class CallDestination : public Orphanable {
 public:
  virtual void StartCall(CallHandler call_handler) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_CALL_DESTINATION_H
