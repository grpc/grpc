//
//
// Copyright 2026 gRPC authors.
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

#include "src/core/client_channel/virtual_channel.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_create.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/transport/session_endpoint.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/status/statusor.h"

namespace grpc_core {

absl::StatusOr<RefCountedPtr<Channel>> VirtualChannel::Create(
    grpc_call* call, ChannelArgs args) {
  Call* core_call = Call::FromC(call);

  // TODO(snohria): Add support for Call V3.
  GRPC_CHECK(core_call->call_stack() != nullptr);

  auto event_engine =
      core_call->arena()
          ->GetContext<grpc_event_engine::experimental::EventEngine>()
          ->shared_from_this();

  args = args.SetObject(event_engine);

  auto legacy_endpoint = SessionEndpoint::Create(call, true);
  auto transport = grpc_create_chttp2_transport(
      args, OrphanablePtr<grpc_endpoint>(legacy_endpoint), true);
  // TODO(snohria): Implement a new channel type for virtual channels.
  auto channel = ChannelCreate("virtual_target", args,
                               GRPC_CLIENT_DIRECT_CHANNEL, transport);
  if (!channel.ok()) {
    return channel.status();
  }

  // TODO(snohria): Is something non-nullptr needed here?
  grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr,
                                      nullptr);
  return channel;
}

}  // namespace grpc_core
