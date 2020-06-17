/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/channel.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/static_metadata.h"

/** Cache grpc-status: X mdelems for X = 0..NUM_CACHED_STATUS_ELEMS.
 *  Avoids needing to take a metadata context lock for sending status
 *  if the status code is <= NUM_CACHED_STATUS_ELEMS.
 *  Sized to allow the most commonly used codes to fit in
 *  (OK, Cancelled, Unknown). */
#define NUM_CACHED_STATUS_ELEMS 3

static void destroy_channel(void* arg, grpc_error* error);

grpc_channel* grpc_channel_create_with_builder(
    grpc_channel_stack_builder* builder,
    grpc_channel_stack_type channel_stack_type) {
  char* target = gpr_strdup(grpc_channel_stack_builder_get_target(builder));
  grpc_channel_args* args = grpc_channel_args_copy(
      grpc_channel_stack_builder_get_channel_arguments(builder));
  grpc_resource_user* resource_user =
      grpc_channel_stack_builder_get_resource_user(builder);
  grpc_channel* channel;
  if (channel_stack_type == GRPC_SERVER_CHANNEL) {
    GRPC_STATS_INC_SERVER_CHANNELS_CREATED();
  } else {
    GRPC_STATS_INC_CLIENT_CHANNELS_CREATED();
  }
  grpc_error* error = grpc_channel_stack_builder_finish(
      builder, sizeof(grpc_channel), 1, destroy_channel, nullptr,
      reinterpret_cast<void**>(&channel));
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "channel stack builder failed: %s",
            grpc_error_string(error));
    GRPC_ERROR_UNREF(error);
    gpr_free(target);
    grpc_channel_args_destroy(args);
    return channel;
  }
  channel->target = target;
  channel->resource_user = resource_user;
  channel->is_client = grpc_channel_stack_type_is_client(channel_stack_type);
  channel->registration_table.Init();

  gpr_atm_no_barrier_store(
      &channel->call_size_estimate,
      (gpr_atm)CHANNEL_STACK_FROM_CHANNEL(channel)->call_stack_size +
          grpc_call_get_initial_size_estimate());

  grpc_compression_options_init(&channel->compression_options);
  for (size_t i = 0; i < args->num_args; i++) {
    if (0 ==
        strcmp(args->args[i].key, GRPC_COMPRESSION_CHANNEL_DEFAULT_LEVEL)) {
      channel->compression_options.default_level.is_set = true;
      channel->compression_options.default_level.level =
          static_cast<grpc_compression_level>(grpc_channel_arg_get_integer(
              &args->args[i],
              {GRPC_COMPRESS_LEVEL_NONE, GRPC_COMPRESS_LEVEL_NONE,
               GRPC_COMPRESS_LEVEL_COUNT - 1}));
    } else if (0 == strcmp(args->args[i].key,
                           GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM)) {
      channel->compression_options.default_algorithm.is_set = true;
      channel->compression_options.default_algorithm.algorithm =
          static_cast<grpc_compression_algorithm>(grpc_channel_arg_get_integer(
              &args->args[i], {GRPC_COMPRESS_NONE, GRPC_COMPRESS_NONE,
                               GRPC_COMPRESS_ALGORITHMS_COUNT - 1}));
    } else if (0 ==
               strcmp(args->args[i].key,
                      GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET)) {
      channel->compression_options.enabled_algorithms_bitset =
          static_cast<uint32_t>(args->args[i].value.integer) |
          0x1; /* always support no compression */
    } else if (0 == strcmp(args->args[i].key, GRPC_ARG_CHANNELZ_CHANNEL_NODE)) {
      if (args->args[i].type == GRPC_ARG_POINTER) {
        GPR_ASSERT(args->args[i].value.pointer.p != nullptr);
        channel->channelz_node = static_cast<grpc_core::channelz::ChannelNode*>(
                                     args->args[i].value.pointer.p)
                                     ->Ref();
      } else {
        gpr_log(GPR_DEBUG,
                GRPC_ARG_CHANNELZ_CHANNEL_NODE " should be a pointer");
      }
    }
  }

  grpc_channel_args_destroy(args);
  return channel;
}

static grpc_core::UniquePtr<char> get_default_authority(
    const grpc_channel_args* input_args) {
  bool has_default_authority = false;
  char* ssl_override = nullptr;
  grpc_core::UniquePtr<char> default_authority;
  const size_t num_args = input_args != nullptr ? input_args->num_args : 0;
  for (size_t i = 0; i < num_args; ++i) {
    if (0 == strcmp(input_args->args[i].key, GRPC_ARG_DEFAULT_AUTHORITY)) {
      has_default_authority = true;
    } else if (0 == strcmp(input_args->args[i].key,
                           GRPC_SSL_TARGET_NAME_OVERRIDE_ARG)) {
      ssl_override = grpc_channel_arg_get_string(&input_args->args[i]);
    }
  }
  if (!has_default_authority && ssl_override != nullptr) {
    default_authority.reset(gpr_strdup(ssl_override));
  }
  return default_authority;
}

static grpc_channel_args* build_channel_args(
    const grpc_channel_args* input_args, char* default_authority) {
  grpc_arg new_args[1];
  size_t num_new_args = 0;
  if (default_authority != nullptr) {
    new_args[num_new_args++] = grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY), default_authority);
  }
  return grpc_channel_args_copy_and_add(input_args, new_args, num_new_args);
}

namespace {

void* channelz_node_copy(void* p) {
  grpc_core::channelz::ChannelNode* node =
      static_cast<grpc_core::channelz::ChannelNode*>(p);
  node->Ref().release();
  return p;
}
void channelz_node_destroy(void* p) {
  grpc_core::channelz::ChannelNode* node =
      static_cast<grpc_core::channelz::ChannelNode*>(p);
  node->Unref();
}
int channelz_node_cmp(void* p1, void* p2) { return GPR_ICMP(p1, p2); }
const grpc_arg_pointer_vtable channelz_node_arg_vtable = {
    channelz_node_copy, channelz_node_destroy, channelz_node_cmp};

void CreateChannelzNode(grpc_channel_stack_builder* builder) {
  const grpc_channel_args* args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  // Check whether channelz is enabled.
  const bool channelz_enabled = grpc_channel_arg_get_bool(
      grpc_channel_args_find(args, GRPC_ARG_ENABLE_CHANNELZ),
      GRPC_ENABLE_CHANNELZ_DEFAULT);
  if (!channelz_enabled) return;
  // Get parameters needed to create the channelz node.
  const size_t channel_tracer_max_memory = grpc_channel_arg_get_integer(
      grpc_channel_args_find(args,
                             GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE),
      {GRPC_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE_DEFAULT, 0, INT_MAX});
  const intptr_t channelz_parent_uuid =
      grpc_core::channelz::GetParentUuidFromArgs(*args);
  // Create the channelz node.
  const char* target = grpc_channel_stack_builder_get_target(builder);
  grpc_core::RefCountedPtr<grpc_core::channelz::ChannelNode> channelz_node =
      grpc_core::MakeRefCounted<grpc_core::channelz::ChannelNode>(
          target != nullptr ? target : "", channel_tracer_max_memory,
          channelz_parent_uuid);
  channelz_node->AddTraceEvent(
      grpc_core::channelz::ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("Channel created"));
  // Update parent channel node, if any.
  if (channelz_parent_uuid > 0) {
    grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> parent_node =
        grpc_core::channelz::ChannelzRegistry::Get(channelz_parent_uuid);
    if (parent_node != nullptr) {
      grpc_core::channelz::ChannelNode* parent =
          static_cast<grpc_core::channelz::ChannelNode*>(parent_node.get());
      parent->AddChildChannel(channelz_node->uuid());
    }
  }
  // Add channelz node to channel args.
  // We remove the arg for the parent uuid, since we no longer need it.
  grpc_arg new_arg = grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_CHANNELZ_CHANNEL_NODE), channelz_node.get(),
      &channelz_node_arg_vtable);
  const char* args_to_remove[] = {GRPC_ARG_CHANNELZ_PARENT_UUID};
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
      args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), &new_arg, 1);
  grpc_channel_stack_builder_set_channel_arguments(builder, new_args);
  grpc_channel_args_destroy(new_args);
}

}  // namespace

grpc_channel* grpc_channel_create(const char* target,
                                  const grpc_channel_args* input_args,
                                  grpc_channel_stack_type channel_stack_type,
                                  grpc_transport* optional_transport,
                                  grpc_resource_user* resource_user) {
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
  grpc_init();
  grpc_channel_stack_builder* builder = grpc_channel_stack_builder_create();
  const grpc_core::UniquePtr<char> default_authority =
      get_default_authority(input_args);
  grpc_channel_args* args =
      build_channel_args(input_args, default_authority.get());
  if (grpc_channel_stack_type_is_client(channel_stack_type)) {
    auto channel_args_mutator =
        grpc_channel_args_get_client_channel_creation_mutator();
    if (channel_args_mutator != nullptr) {
      args = channel_args_mutator(target, args, channel_stack_type);
    }
  }
  grpc_channel_stack_builder_set_channel_arguments(builder, args);
  grpc_channel_args_destroy(args);
  grpc_channel_stack_builder_set_target(builder, target);
  grpc_channel_stack_builder_set_transport(builder, optional_transport);
  grpc_channel_stack_builder_set_resource_user(builder, resource_user);
  if (!grpc_channel_init_create_stack(builder, channel_stack_type)) {
    grpc_channel_stack_builder_destroy(builder);
    if (resource_user != nullptr) {
      grpc_resource_user_free(resource_user, GRPC_RESOURCE_QUOTA_CHANNEL_SIZE);
    }
    grpc_shutdown();  // Since we won't call destroy_channel().
    return nullptr;
  }
  // We only need to do this for clients here. For servers, this will be
  // done in src/core/lib/surface/server.cc.
  if (grpc_channel_stack_type_is_client(channel_stack_type)) {
    CreateChannelzNode(builder);
  }
  grpc_channel* channel =
      grpc_channel_create_with_builder(builder, channel_stack_type);
  if (channel == nullptr) {
    grpc_shutdown();  // Since we won't call destroy_channel().
  }
  return channel;
}

size_t grpc_channel_get_call_size_estimate(grpc_channel* channel) {
#define ROUND_UP_SIZE 256
  /* We round up our current estimate to the NEXT value of ROUND_UP_SIZE.
     This ensures:
      1. a consistent size allocation when our estimate is drifting slowly
         (which is common) - which tends to help most allocators reuse memory
      2. a small amount of allowed growth over the estimate without hitting
         the arena size doubling case, reducing overall memory usage */
  return (static_cast<size_t>(
              gpr_atm_no_barrier_load(&channel->call_size_estimate)) +
          2 * ROUND_UP_SIZE) &
         ~static_cast<size_t>(ROUND_UP_SIZE - 1);
}

void grpc_channel_update_call_size_estimate(grpc_channel* channel,
                                            size_t size) {
  size_t cur = static_cast<size_t>(
      gpr_atm_no_barrier_load(&channel->call_size_estimate));
  if (cur < size) {
    /* size grew: update estimate */
    gpr_atm_no_barrier_cas(&channel->call_size_estimate,
                           static_cast<gpr_atm>(cur),
                           static_cast<gpr_atm>(size));
    /* if we lose: never mind, something else will likely update soon enough */
  } else if (cur == size) {
    /* no change: holding pattern */
  } else if (cur > 0) {
    /* size shrank: decrease estimate */
    gpr_atm_no_barrier_cas(
        &channel->call_size_estimate, static_cast<gpr_atm>(cur),
        static_cast<gpr_atm>(GPR_MIN(cur - 1, (255 * cur + size) / 256)));
    /* if we lose: never mind, something else will likely update soon enough */
  }
}

char* grpc_channel_get_target(grpc_channel* channel) {
  GRPC_API_TRACE("grpc_channel_get_target(channel=%p)", 1, (channel));
  return gpr_strdup(channel->target);
}

void grpc_channel_get_info(grpc_channel* channel,
                           const grpc_channel_info* channel_info) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_element* elem =
      grpc_channel_stack_element(CHANNEL_STACK_FROM_CHANNEL(channel), 0);
  elem->filter->get_channel_info(elem, channel_info);
}

void grpc_channel_reset_connect_backoff(grpc_channel* channel) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_channel_reset_connect_backoff(channel=%p)", 1,
                 (channel));
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  op->reset_connect_backoff = true;
  grpc_channel_element* elem =
      grpc_channel_stack_element(CHANNEL_STACK_FROM_CHANNEL(channel), 0);
  elem->filter->start_transport_op(elem, op);
}

static grpc_call* grpc_channel_create_call_internal(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* cq, grpc_pollset_set* pollset_set_alternative,
    grpc_mdelem path_mdelem, grpc_mdelem authority_mdelem,
    grpc_millis deadline) {
  grpc_mdelem send_metadata[2];
  size_t num_metadata = 0;

  GPR_ASSERT(channel->is_client);
  GPR_ASSERT(!(cq != nullptr && pollset_set_alternative != nullptr));

  send_metadata[num_metadata++] = path_mdelem;
  if (!GRPC_MDISNULL(authority_mdelem)) {
    send_metadata[num_metadata++] = authority_mdelem;
  }

  grpc_call_create_args args;
  args.channel = channel;
  args.server = nullptr;
  args.parent = parent_call;
  args.propagation_mask = propagation_mask;
  args.cq = cq;
  args.pollset_set_alternative = pollset_set_alternative;
  args.server_transport_data = nullptr;
  args.add_initial_metadata = send_metadata;
  args.add_initial_metadata_count = num_metadata;
  args.send_deadline = deadline;

  grpc_call* call;
  GRPC_LOG_IF_ERROR("call_create", grpc_call_create(&args, &call));
  return call;
}

grpc_call* grpc_channel_create_call(grpc_channel* channel,
                                    grpc_call* parent_call,
                                    uint32_t propagation_mask,
                                    grpc_completion_queue* cq,
                                    grpc_slice method, const grpc_slice* host,
                                    gpr_timespec deadline, void* reserved) {
  GPR_ASSERT(!reserved);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_call* call = grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, cq, nullptr,
      grpc_mdelem_create(GRPC_MDSTR_PATH, method, nullptr),
      host != nullptr ? grpc_mdelem_create(GRPC_MDSTR_AUTHORITY, *host, nullptr)
                      : GRPC_MDNULL,
      grpc_timespec_to_millis_round_up(deadline));

  return call;
}

grpc_call* grpc_channel_create_pollset_set_call(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_pollset_set* pollset_set, const grpc_slice& method,
    const grpc_slice* host, grpc_millis deadline, void* reserved) {
  GPR_ASSERT(!reserved);
  return grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, nullptr, pollset_set,
      grpc_mdelem_create(GRPC_MDSTR_PATH, method, nullptr),
      host != nullptr ? grpc_mdelem_create(GRPC_MDSTR_AUTHORITY, *host, nullptr)
                      : GRPC_MDNULL,
      deadline);
}

namespace grpc_core {

RegisteredCall::RegisteredCall(const char* method, const char* host) {
  path = grpc_mdelem_from_slices(GRPC_MDSTR_PATH,
                                 grpc_core::ExternallyManagedSlice(method));
  authority =
      host ? grpc_mdelem_from_slices(GRPC_MDSTR_AUTHORITY,
                                     grpc_core::ExternallyManagedSlice(host))
           : GRPC_MDNULL;
}

// TODO(vjpai): Delete copy-constructor when allowed by all supported compilers.
RegisteredCall::RegisteredCall(const RegisteredCall& other) {
  path = other.path;
  authority = other.authority;
  GRPC_MDELEM_REF(path);
  GRPC_MDELEM_REF(authority);
}

RegisteredCall::RegisteredCall(RegisteredCall&& other) noexcept {
  path = other.path;
  authority = other.authority;
  other.path = GRPC_MDNULL;
  other.authority = GRPC_MDNULL;
}

RegisteredCall::~RegisteredCall() {
  GRPC_MDELEM_UNREF(path);
  GRPC_MDELEM_UNREF(authority);
}

}  // namespace grpc_core

void* grpc_channel_register_call(grpc_channel* channel, const char* method,
                                 const char* host, void* reserved) {
  GRPC_API_TRACE(
      "grpc_channel_register_call(channel=%p, method=%s, host=%s, reserved=%p)",
      4, (channel, method, host, reserved));
  GPR_ASSERT(!reserved);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;

  grpc_core::MutexLock lock(&channel->registration_table->mu);
  channel->registration_table->method_registration_attempts++;
  auto key = std::make_pair(host, method);
  auto rc_posn = channel->registration_table->map.find(key);
  if (rc_posn != channel->registration_table->map.end()) {
    return &rc_posn->second;
  }
  auto insertion_result = channel->registration_table->map.insert(
      {key, grpc_core::RegisteredCall(method, host)});
  return &insertion_result.first->second;
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
  grpc_call* call = grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, completion_queue, nullptr,
      GRPC_MDELEM_REF(rc->path), GRPC_MDELEM_REF(rc->authority),
      grpc_timespec_to_millis_round_up(deadline));

  return call;
}

static void destroy_channel(void* arg, grpc_error* /*error*/) {
  grpc_channel* channel = static_cast<grpc_channel*>(arg);
  if (channel->channelz_node != nullptr) {
    if (channel->channelz_node->parent_uuid() > 0) {
      grpc_core::RefCountedPtr<grpc_core::channelz::BaseNode> parent_node =
          grpc_core::channelz::ChannelzRegistry::Get(
              channel->channelz_node->parent_uuid());
      if (parent_node != nullptr) {
        grpc_core::channelz::ChannelNode* parent =
            static_cast<grpc_core::channelz::ChannelNode*>(parent_node.get());
        parent->RemoveChildChannel(channel->channelz_node->uuid());
      }
    }
    channel->channelz_node->AddTraceEvent(
        grpc_core::channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string("Channel destroyed"));
    channel->channelz_node.reset();
  }
  grpc_channel_stack_destroy(CHANNEL_STACK_FROM_CHANNEL(channel));
  channel->registration_table.Destroy();
  if (channel->resource_user != nullptr) {
    grpc_resource_user_free(channel->resource_user,
                            GRPC_RESOURCE_QUOTA_CHANNEL_SIZE);
  }
  gpr_free(channel->target);
  gpr_free(channel);
  // See comment in grpc_channel_create() for why we do this.
  grpc_shutdown();
}

void grpc_channel_destroy_internal(grpc_channel* channel) {
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  grpc_channel_element* elem;
  GRPC_API_TRACE("grpc_channel_destroy(channel=%p)", 1, (channel));
  op->disconnect_with_error =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel Destroyed");
  elem = grpc_channel_stack_element(CHANNEL_STACK_FROM_CHANNEL(channel), 0);
  elem->filter->start_transport_op(elem, op);
  GRPC_CHANNEL_INTERNAL_UNREF(channel, "channel");
}

void grpc_channel_destroy(grpc_channel* channel) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_destroy_internal(channel);
}
