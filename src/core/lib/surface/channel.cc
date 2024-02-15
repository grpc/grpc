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

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/channel.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/init_internally.h"

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

Channel::Channel(std::string target, const ChannelArgs& channel_args,
                 grpc_compression_options compression_options)
    : target_(std::move(target)),
      channelz_node_(channel_args.GetObjectRef<channelz::ChannelNode>()),
      compression_options_(compression_options) {}
  // We need to make sure that grpc_shutdown() does not shut things down
  // until after the channel is destroyed.  However, the channel may not
  // actually be destroyed by the time grpc_channel_destroy() returns,
  // since there may be other existing refs to the channel.  If those
  // refs are held by things that are visible to the wrapped language
  // (such as outstanding calls on the channel), then the wrapped
  // language can be responsible for making sure that grpc_shutdown()
  // does not run until after those refs are released.  However, the
  // channel may also have refs to itself held internally for various
  // things that need to be cleaned up at channel destruction (e.g.,
  // LB policies, subchannels, etc), and because these refs are not
  // visible to the wrapped language, it cannot be responsible for
  // deferring grpc_shutdown() until after they are released.  To
  // accommodate that, we call grpc_init() here and then call
  // grpc_shutdown() when the channel is actually destroyed, thus
  // ensuring that shutdown is deferred until that point.
  InitInternally();
}

Channel::~Channel() {
  if (channelz_node_ != nullptr) {
    channelz_node_->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string("Channel destroyed"));
  }
  ShutdownInternally();
}

RegisteredCall* Channel::RegisterCall(const char* method, const char* host) {
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

// FIXME: move to another file
absl::StatusOr<RefCountedPtr<Channel>> Channel::Create(
    std::string target, ChannelArgs args,
    grpc_channel_stack_type channel_stack_type, Transport* optional_transport) {
  global_stats().IncrementClientChannelsCreated();
  // Set default authority if needed.
  if (!args.GetString(GRPC_ARG_DEFAULT_AUTHORITY).has_value()) {
    auto ssl_override = args.GetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
    if (ssl_override.has_value()) {
      args = args.Set(GRPC_ARG_DEFAULT_AUTHORITY,
                      std::string(ssl_override.value()));
    }
  }
  // Check whether channelz is enabled.
  if (args.GetBool(GRPC_ARG_ENABLE_CHANNELZ)
          .value_or(GRPC_ENABLE_CHANNELZ_DEFAULT)) {
    // Get parameters needed to create the channelz node.
    const size_t channel_tracer_max_memory = std::max(
        0,
        args.GetInt(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE)
            .value_or(GRPC_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE_DEFAULT));
    const bool is_internal_channel =
        args.GetBool(GRPC_ARG_CHANNELZ_IS_INTERNAL_CHANNEL).value_or(false);
    // Create the channelz node.
    std::string channelz_node_target{target.empty() ? "unknown" : target};
    auto channelz_node = MakeRefCounted<channelz::ChannelNode>(
        channelz_node_target, channel_tracer_max_memory,
        is_internal_channel);
    channelz_node->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string("Channel created"));
    // Add channelz node to channel args.
    // We remove the is_internal_channel arg, since we no longer need it.
    args = args.Remove(GRPC_ARG_CHANNELZ_IS_INTERNAL_CHANNEL)
               .SetObject(channelz_node);
  }
  // Set compression options.
  grpc_compression_options compression_options;
  grpc_compression_options_init(&compression_options);
  auto default_level = args.GetInt(GRPC_COMPRESSION_CHANNEL_DEFAULT_LEVEL);
  if (default_level.has_value()) {
    compression_options.default_level.is_set = true;
    compression_options.default_level.level = Clamp(
        static_cast<grpc_compression_level>(*default_level),
        GRPC_COMPRESS_LEVEL_NONE,
        static_cast<grpc_compression_level>(GRPC_COMPRESS_LEVEL_COUNT - 1));
  }
  auto default_algorithm =
      args.GetInt(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM);
  if (default_algorithm.has_value()) {
    compression_options.default_algorithm.is_set = true;
    compression_options.default_algorithm.algorithm =
        Clamp(static_cast<grpc_compression_algorithm>(*default_algorithm),
              GRPC_COMPRESS_NONE,
              static_cast<grpc_compression_algorithm>(
                  GRPC_COMPRESS_ALGORITHMS_COUNT - 1));
  }
  auto enabled_algorithms_bitset =
      args.GetInt(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET);
  if (enabled_algorithms_bitset.has_value()) {
    compression_options.enabled_algorithms_bitset =
        *enabled_algorithms_bitset | 1 /* always support no compression */;
  }
  // Add transport to args.
  if (optional_transport != nullptr) {
    args = args.SetObject(optional_transport);
  }
  // Delegate to channel impl.
  if (!IsCallV3Enabled()) {
    return LegacyChannel::Create(std::move(target), std::move(args),
                                 channel_stack_type, compression_options);
  }
  return ChannelImpl::Create(std::move(target), std::move(args),
                             channel_stack_type, compression_options);
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
  GPR_ASSERT(!reserved);
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

grpc_call* grpc_channel_create_pollset_set_call(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_pollset_set* pollset_set, const grpc_slice& method,
    const grpc_slice* host, grpc_core::Timestamp deadline, void* reserved) {
  GPR_ASSERT(!reserved);
  return grpc_core::Channel::FromC(channel)->CreateCall(
      parent_call, propagation_mask, nullptr, pollset_set,
      grpc_core::Slice(method),
      host != nullptr
          ? absl::optional<grpc_core::Slice>(grpc_core::CSliceRef(*host))
          : absl::nullopt,
      deadline, /*registered_method=*/true);
}

void* grpc_channel_register_call(grpc_channel* channel, const char* method,
                                 const char* host, void* reserved) {
  GRPC_API_TRACE(
      "grpc_channel_register_call(channel=%p, method=%s, host=%s, reserved=%p)",
      4, (channel, method, host, reserved));
  GPR_ASSERT(!reserved);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  return grpc_core::Channel::FromC(channel)->RegisterCall(method, host);
}

grpc_call* grpc_channel_create_registered_call(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* completion_queue, void* registered_call_handle,
    gpr_timespec deadline, void* reserved) {
  grpc_core::RegisteredCall* rc =
      static_cast<grpc_core::RegisteredCall*>(registered_call_handle);
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
  GPR_ASSERT(!reserved);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  return grpc_core::Channel::FromC(channel)->CreateCall(
      parent_call, propagation_mask, completion_queue, nullptr,
      rc->path.Ref(),
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
      (c_channel, try_to_connect));
  return grpc_core::Channel::FromC(channel)->CheckConnectivityState(
      try_to_connect);
}

void grpc_channel_watch_connectivity_state(
    grpc_channel* channel, grpc_connectivity_state last_observed_state,
    gpr_timespec deadline, grpc_completion_queue* cq, void* tag) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE(
      "grpc_channel_watch_connectivity_state(
      "channel=%p, last_observed_state=%d, "
      "deadline=gpr_timespec { tv_sec: %" PRId64
      ", tv_nsec: %d, clock_type: %d }, "
      "cq=%p, tag=%p)",
      7,
      (channel, (int)last_observed_state, deadline.tv_sec, deadline.tv_nsec,
       (int)deadline.clock_type, cq, tag));
  return grpc_core::Channel::FromC(channel)->WatchConnectivityState(
      last_observed_state, Timestamp::FromTimespecRoundUp(deadline), cq, tag);
}

void grpc_channel_ping(grpc_channel* channel, grpc_completion_queue* cq,
                       void* tag, void* reserved) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_channel_ping(channel=%p, cq=%p, tag=%p, reserved=%p)", 4,
                 (channel, cq, tag, reserved));
  GPR_ASSERT(reserved == nullptr);
  grpc_core::Channel::FromC(channel)->Ping(cq, tag);
}
