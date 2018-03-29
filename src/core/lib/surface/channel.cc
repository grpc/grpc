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
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/iomgr.h"
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

typedef struct registered_call {
  grpc_mdelem path;
  grpc_mdelem authority;
  struct registered_call* next;
} registered_call;

struct grpc_channel {
  int is_client;
  grpc_compression_options compression_options;

  gpr_atm call_size_estimate;

  gpr_mu registered_call_mu;
  registered_call* registered_calls;

  grpc_core::RefCountedPtr<grpc_core::ChannelTrace> tracer;

  char* target;
};

#define CHANNEL_STACK_FROM_CHANNEL(c) ((grpc_channel_stack*)((c) + 1))
#define CHANNEL_FROM_CHANNEL_STACK(channel_stack) \
  (((grpc_channel*)(channel_stack)) - 1)
#define CHANNEL_FROM_TOP_ELEM(top_elem) \
  CHANNEL_FROM_CHANNEL_STACK(grpc_channel_stack_from_top_element(top_elem))

static void destroy_channel(void* arg, grpc_error* error);

grpc_channel* grpc_channel_create_with_builder(
    grpc_channel_stack_builder* builder,
    grpc_channel_stack_type channel_stack_type) {
  char* target = gpr_strdup(grpc_channel_stack_builder_get_target(builder));
  grpc_channel_args* args = grpc_channel_args_copy(
      grpc_channel_stack_builder_get_channel_arguments(builder));
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

  memset(channel, 0, sizeof(*channel));
  channel->target = target;
  channel->is_client = grpc_channel_stack_type_is_client(channel_stack_type);
  size_t channel_tracer_max_nodes = 0;  // default to off
  gpr_mu_init(&channel->registered_call_mu);
  channel->registered_calls = nullptr;

  gpr_atm_no_barrier_store(
      &channel->call_size_estimate,
      (gpr_atm)CHANNEL_STACK_FROM_CHANNEL(channel)->call_stack_size);

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
    } else if (0 == strcmp(args->args[i].key,
                           GRPC_ARG_MAX_CHANNEL_TRACE_EVENTS_PER_NODE)) {
      GPR_ASSERT(channel_tracer_max_nodes == 0);
      // max_nodes defaults to 0 (which is off), clamped between 0 and INT_MAX
      const grpc_integer_options options = {0, 0, INT_MAX};
      channel_tracer_max_nodes =
          (size_t)grpc_channel_arg_get_integer(&args->args[i], options);
    }
  }

  grpc_channel_args_destroy(args);
  channel->tracer = grpc_core::MakeRefCounted<grpc_core::ChannelTrace>(
      channel_tracer_max_nodes);
  channel->tracer->AddTraceEvent(
      grpc_core::ChannelTrace::Severity::Info,
      grpc_slice_from_static_string("Channel created"));
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

char* grpc_channel_get_trace(grpc_channel* channel) {
  return channel->tracer->RenderTrace();
}

intptr_t grpc_channel_get_uuid(grpc_channel* channel) {
  return channel->tracer->GetUuid();
}

grpc_channel* grpc_channel_create(const char* target,
                                  const grpc_channel_args* input_args,
                                  grpc_channel_stack_type channel_stack_type,
                                  grpc_transport* optional_transport) {
  grpc_channel_stack_builder* builder = grpc_channel_stack_builder_create();
  const grpc_core::UniquePtr<char> default_authority =
      get_default_authority(input_args);
  grpc_channel_args* args =
      build_channel_args(input_args, default_authority.get());
  grpc_channel_stack_builder_set_channel_arguments(builder, args);
  grpc_channel_args_destroy(args);
  grpc_channel_stack_builder_set_target(builder, target);
  grpc_channel_stack_builder_set_transport(builder, optional_transport);
  if (!grpc_channel_init_create_stack(builder, channel_stack_type)) {
    grpc_channel_stack_builder_destroy(builder);
    return nullptr;
  }
  return grpc_channel_create_with_builder(builder, channel_stack_type);
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
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_element* elem =
      grpc_channel_stack_element(CHANNEL_STACK_FROM_CHANNEL(channel), 0);
  elem->filter->get_channel_info(elem, channel_info);
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
  memset(&args, 0, sizeof(args));
  args.channel = channel;
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
  grpc_core::ExecCtx exec_ctx;
  grpc_call* call = grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, cq, nullptr,
      grpc_mdelem_from_slices(GRPC_MDSTR_PATH, grpc_slice_ref_internal(method)),
      host != nullptr ? grpc_mdelem_from_slices(GRPC_MDSTR_AUTHORITY,
                                                grpc_slice_ref_internal(*host))
                      : GRPC_MDNULL,
      grpc_timespec_to_millis_round_up(deadline));

  return call;
}

grpc_call* grpc_channel_create_pollset_set_call(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_pollset_set* pollset_set, grpc_slice method, const grpc_slice* host,
    grpc_millis deadline, void* reserved) {
  GPR_ASSERT(!reserved);
  return grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, nullptr, pollset_set,
      grpc_mdelem_from_slices(GRPC_MDSTR_PATH, grpc_slice_ref_internal(method)),
      host != nullptr ? grpc_mdelem_from_slices(GRPC_MDSTR_AUTHORITY,
                                                grpc_slice_ref_internal(*host))
                      : GRPC_MDNULL,
      deadline);
}

void* grpc_channel_register_call(grpc_channel* channel, const char* method,
                                 const char* host, void* reserved) {
  registered_call* rc =
      static_cast<registered_call*>(gpr_malloc(sizeof(registered_call)));
  GRPC_API_TRACE(
      "grpc_channel_register_call(channel=%p, method=%s, host=%s, reserved=%p)",
      4, (channel, method, host, reserved));
  GPR_ASSERT(!reserved);
  grpc_core::ExecCtx exec_ctx;

  rc->path = grpc_mdelem_from_slices(
      GRPC_MDSTR_PATH,
      grpc_slice_intern(grpc_slice_from_static_string(method)));
  rc->authority =
      host ? grpc_mdelem_from_slices(
                 GRPC_MDSTR_AUTHORITY,
                 grpc_slice_intern(grpc_slice_from_static_string(host)))
           : GRPC_MDNULL;
  gpr_mu_lock(&channel->registered_call_mu);
  rc->next = channel->registered_calls;
  channel->registered_calls = rc;
  gpr_mu_unlock(&channel->registered_call_mu);

  return rc;
}

grpc_call* grpc_channel_create_registered_call(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* completion_queue, void* registered_call_handle,
    gpr_timespec deadline, void* reserved) {
  registered_call* rc = static_cast<registered_call*>(registered_call_handle);
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
  grpc_core::ExecCtx exec_ctx;
  grpc_call* call = grpc_channel_create_call_internal(
      channel, parent_call, propagation_mask, completion_queue, nullptr,
      GRPC_MDELEM_REF(rc->path), GRPC_MDELEM_REF(rc->authority),
      grpc_timespec_to_millis_round_up(deadline));

  return call;
}

#ifndef NDEBUG
#define REF_REASON reason
#define REF_ARG , const char* reason
#else
#define REF_REASON ""
#define REF_ARG
#endif
void grpc_channel_internal_ref(grpc_channel* c REF_ARG) {
  GRPC_CHANNEL_STACK_REF(CHANNEL_STACK_FROM_CHANNEL(c), REF_REASON);
}

void grpc_channel_internal_unref(grpc_channel* c REF_ARG) {
  GRPC_CHANNEL_STACK_UNREF(CHANNEL_STACK_FROM_CHANNEL(c), REF_REASON);
}

static void destroy_channel(void* arg, grpc_error* error) {
  grpc_channel* channel = static_cast<grpc_channel*>(arg);
  grpc_channel_stack_destroy(CHANNEL_STACK_FROM_CHANNEL(channel));
  while (channel->registered_calls) {
    registered_call* rc = channel->registered_calls;
    channel->registered_calls = rc->next;
    GRPC_MDELEM_UNREF(rc->path);
    GRPC_MDELEM_UNREF(rc->authority);
    gpr_free(rc);
  }
  channel->tracer.reset();
  gpr_mu_destroy(&channel->registered_call_mu);
  gpr_free(channel->target);
  gpr_free(channel);
}

void grpc_channel_destroy(grpc_channel* channel) {
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  grpc_channel_element* elem;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_channel_destroy(channel=%p)", 1, (channel));
  op->disconnect_with_error =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel Destroyed");
  elem = grpc_channel_stack_element(CHANNEL_STACK_FROM_CHANNEL(channel), 0);
  elem->filter->start_transport_op(elem, op);

  GRPC_CHANNEL_INTERNAL_UNREF(channel, "channel");
}

grpc_channel_stack* grpc_channel_get_channel_stack(grpc_channel* channel) {
  return CHANNEL_STACK_FROM_CHANNEL(channel);
}

grpc_compression_options grpc_channel_compression_options(
    const grpc_channel* channel) {
  return channel->compression_options;
}

grpc_mdelem grpc_channel_get_reffed_status_elem(grpc_channel* channel, int i) {
  char tmp[GPR_LTOA_MIN_BUFSIZE];
  switch (i) {
    case 0:
      return GRPC_MDELEM_GRPC_STATUS_0;
    case 1:
      return GRPC_MDELEM_GRPC_STATUS_1;
    case 2:
      return GRPC_MDELEM_GRPC_STATUS_2;
  }
  gpr_ltoa(i, tmp);
  return grpc_mdelem_from_slices(GRPC_MDSTR_GRPC_STATUS,
                                 grpc_slice_from_copied_string(tmp));
}
