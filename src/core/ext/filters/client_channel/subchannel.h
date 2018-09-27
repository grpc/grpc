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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/arena.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata.h"

// Channel arg containing a grpc_resolved_address to connect to.
#define GRPC_ARG_SUBCHANNEL_ADDRESS "grpc.subchannel_address"

/** A (sub-)channel that knows how to connect to exactly one target
    address. Provides a target for load balancing. */
typedef struct grpc_subchannel grpc_subchannel;
typedef struct grpc_subchannel_call grpc_subchannel_call;
typedef struct grpc_subchannel_args grpc_subchannel_args;
typedef struct grpc_subchannel_key grpc_subchannel_key;

#ifndef NDEBUG
#define GRPC_SUBCHANNEL_REF(p, r) \
  grpc_subchannel_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(p, r) \
  grpc_subchannel_ref_from_weak_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_UNREF(p, r) \
  grpc_subchannel_unref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_WEAK_REF(p, r) \
  grpc_subchannel_weak_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_WEAK_UNREF(p, r) \
  grpc_subchannel_weak_unref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_CALL_REF(p, r) \
  grpc_subchannel_call_ref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_CALL_UNREF(p, r) \
  grpc_subchannel_call_unref((p), __FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_REF_EXTRA_ARGS \
  , const char *file, int line, const char *reason
#else
#define GRPC_SUBCHANNEL_REF(p, r) grpc_subchannel_ref((p))
#define GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(p, r) \
  grpc_subchannel_ref_from_weak_ref((p))
#define GRPC_SUBCHANNEL_UNREF(p, r) grpc_subchannel_unref((p))
#define GRPC_SUBCHANNEL_WEAK_REF(p, r) grpc_subchannel_weak_ref((p))
#define GRPC_SUBCHANNEL_WEAK_UNREF(p, r) grpc_subchannel_weak_unref((p))
#define GRPC_SUBCHANNEL_CALL_REF(p, r) grpc_subchannel_call_ref((p))
#define GRPC_SUBCHANNEL_CALL_UNREF(p, r) grpc_subchannel_call_unref((p))
#define GRPC_SUBCHANNEL_REF_EXTRA_ARGS
#endif

namespace grpc_core {

class ConnectedSubchannel : public RefCountedWithTracing<ConnectedSubchannel> {
 public:
  struct CallArgs {
    grpc_polling_entity* pollent;
    grpc_slice path;
    gpr_timespec start_time;
    grpc_millis deadline;
    gpr_arena* arena;
    grpc_call_context_element* context;
    grpc_call_combiner* call_combiner;
    size_t parent_data_size;
  };

  explicit ConnectedSubchannel(grpc_channel_stack* channel_stack,
                               channelz::SubchannelNode* channelz_subchannel,
                               intptr_t socket_uuid);
  ~ConnectedSubchannel();

  grpc_channel_stack* channel_stack() { return channel_stack_; }
  void NotifyOnStateChange(grpc_pollset_set* interested_parties,
                           grpc_connectivity_state* state,
                           grpc_closure* closure);
  void Ping(grpc_closure* on_initiate, grpc_closure* on_ack);
  grpc_error* CreateCall(const CallArgs& args, grpc_subchannel_call** call);
  channelz::SubchannelNode* channelz_subchannel() {
    return channelz_subchannel_;
  }
  intptr_t socket_uuid() { return socket_uuid_; }

 private:
  grpc_channel_stack* channel_stack_;
  // backpointer to the channelz node in this connected subchannel's
  // owning subchannel.
  channelz::SubchannelNode* channelz_subchannel_;
  // uuid of this subchannel's socket. 0 if this subchannel is not connected.
  const intptr_t socket_uuid_;
};

}  // namespace grpc_core

grpc_subchannel* grpc_subchannel_ref(
    grpc_subchannel* channel GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
grpc_subchannel* grpc_subchannel_ref_from_weak_ref(
    grpc_subchannel* channel GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
void grpc_subchannel_unref(
    grpc_subchannel* channel GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
grpc_subchannel* grpc_subchannel_weak_ref(
    grpc_subchannel* channel GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
void grpc_subchannel_weak_unref(
    grpc_subchannel* channel GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
grpc_subchannel_call* grpc_subchannel_call_ref(
    grpc_subchannel_call* call GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
void grpc_subchannel_call_unref(
    grpc_subchannel_call* call GRPC_SUBCHANNEL_REF_EXTRA_ARGS);

grpc_core::channelz::SubchannelNode* grpc_subchannel_get_channelz_node(
    grpc_subchannel* subchannel);

intptr_t grpc_subchannel_get_child_socket_uuid(grpc_subchannel* subchannel);

/** Returns a pointer to the parent data associated with \a subchannel_call.
    The data will be of the size specified in \a parent_data_size
    field of the args passed to \a grpc_connected_subchannel_create_call(). */
void* grpc_connected_subchannel_call_get_parent_data(
    grpc_subchannel_call* subchannel_call);

/** poll the current connectivity state of a channel */
grpc_connectivity_state grpc_subchannel_check_connectivity(
    grpc_subchannel* channel, grpc_error** error);

/** Calls notify when the connectivity state of a channel becomes different
    from *state.  Updates *state with the new state of the channel. */
void grpc_subchannel_notify_on_state_change(
    grpc_subchannel* channel, grpc_pollset_set* interested_parties,
    grpc_connectivity_state* state, grpc_closure* notify);

/** retrieve the grpc_core::ConnectedSubchannel - or nullptr if not connected
 * (which may happen before it initially connects or during transient failures)
 * */
grpc_core::RefCountedPtr<grpc_core::ConnectedSubchannel>
grpc_subchannel_get_connected_subchannel(grpc_subchannel* c);

/** return the subchannel index key for \a subchannel */
const grpc_subchannel_key* grpc_subchannel_get_key(
    const grpc_subchannel* subchannel);

// Resets the connection backoff of the subchannel.
// TODO(roth): Move connection backoff out of subchannels and up into LB
// policy code (probably by adding a SubchannelGroup between
// SubchannelList and SubchannelData), at which point this method can
// go away.
void grpc_subchannel_reset_backoff(grpc_subchannel* subchannel);

/** continue processing a transport op */
void grpc_subchannel_call_process_op(grpc_subchannel_call* subchannel_call,
                                     grpc_transport_stream_op_batch* op);

/** Must be called once per call. Sets the 'then_schedule_closure' argument for
    call stack destruction. */
void grpc_subchannel_call_set_cleanup_closure(
    grpc_subchannel_call* subchannel_call, grpc_closure* closure);

grpc_call_stack* grpc_subchannel_call_get_call_stack(
    grpc_subchannel_call* subchannel_call);

struct grpc_subchannel_args {
  /* When updating this struct, also update subchannel_index.c */

  /** Channel filters for this channel - wrapped factories will likely
      want to mutate this */
  const grpc_channel_filter** filters;
  /** The number of filters in the above array */
  size_t filter_count;
  /** Channel arguments to be supplied to the newly created channel */
  const grpc_channel_args* args;
};

/** create a subchannel given a connector */
grpc_subchannel* grpc_subchannel_create(grpc_connector* connector,
                                        const grpc_subchannel_args* args);

/// Sets \a addr from \a args.
void grpc_get_subchannel_address_arg(const grpc_channel_args* args,
                                     grpc_resolved_address* addr);

const char* grpc_subchannel_get_target(grpc_subchannel* subchannel);

/// Returns the URI string for the address to connect to.
const char* grpc_get_subchannel_address_uri_arg(const grpc_channel_args* args);

/// Returns a new channel arg encoding the subchannel address as a string.
/// Caller is responsible for freeing the string.
grpc_arg grpc_create_subchannel_address_arg(const grpc_resolved_address* addr);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_H */
