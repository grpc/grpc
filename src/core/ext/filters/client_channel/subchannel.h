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
#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/arena.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata.h"

// Channel arg containing a grpc_resolved_address to connect to.
#define GRPC_ARG_SUBCHANNEL_ADDRESS "grpc.subchannel_address"

#ifndef NDEBUG
#define GRPC_SUBCHANNEL_REF(p, r) (p)->Ref(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(p, r) \
  (p)->RefFromWeakRef(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_UNREF(p, r) (p)->Unref(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_WEAK_REF(p, r) (p)->WeakRef(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_WEAK_UNREF(p, r) (p)->WeakUnref(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_CALL_REF(p, r) (p)->Ref(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_CALL_UNREF(p, r) (p)->Unref(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_REF_EXTRA_ARGS \
  const char *file, int line, const char *reason
#define GRPC_SUBCHANNEL_REF_REASON reason
#define GRPC_SUBCHANNEL_REF_MUTATE_EXTRA_ARGS \
  , GRPC_SUBCHANNEL_REF_EXTRA_ARGS, const char* purpose
#define GRPC_SUBCHANNEL_REF_MUTATE_PURPOSE(x) , file, line, reason, x
#else
#define GRPC_SUBCHANNEL_REF(p, r) (p)->Ref()
#define GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(p, r) (p)->RefFromWeakRef()
#define GRPC_SUBCHANNEL_UNREF(p, r) (p)->Unref()
#define GRPC_SUBCHANNEL_WEAK_REF(p, r) (p)->WeakRef()
#define GRPC_SUBCHANNEL_WEAK_UNREF(p, r) (p)->WeakUnref()
#define GRPC_SUBCHANNEL_CALL_REF(p, r) (p)->Ref()
#define GRPC_SUBCHANNEL_CALL_UNREF(p, r) (p)->Unref()
#define GRPC_SUBCHANNEL_REF_EXTRA_ARGS
#define GRPC_SUBCHANNEL_REF_REASON ""
#define GRPC_SUBCHANNEL_REF_MUTATE_EXTRA_ARGS
#define GRPC_SUBCHANNEL_REF_MUTATE_PURPOSE(x)
#endif

namespace grpc_core {

class SubchannelCall;
class ConnectedSubchannelStateWatcher;

class ConnectedSubchannel : public RefCounted<ConnectedSubchannel> {
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

  ConnectedSubchannel(
      grpc_channel_stack* channel_stack, const grpc_channel_args* args,
      RefCountedPtr<channelz::SubchannelNode> channelz_subchannel,
      intptr_t socket_uuid);
  ~ConnectedSubchannel();

  void NotifyOnStateChange(grpc_pollset_set* interested_parties,
                           grpc_connectivity_state* state,
                           grpc_closure* closure);
  void Ping(grpc_closure* on_initiate, grpc_closure* on_ack);
  grpc_error* CreateCall(const CallArgs& args, SubchannelCall** call);

  grpc_channel_stack* channel_stack() const { return channel_stack_; }
  const grpc_channel_args* args() const { return args_; }
  channelz::SubchannelNode* channelz_subchannel() const {
    return channelz_subchannel_.get();
  }
  intptr_t socket_uuid() const { return socket_uuid_; }

  size_t GetInitialCallSizeEstimate(size_t parent_data_size) const;

 private:
  grpc_channel_stack* channel_stack_;
  grpc_channel_args* args_;
  // ref counted pointer to the channelz node in this connected subchannel's
  // owning subchannel.
  RefCountedPtr<channelz::SubchannelNode> channelz_subchannel_;
  // uuid of this subchannel's socket. 0 if this subchannel is not connected.
  const intptr_t socket_uuid_;
};

class SubchannelCall {
 public:
  SubchannelCall(grpc_core::ConnectedSubchannel* connection,
                 const grpc_core::ConnectedSubchannel::CallArgs& args)
      : connection_(connection), deadline_(args.deadline) {}

  // Continues processing a transport op.
  void ProcessOp(grpc_transport_stream_op_batch* op);

  // Returns a pointer to the parent data associated with \a SubchannelCall.
  // The data will be of the size specified in \a parent_data_size field of
  // the args passed to \a ConnectedSubchannel::CreateCall().
  void* GetParentData();

  // Sets *status based on md_batch and error.
  void GetCallStatus(grpc_metadata_batch* md_batch, grpc_error* error,
                     grpc_status_code* status);

  // Returns the call stack of the subchannel call.
  grpc_call_stack* GetCallStack();

  // Sets the 'then_schedule_closure' argument for call stack destruction.
  // Must be called once per call.
  void SetCleanupClosure(grpc_closure* closure);

  SubchannelCall* Ref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
  void Unref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);

  static void Destroy(void* call, grpc_error* error);

 private:
  // If channelz is enabled, intercepts recv_trailing so that we may check the
  // status and associate it to a subchannel.
  void MaybeInterceptRecvTrailingMetadata(
      grpc_transport_stream_op_batch* batch);

  static void RecvTrailingMetadataReady(void* arg, grpc_error* error);

  grpc_core::ConnectedSubchannel* connection_;
  grpc_closure* schedule_closure_after_destroy_ = nullptr;
  // State needed to support channelz interception of recv trailing metadata.
  grpc_closure recv_trailing_metadata_ready_;
  grpc_closure* original_recv_trailing_metadata_;
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
  grpc_millis deadline_;
};

// A subchannel that knows how to connect to exactly one target address. It
// provides a target for load balancing.
class Subchannel {
 public:
  struct ExternalStateWatcher {
    ExternalStateWatcher(Subchannel* subchannel, grpc_pollset_set* pollset_set,
                         grpc_closure* notify)
        : subchannel(subchannel), pollset_set(pollset_set), notify(notify) {
      GRPC_SUBCHANNEL_WEAK_REF(subchannel, "external_state_watcher+init");
      GRPC_CLOSURE_INIT(&on_state_changed, OnStateChanged, this,
                        grpc_schedule_on_exec_ctx);
    }

    static void OnStateChanged(void* arg, grpc_error* error) {
      ExternalStateWatcher* w = static_cast<ExternalStateWatcher*>(arg);
      grpc_closure* follow_up = w->notify;
      if (w->pollset_set != nullptr) {
        grpc_pollset_set_del_pollset_set(w->subchannel->pollset_set_,
                                         w->pollset_set);
      }
      gpr_mu_lock(&w->subchannel->mu_);
      w->next->prev = w->prev;
      w->prev->next = w->next;
      gpr_mu_unlock(&w->subchannel->mu_);
      GRPC_SUBCHANNEL_WEAK_UNREF(w->subchannel, "external_state_watcher+done");
      gpr_free(w);
      GRPC_CLOSURE_SCHED(follow_up, GRPC_ERROR_REF(error));
    }

    grpc_core::Subchannel* subchannel;
    grpc_pollset_set* pollset_set;
    grpc_closure* notify;
    grpc_closure on_state_changed;
    ExternalStateWatcher* next;
    ExternalStateWatcher* prev;
  };

  static Subchannel* Create(grpc_connector* connector,
                            const grpc_channel_args* args);

  Subchannel* Ref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
  Subchannel* RefFromWeakRef(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
  void Unref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
  Subchannel* WeakRef(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
  void WeakUnref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);

  channelz::SubchannelNode* GetChannelzNode();

  intptr_t GetChildSocketUuid();

  const char* GetTarget();

  // Gets the connected subchannel - or nullptr if not connected (which may
  // happen before it initially connects or during transient failures).
  RefCountedPtr<ConnectedSubchannel> connected_subchannel();

  // Polls the current connectivity state of the subchannel.
  grpc_connectivity_state CheckConnectivity(grpc_error** error,
                                            bool inhibit_health_checking);

  // When the connectivity state of the subchannel changes from \a *state,
  // invokes \a notify and updates \a *state with the new state.
  void NotifyOnStateChange(grpc_pollset_set* interested_parties,
                           grpc_connectivity_state* state, grpc_closure* notify,
                           bool inhibit_health_checks);

  // Resets the connection backoff of the subchannel.
  // TODO(roth): Move connection backoff out of subchannels and up into LB
  // policy code (probably by adding a SubchannelGroup between
  // SubchannelList and SubchannelData), at which point this method can
  // go away.
  void ResetBackoff();

  // Returns a string indicating the subchannel's connectivity state change to
  // \a state.
  static const char* ConnectivityStateChangeString(
      grpc_connectivity_state state);

  // Returns a new channel arg encoding the subchannel address as a URI
  // string. Caller is responsible for freeing the string.
  static grpc_arg CreateSubchannelAddressArg(const grpc_resolved_address* addr);

  // Returns the URI string from the subchannel address arg in \a args.
  static const char* GetUriFromSubchannelAddressArg(
      const grpc_channel_args* args);

  // Sets \a addr from the subchannel address arg in \a args.
  static void GetAddressFromSubchannelAddressArg(const grpc_channel_args* args,
                                                 grpc_resolved_address* addr);

 private:
  friend class ConnectedSubchannelStateWatcher;

  void SetSubchannelConnectivityStateLocked(grpc_connectivity_state state,
                                            grpc_error* error,
                                            const char* reason);

  void MaybeStartConnectingLocked();
  void ContinueConnectLocked();
  bool PublishTransportLocked();
  void Disconnect();

  static void OnConnected(void* arg, grpc_error* error);
  static void OnRetryAlarm(void* arg, grpc_error* error);
  static void Destroy(void* arg, grpc_error* error);

  static void ParseArgsForBackoffValues(
      const grpc_channel_args* args,
      grpc_core::BackOff::Options* backoff_options,
      grpc_millis* min_connect_timeout_ms);

  gpr_atm RefMutate(gpr_atm delta,
                    int barrier GRPC_SUBCHANNEL_REF_MUTATE_EXTRA_ARGS);

  // The subchannel pool this subchannel is in.
  RefCountedPtr<SubchannelPoolInterface> subchannel_pool_;
  // Subchannel key that identifies this subchannel in the subchannel pool.
  SubchannelKey* key_;
  // Channel arguments.
  grpc_channel_args* args_;
  // pollset_set tracking who's interested in a connection being setup.
  grpc_pollset_set* pollset_set_;
  // Protects the other members.
  gpr_mu mu_;
  // Refcount
  //    - lower INTERNAL_REF_BITS bits are for internal references:
  //      these do not keep the subchannel open.
  //    - upper remaining bits are for public references: these do
  //      keep the subchannel open
  gpr_atm ref_pair_;

  // Connection states.
  grpc_connector* connector_;
  // Set during connection.
  grpc_connect_out_args connecting_result_;
  // Callback for connection finishing.
  grpc_closure on_connected_;
  // Connection state.
  bool disconnected_;
  bool connecting_;

  // Active connection, or null.
  RefCountedPtr<ConnectedSubchannel> connected_subchannel_;
  OrphanablePtr<ConnectedSubchannelStateWatcher> connected_subchannel_watcher_;

  // Connectivity state tracking.
  grpc_connectivity_state_tracker state_tracker_;
  grpc_connectivity_state_tracker state_and_health_tracker_;
  UniquePtr<char> health_check_service_name_;
  ExternalStateWatcher root_external_state_watcher_;

  // Backoff state.
  ManualConstructor<BackOff> backoff_;
  grpc_millis next_attempt_deadline_;
  grpc_millis min_connect_timeout_ms_;
  bool backoff_begun_;

  // Retry alarm.
  grpc_timer retry_alarm_;
  grpc_closure on_retry_alarm_;
  bool have_retry_alarm_;
  // reset_backoff() was called while alarm was pending.
  bool retry_immediately_;

  // Channelz tracking.
  RefCountedPtr<channelz::SubchannelNode> channelz_subchannel_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_H */
