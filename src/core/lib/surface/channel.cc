//
// Copyright 2015 gRPC authors.
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

#include "src/core/lib/surface/channel.h"

#include "absl/log/check.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/channelz/channel_trace.h"
#include "src/core/channelz/channelz.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"

namespace grpc_core {

//
// Channel::RegisteredCall
//

Channel::RegisteredCall::RegisteredCall(const char* method_arg,
                                        const char* host_arg) {
  path = Slice::FromCopiedString(method_arg);
  if (host_arg != nullptr && host_arg[0] != 0) {
    authority = Slice::FromCopiedString(host_arg);
  }
}

Channel::RegisteredCall::RegisteredCall(const RegisteredCall& other)
    : path(other.path.Ref()) {
  if (other.authority.has_value()) {
    authority = other.authority->Ref();
  }
}

Channel::RegisteredCall::~RegisteredCall() {}

//
// Channel
//

Channel::Channel(std::string target, const ChannelArgs& channel_args)
    : target_(std::move(target)),
      channelz_node_(channel_args.GetObjectRef<channelz::ChannelNode>()),
      compression_options_(CompressionOptionsFromChannelArgs(channel_args)),
      call_arena_allocator_(MakeRefCounted<CallArenaAllocator>(
          channel_args.GetObject<ResourceQuota>()
              ->memory_quota()
              ->CreateMemoryOwner(),
          1024)) {}

Channel::RegisteredCall* Channel::RegisterCall(const char* method,
                                               const char* host) {
  MutexLock lock(&mu_);
  auto key = std::make_pair(std::string(host != nullptr ? host : ""),
                            std::string(method != nullptr ? method : ""));
  auto rc_posn = registration_table_.find(key);
  if (rc_posn != registration_table_.end()) {
    return &rc_posn->second;
  }
  auto insertion_result = registration_table_.insert(
      {std::move(key), RegisteredCall(method, host)});
  return &insertion_result.first->second;
}

}  // namespace grpc_core

//
// C-core API
//

void grpc_channel_destroy(grpc_channel* channel) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_channel_destroy(channel=%p)", 1, (channel));
  grpc_channel_destroy_internal(channel);
}

grpc_call* grpc_channel_create_call(grpc_channel* channel,
                                    grpc_call* parent_call,
                                    uint32_t propagation_mask,
                                    grpc_completion_queue* completion_queue,
                                    grpc_slice method, const grpc_slice* host,
                                    gpr_timespec deadline, void* reserved) {
  CHECK(!reserved);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  return grpc_core::Channel::FromC(channel)->CreateCall(
      parent_call, propagation_mask, completion_queue, nullptr,
      grpc_core::Slice(grpc_core::CSliceRef(method)),
      host != nullptr
          ? absl::optional<grpc_core::Slice>(grpc_core::CSliceRef(*host))
          : absl::nullopt,
      grpc_core::Timestamp::FromTimespecRoundUp(deadline),
      /*registered_method=*/false);
}

void* grpc_channel_register_call(grpc_channel* channel, const char* method,
                                 const char* host, void* reserved) {
  GRPC_API_TRACE(
      "grpc_channel_register_call(channel=%p, method=%s, host=%s, reserved=%p)",
      4, (channel, method, host, reserved));
  CHECK(!reserved);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  return grpc_core::Channel::FromC(channel)->RegisterCall(method, host);
}

grpc_call* grpc_channel_create_registered_call(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* completion_queue, void* registered_call_handle,
    gpr_timespec deadline, void* reserved) {
  auto* rc =
      static_cast<grpc_core::Channel::RegisteredCall*>(registered_call_handle);
  GRPC_API_TRACE(
      "grpc_channel_create_registered_call("
      "channel=%p, parent_call=%p, propagation_mask=%x, completion_queue=%p, "
      "registered_call_handle=%p, "
      "deadline=gpr_timespec { tv_sec: %" PRId64
      ", tv_nsec: %d, clock_type: %d }, "
      "reserved=%p)",
      9,
      (channel, parent_call, (unsigned)propagation_mask, completion_queue,
       registered_call_handle, deadline.tv_sec, deadline.tv_nsec,
       (int)deadline.clock_type, reserved));
  CHECK(!reserved);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  return grpc_core::Channel::FromC(channel)->CreateCall(
      parent_call, propagation_mask, completion_queue, nullptr, rc->path.Ref(),
      rc->authority.has_value()
          ? absl::optional<grpc_core::Slice>(rc->authority->Ref())
          : absl::nullopt,
      grpc_core::Timestamp::FromTimespecRoundUp(deadline),
      /*registered_method=*/true);
}

char* grpc_channel_get_target(grpc_channel* channel) {
  GRPC_API_TRACE("grpc_channel_get_target(channel=%p)", 1, (channel));
  auto target = grpc_core::Channel::FromC(channel)->target();
  char* buffer = static_cast<char*>(gpr_zalloc(target.size() + 1));
  memcpy(buffer, target.data(), target.size());
  return buffer;
}

void grpc_channel_get_info(grpc_channel* channel,
                           const grpc_channel_info* channel_info) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Channel::FromC(channel)->GetInfo(channel_info);
}

void grpc_channel_reset_connect_backoff(grpc_channel* channel) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_channel_reset_connect_backoff(channel=%p)", 1,
                 (channel));
  grpc_core::Channel::FromC(channel)->ResetConnectionBackoff();
}

int grpc_channel_support_connectivity_watcher(grpc_channel* channel) {
  return grpc_core::Channel::FromC(channel)->SupportsConnectivityWatcher();
}

grpc_connectivity_state grpc_channel_check_connectivity_state(
    grpc_channel* channel, int try_to_connect) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE(
      "grpc_channel_check_connectivity_state(channel=%p, try_to_connect=%d)", 2,
      (channel, try_to_connect));
  return grpc_core::Channel::FromC(channel)->CheckConnectivityState(
      try_to_connect);
}

void grpc_channel_watch_connectivity_state(
    grpc_channel* channel, grpc_connectivity_state last_observed_state,
    gpr_timespec deadline, grpc_completion_queue* cq, void* tag) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE(
      "grpc_channel_watch_connectivity_state("
      "channel=%p, last_observed_state=%d, "
      "deadline=gpr_timespec { tv_sec: %" PRId64
      ", tv_nsec: %d, clock_type: %d }, "
      "cq=%p, tag=%p)",
      7,
      (channel, (int)last_observed_state, deadline.tv_sec, deadline.tv_nsec,
       (int)deadline.clock_type, cq, tag));
  return grpc_core::Channel::FromC(channel)->WatchConnectivityState(
      last_observed_state, grpc_core::Timestamp::FromTimespecRoundUp(deadline),
      cq, tag);
}

void grpc_channel_ping(grpc_channel* channel, grpc_completion_queue* cq,
                       void* tag, void* reserved) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_channel_ping(channel=%p, cq=%p, tag=%p, reserved=%p)", 4,
                 (channel, cq, tag, reserved));
  CHECK_EQ(reserved, nullptr);
  grpc_core::Channel::FromC(channel)->Ping(cq, tag);
}
