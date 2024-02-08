//
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
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/channel.h"

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>

#include "absl/status/status.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/init_internally.h"
#include "src/core/lib/transport/call_factory.h"
#include "src/core/lib/transport/transport.h"

// IWYU pragma: no_include <type_traits>

namespace grpc_core {

namespace {

class NotReallyACallFactory final : public CallFactory {
 public:
  using CallFactory::CallFactory;
  CallInitiator CreateCall(ClientMetadataHandle, Arena*) override {
    Crash("NotReallyACallFactory::CreateCall should never be called");
  }
};

}  // namespace

Channel::Channel(bool is_client, bool is_promising, std::string target,
                 const ChannelArgs& channel_args,
                 grpc_compression_options compression_options,
                 RefCountedPtr<grpc_channel_stack> channel_stack)
    : is_client_(is_client),
      is_promising_(is_promising),
      compression_options_(compression_options),
      channelz_node_(channel_args.GetObjectRef<channelz::ChannelNode>()),
      target_(std::move(target)),
      channel_stack_(std::move(channel_stack)),
      call_factory_(MakeRefCounted<NotReallyACallFactory>(channel_args)) {
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
  auto channelz_node = channelz_node_;
  *channel_stack_->on_destroy = [channelz_node]() {
    if (channelz_node != nullptr) {
      channelz_node->AddTraceEvent(
          channelz::ChannelTrace::Severity::Info,
          grpc_slice_from_static_string("Channel destroyed"));
    }
    ShutdownInternally();
  };
}

absl::StatusOr<RefCountedPtr<Channel>> Channel::CreateWithBuilder(
    ChannelStackBuilder* builder) {
  auto channel_args = builder->channel_args();
  if (builder->channel_stack_type() == GRPC_SERVER_CHANNEL) {
    global_stats().IncrementServerChannelsCreated();
  } else {
    global_stats().IncrementClientChannelsCreated();
  }
  absl::StatusOr<RefCountedPtr<grpc_channel_stack>> r = builder->Build();
  if (!r.ok()) {
    auto status = r.status();
    gpr_log(GPR_ERROR, "channel stack builder failed: %s",
            status.ToString().c_str());
    return status;
  }

  grpc_compression_options compression_options;
  grpc_compression_options_init(&compression_options);
  auto default_level =
      channel_args.GetInt(GRPC_COMPRESSION_CHANNEL_DEFAULT_LEVEL);
  if (default_level.has_value()) {
    compression_options.default_level.is_set = true;
    compression_options.default_level.level = Clamp(
        static_cast<grpc_compression_level>(*default_level),
        GRPC_COMPRESS_LEVEL_NONE,
        static_cast<grpc_compression_level>(GRPC_COMPRESS_LEVEL_COUNT - 1));
  }
  auto default_algorithm =
      channel_args.GetInt(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM);
  if (default_algorithm.has_value()) {
    compression_options.default_algorithm.is_set = true;
    compression_options.default_algorithm.algorithm =
        Clamp(static_cast<grpc_compression_algorithm>(*default_algorithm),
              GRPC_COMPRESS_NONE,
              static_cast<grpc_compression_algorithm>(
                  GRPC_COMPRESS_ALGORITHMS_COUNT - 1));
  }
  auto enabled_algorithms_bitset =
      channel_args.GetInt(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET);
  if (enabled_algorithms_bitset.has_value()) {
    compression_options.enabled_algorithms_bitset =
        *enabled_algorithms_bitset | 1 /* always support no compression */;
  }

  return RefCountedPtr<Channel>(new Channel(
      grpc_channel_stack_type_is_client(builder->channel_stack_type()),
      builder->IsPromising(), std::string(builder->target()), channel_args,
      compression_options, std::move(*r)));
}

namespace {

void* channelz_node_copy(void* p) {
  channelz::ChannelNode* node = static_cast<channelz::ChannelNode*>(p);
  node->Ref().release();
  return p;
}
void channelz_node_destroy(void* p) {
  channelz::ChannelNode* node = static_cast<channelz::ChannelNode*>(p);
  node->Unref();
}
int channelz_node_cmp(void* p1, void* p2) { return QsortCompare(p1, p2); }
const grpc_arg_pointer_vtable channelz_node_arg_vtable = {
    channelz_node_copy, channelz_node_destroy, channelz_node_cmp};
}  // namespace

absl::StatusOr<RefCountedPtr<Channel>> Channel::Create(
    const char* target, ChannelArgs args,
    grpc_channel_stack_type channel_stack_type, Transport* optional_transport) {
  if (!args.GetString(GRPC_ARG_DEFAULT_AUTHORITY).has_value()) {
    auto ssl_override = args.GetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
    if (ssl_override.has_value()) {
      args = args.Set(GRPC_ARG_DEFAULT_AUTHORITY,
                      std::string(ssl_override.value()));
    }
  }
  if (grpc_channel_stack_type_is_client(channel_stack_type)) {
    auto channel_args_mutator =
        grpc_channel_args_get_client_channel_creation_mutator();
    if (channel_args_mutator != nullptr) {
      args = channel_args_mutator(target, args, channel_stack_type);
    }
  }
  // We only need to do this for clients here. For servers, this will be
  // done in src/core/lib/surface/server.cc.
  if (grpc_channel_stack_type_is_client(channel_stack_type)) {
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
      std::string channelz_node_target{target == nullptr ? "unknown" : target};
      RefCountedPtr<channelz::ChannelNode> channelz_node =
          MakeRefCounted<channelz::ChannelNode>(channelz_node_target,
                                                channel_tracer_max_memory,
                                                is_internal_channel);
      channelz_node->AddTraceEvent(
          channelz::ChannelTrace::Severity::Info,
          grpc_slice_from_static_string("Channel created"));
      // Add channelz node to channel args.
      // We remove the is_internal_channel arg, since we no longer need it.
      args = args.Remove(GRPC_ARG_CHANNELZ_IS_INTERNAL_CHANNEL)
                 .Set(GRPC_ARG_CHANNELZ_CHANNEL_NODE,
                      ChannelArgs::Pointer(channelz_node.release(),
                                           &channelz_node_arg_vtable));
    }
  }
  ChannelStackBuilderImpl builder(
      grpc_channel_stack_type_string(channel_stack_type), channel_stack_type,
      args.SetObject(optional_transport));
  builder.SetTarget(target);
  if (!CoreConfiguration::Get().channel_init().CreateStack(&builder)) {
    return nullptr;
  }
  return CreateWithBuilder(&builder);
}

}  // namespace grpc_core

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
  grpc_channel_element* elem = grpc_channel_stack_element(
      grpc_core::Channel::FromC(channel)->channel_stack(), 0);
  elem->filter->get_channel_info(elem, channel_info);
}

void grpc_channel_reset_connect_backoff(grpc_channel* channel) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_channel_reset_connect_backoff(channel=%p)", 1,
                 (channel));
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  op->reset_connect_backoff = true;
  grpc_channel_element* elem = grpc_channel_stack_element(
      grpc_core::Channel::FromC(channel)->channel_stack(), 0);
  elem->filter->start_transport_op(elem, op);
}

static grpc_call* grpc_channel_create_call_internal(
    grpc_channel* c_channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* cq, grpc_pollset_set* pollset_set_alternative,
    grpc_core::Slice path, absl::optional<grpc_core::Slice> authority,
    grpc_core::Timestamp deadline, bool registered_method) {
  auto channel = grpc_core::Channel::FromC(c_channel)->Ref();
  GPR_ASSERT(channel->is_client());
  GPR_ASSERT(!(cq != nullptr && pollset_set_alternative != nullptr));

  grpc_call_create_args args;
  args.channel = std::move(channel);
  args.server = nullptr;
  args.parent = parent_call;
  args.propagation_mask = propagation_mask;
  args.cq = cq;
  args.pollset_set_alternative = pollset_set_alternative;
  args.server_transport_data = nullptr;
  args.path = std::move(path);
  args.authority = std::move(authority);
  args.send_deadline = deadline;
  args.registered_method = registered_method;

  grpc_call* call;
  GRPC_LOG_IF_ERROR("call_create", grpc_call_create(&args, &call));
  return call;
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
  grpc_call* call = grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, completion_queue, nullptr,
      grpc_core::Slice(grpc_core::CSliceRef(method)),
      host != nullptr
          ? absl::optional<grpc_core::Slice>(grpc_core::CSliceRef(*host))
          : absl::nullopt,
      grpc_core::Timestamp::FromTimespecRoundUp(deadline),
      /*registered_method=*/false);

  return call;
}

grpc_call* grpc_channel_create_pollset_set_call(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_pollset_set* pollset_set, const grpc_slice& method,
    const grpc_slice* host, grpc_core::Timestamp deadline, void* reserved) {
  GPR_ASSERT(!reserved);
  return grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, nullptr, pollset_set,
      grpc_core::Slice(method),
      host != nullptr
          ? absl::optional<grpc_core::Slice>(grpc_core::CSliceRef(*host))
          : absl::nullopt,
      deadline, /*registered_method=*/true);
}

namespace grpc_core {

RegisteredCall::RegisteredCall(const char* method_arg, const char* host_arg) {
  path = Slice::FromCopiedString(method_arg);
  if (host_arg != nullptr && host_arg[0] != 0) {
    authority = Slice::FromCopiedString(host_arg);
  }
}

RegisteredCall::RegisteredCall(const RegisteredCall& other)
    : path(other.path.Ref()) {
  if (other.authority.has_value()) {
    authority = other.authority->Ref();
  }
}

RegisteredCall::~RegisteredCall() {}

}  // namespace grpc_core

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

namespace grpc_core {

RegisteredCall* Channel::RegisterCall(const char* method, const char* host) {
  MutexLock lock(&registration_table_.mu);
  auto key = std::make_pair(std::string(host != nullptr ? host : ""),
                            std::string(method != nullptr ? method : ""));
  auto rc_posn = registration_table_.map.find(key);
  if (rc_posn != registration_table_.map.end()) {
    return &rc_posn->second;
  }
  auto insertion_result = registration_table_.map.insert(
      {std::move(key), RegisteredCall(method, host)});
  return &insertion_result.first->second;
}

}  // namespace grpc_core

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
  grpc_call* call = grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, completion_queue, nullptr,
      rc->path.Ref(),
      rc->authority.has_value()
          ? absl::optional<grpc_core::Slice>(rc->authority->Ref())
          : absl::nullopt,
      grpc_core::Timestamp::FromTimespecRoundUp(deadline),
      /*registered_method=*/true);

  return call;
}

void grpc_channel_destroy_internal(grpc_channel* c_channel) {
  grpc_core::RefCountedPtr<grpc_core::Channel> channel(
      grpc_core::Channel::FromC(c_channel));
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  grpc_channel_element* elem;
  GRPC_API_TRACE("grpc_channel_destroy(channel=%p)", 1, (c_channel));
  op->disconnect_with_error = GRPC_ERROR_CREATE("Channel Destroyed");
  elem = grpc_channel_stack_element(channel->channel_stack(), 0);
  elem->filter->start_transport_op(elem, op);
}

void grpc_channel_destroy(grpc_channel* channel) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_destroy_internal(channel);
}
