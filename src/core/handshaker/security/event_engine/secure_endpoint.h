//
//
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
//
//

#ifndef GRPC_SRC_CORE_HANDSHAKER_SECURITY_EVENT_ENGINE_SECURE_ENDPOINT_H
#define GRPC_SRC_CORE_HANDSHAKER_SECURITY_EVENT_ENGINE_SECURE_ENDPOINT_H

#include <stddef.h>

#include <memory>

#include "include/grpc/event_engine/event_engine.h"
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/tsi/transport_security_interface.h"

namespace grpc_core {

// Creates a secure endpoint around `to_wrap`. Uses `protector` and
// `zero_copy_protector` to protect and unprotect data passed to and from the
// endpoint.
std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
CreateSecureEndpoint(
    tsi_frame_protector* protector,
    tsi_zero_copy_grpc_protector* zero_copy_protector,
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        to_wrap,
    grpc_event_engine::experimental::Slice* leftover_slices,
    const grpc_core::ChannelArgs& channel_args, size_t leftover_nslices);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_HANDSHAKER_SECURITY_EVENT_ENGINE_SECURE_ENDPOINT_H
