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

#include "src/core/ext/filters/client_channel/client_channel.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/global_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/local_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/resolving_lb_policy.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_metadata.h"

using grpc_core::internal::ClientChannelMethodParams;
using grpc_core::internal::ClientChannelMethodParamsTable;
using grpc_core::internal::ProcessedResolverResult;
using grpc_core::internal::ServerRetryThrottleData;

using grpc_core::LoadBalancingPolicy;

/* Client channel implementation */

// By default, we buffer 256 KiB per RPC for retries.
// TODO(roth): Do we have any data to suggest a better value?
#define DEFAULT_PER_RPC_RETRY_BUFFER_SIZE (256 << 10)

// This value was picked arbitrarily.  It can be changed if there is
// any even moderately compelling reason to do so.
#define RETRY_BACKOFF_JITTER 0.2

grpc_core::TraceFlag grpc_client_channel_call_trace(false,
                                                    "client_channel_call");
grpc_core::TraceFlag grpc_client_channel_routing_trace(
    false, "client_channel_routing");

/*************************************************************************
 * CHANNEL-WIDE FUNCTIONS
 */

struct external_connectivity_watcher;

struct QueuedPick {
  LoadBalancingPolicy::PickArgs pick;
  grpc_call_element* elem;
  QueuedPick* next = nullptr;
};

typedef struct client_channel_channel_data {
  bool deadline_checking_enabled;
  bool enable_retries;
  size_t per_rpc_retry_buffer_size;

  /** combiner protecting all variables below in this data structure */
  grpc_combiner* combiner;
  /** owning stack */
  grpc_channel_stack* owning_stack;
  /** interested parties (owned) */
  grpc_pollset_set* interested_parties;
  // Client channel factory.
  grpc_core::ClientChannelFactory* client_channel_factory;
  // Subchannel pool.
  grpc_core::RefCountedPtr<grpc_core::SubchannelPoolInterface> subchannel_pool;

  grpc_core::channelz::ClientChannelNode* channelz_node;

  // Resolving LB policy.
  grpc_core::OrphanablePtr<LoadBalancingPolicy> resolving_lb_policy;
  // Subchannel picker from LB policy.
  grpc_core::UniquePtr<LoadBalancingPolicy::SubchannelPicker> picker;
  // Linked list of queued picks.
  QueuedPick* queued_picks;

  bool have_service_config;
  /** retry throttle data from service config */
  grpc_core::RefCountedPtr<ServerRetryThrottleData> retry_throttle_data;
  /** per-method service config data */
  grpc_core::RefCountedPtr<ClientChannelMethodParamsTable> method_params_table;

  /* the following properties are guarded by a mutex since APIs require them
     to be instantaneously available */
  gpr_mu info_mu;
  grpc_core::UniquePtr<char> info_lb_policy_name;
  grpc_core::UniquePtr<char> info_service_config_json;

  grpc_connectivity_state_tracker state_tracker;
  grpc_error* disconnect_error;

  /* external_connectivity_watcher_list head is guarded by its own mutex, since
   * counts need to be grabbed immediately without polling on a cq */
  gpr_mu external_connectivity_watcher_list_mu;
  struct external_connectivity_watcher* external_connectivity_watcher_list_head;
} channel_data;

// Forward declarations.
static void start_pick_locked(void* arg, grpc_error* ignored);
static void maybe_apply_service_config_to_call_locked(grpc_call_element* elem);

static const char* get_channel_connectivity_state_change_string(
    grpc_connectivity_state state) {
  switch (state) {
    case GRPC_CHANNEL_IDLE:
      return "Channel state change to IDLE";
    case GRPC_CHANNEL_CONNECTING:
      return "Channel state change to CONNECTING";
    case GRPC_CHANNEL_READY:
      return "Channel state change to READY";
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      return "Channel state change to TRANSIENT_FAILURE";
    case GRPC_CHANNEL_SHUTDOWN:
      return "Channel state change to SHUTDOWN";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

static void set_connectivity_state_and_picker_locked(
    channel_data* chand, grpc_connectivity_state state, grpc_error* state_error,
    const char* reason,
    grpc_core::UniquePtr<LoadBalancingPolicy::SubchannelPicker> picker) {
  // Update connectivity state.
  grpc_connectivity_state_set(&chand->state_tracker, state, state_error,
                              reason);
  if (chand->channelz_node != nullptr) {
    chand->channelz_node->AddTraceEvent(
        grpc_core::channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string(
            get_channel_connectivity_state_change_string(state)));
  }
  // Update picker.
  chand->picker = std::move(picker);
  // Re-process queued picks.
  for (QueuedPick* pick = chand->queued_picks; pick != nullptr;
       pick = pick->next) {
    start_pick_locked(pick->elem, GRPC_ERROR_NONE);
  }
}

namespace grpc_core {
namespace {

class ClientChannelControlHelper
    : public LoadBalancingPolicy::ChannelControlHelper {
 public:
  explicit ClientChannelControlHelper(channel_data* chand) : chand_(chand) {
    GRPC_CHANNEL_STACK_REF(chand_->owning_stack, "ClientChannelControlHelper");
  }

  ~ClientChannelControlHelper() override {
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack,
                             "ClientChannelControlHelper");
  }

  Subchannel* CreateSubchannel(const grpc_channel_args& args) override {
    grpc_arg arg = SubchannelPoolInterface::CreateChannelArg(
        chand_->subchannel_pool.get());
    grpc_channel_args* new_args =
        grpc_channel_args_copy_and_add(&args, &arg, 1);
    Subchannel* subchannel =
        chand_->client_channel_factory->CreateSubchannel(new_args);
    grpc_channel_args_destroy(new_args);
    return subchannel;
  }

  grpc_channel* CreateChannel(const char* target,
                              const grpc_channel_args& args) override {
    return chand_->client_channel_factory->CreateChannel(target, &args);
  }

  void UpdateState(
      grpc_connectivity_state state, grpc_error* state_error,
      UniquePtr<LoadBalancingPolicy::SubchannelPicker> picker) override {
    if (grpc_client_channel_routing_trace.enabled()) {
      const char* extra = chand_->disconnect_error == GRPC_ERROR_NONE
                              ? ""
                              : " (ignoring -- channel shutting down)";
      gpr_log(GPR_INFO, "chand=%p: update: state=%s error=%s picker=%p%s",
              chand_, grpc_connectivity_state_name(state),
              grpc_error_string(state_error), picker.get(), extra);
    }
    // Do update only if not shutting down.
    if (chand_->disconnect_error == GRPC_ERROR_NONE) {
      set_connectivity_state_and_picker_locked(chand_, state, state_error,
                                               "helper", std::move(picker));
    } else {
      GRPC_ERROR_UNREF(state_error);
    }
  }

  // No-op -- we should never get this from ResolvingLoadBalancingPolicy.
  void RequestReresolution() override {}

 private:
  channel_data* chand_;
};

}  // namespace
}  // namespace grpc_core

// Synchronous callback from chand->resolving_lb_policy to process a resolver
// result update.
static bool process_resolver_result_locked(
    void* arg, grpc_core::Resolver::Result* result, const char** lb_policy_name,
    grpc_core::RefCountedPtr<LoadBalancingPolicy::Config>* lb_policy_config) {
  channel_data* chand = static_cast<channel_data*>(arg);
  chand->have_service_config = true;
  ProcessedResolverResult resolver_result(result, chand->enable_retries);
  grpc_core::UniquePtr<char> service_config_json =
      resolver_result.service_config_json();
  if (grpc_client_channel_routing_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p: resolver returned service config: \"%s\"",
            chand, service_config_json.get());
  }
  // Update channel state.
  chand->retry_throttle_data = resolver_result.retry_throttle_data();
  chand->method_params_table = resolver_result.method_params_table();
  // Swap out the data used by cc_get_channel_info().
  gpr_mu_lock(&chand->info_mu);
  chand->info_lb_policy_name = resolver_result.lb_policy_name();
  const bool service_config_changed =
      ((service_config_json == nullptr) !=
       (chand->info_service_config_json == nullptr)) ||
      (service_config_json != nullptr &&
       strcmp(service_config_json.get(),
              chand->info_service_config_json.get()) != 0);
  chand->info_service_config_json = std::move(service_config_json);
  gpr_mu_unlock(&chand->info_mu);
  // Return results.
  *lb_policy_name = chand->info_lb_policy_name.get();
  *lb_policy_config = resolver_result.lb_policy_config();
  // Apply service config to queued picks.
  for (QueuedPick* pick = chand->queued_picks; pick != nullptr;
       pick = pick->next) {
    maybe_apply_service_config_to_call_locked(pick->elem);
  }
  return service_config_changed;
}

static grpc_error* do_ping_locked(channel_data* chand, grpc_transport_op* op) {
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_connectivity_state state =
      grpc_connectivity_state_get(&chand->state_tracker, &error);
  if (state != GRPC_CHANNEL_READY) {
    grpc_error* new_error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "channel not connected", &error, 1);
    GRPC_ERROR_UNREF(error);
    return new_error;
  }
  LoadBalancingPolicy::PickArgs pick;
  chand->picker->Pick(&pick, &error);
  if (pick.connected_subchannel != nullptr) {
    pick.connected_subchannel->Ping(op->send_ping.on_initiate,
                                    op->send_ping.on_ack);
  } else {
    if (error == GRPC_ERROR_NONE) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "LB policy dropped call on ping");
    }
  }
  return error;
}

static void start_transport_op_locked(void* arg, grpc_error* error_ignored) {
  grpc_transport_op* op = static_cast<grpc_transport_op*>(arg);
  grpc_channel_element* elem =
      static_cast<grpc_channel_element*>(op->handler_private.extra_arg);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);

  if (op->on_connectivity_state_change != nullptr) {
    grpc_connectivity_state_notify_on_state_change(
        &chand->state_tracker, op->connectivity_state,
        op->on_connectivity_state_change);
    op->on_connectivity_state_change = nullptr;
    op->connectivity_state = nullptr;
  }

  if (op->send_ping.on_initiate != nullptr || op->send_ping.on_ack != nullptr) {
    grpc_error* error = do_ping_locked(chand, op);
    if (error != GRPC_ERROR_NONE) {
      GRPC_CLOSURE_SCHED(op->send_ping.on_initiate, GRPC_ERROR_REF(error));
      GRPC_CLOSURE_SCHED(op->send_ping.on_ack, error);
    }
    op->bind_pollset = nullptr;
    op->send_ping.on_initiate = nullptr;
    op->send_ping.on_ack = nullptr;
  }

  if (op->reset_connect_backoff) {
    chand->resolving_lb_policy->ResetBackoffLocked();
  }

  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    chand->disconnect_error = op->disconnect_with_error;
    grpc_pollset_set_del_pollset_set(
        chand->resolving_lb_policy->interested_parties(),
        chand->interested_parties);
    chand->resolving_lb_policy.reset();
    set_connectivity_state_and_picker_locked(
        chand, GRPC_CHANNEL_SHUTDOWN, GRPC_ERROR_REF(op->disconnect_with_error),
        "shutdown from API",
        grpc_core::UniquePtr<LoadBalancingPolicy::SubchannelPicker>(
            grpc_core::New<LoadBalancingPolicy::TransientFailurePicker>(
                GRPC_ERROR_REF(op->disconnect_with_error))));
  }

  GRPC_CHANNEL_STACK_UNREF(chand->owning_stack, "start_transport_op");
  GRPC_CLOSURE_SCHED(op->on_consumed, GRPC_ERROR_NONE);
}

static void cc_start_transport_op(grpc_channel_element* elem,
                                  grpc_transport_op* op) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);

  GPR_ASSERT(op->set_accept_stream == false);
  if (op->bind_pollset != nullptr) {
    grpc_pollset_set_add_pollset(chand->interested_parties, op->bind_pollset);
  }

  op->handler_private.extra_arg = elem;
  GRPC_CHANNEL_STACK_REF(chand->owning_stack, "start_transport_op");
  GRPC_CLOSURE_SCHED(
      GRPC_CLOSURE_INIT(&op->handler_private.closure, start_transport_op_locked,
                        op, grpc_combiner_scheduler(chand->combiner)),
      GRPC_ERROR_NONE);
}

static void cc_get_channel_info(grpc_channel_element* elem,
                                const grpc_channel_info* info) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  gpr_mu_lock(&chand->info_mu);
  if (info->lb_policy_name != nullptr) {
    *info->lb_policy_name = gpr_strdup(chand->info_lb_policy_name.get());
  }
  if (info->service_config_json != nullptr) {
    *info->service_config_json =
        gpr_strdup(chand->info_service_config_json.get());
  }
  gpr_mu_unlock(&chand->info_mu);
}

/* Constructor for channel_data */
static grpc_error* cc_init_channel_elem(grpc_channel_element* elem,
                                        grpc_channel_element_args* args) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  GPR_ASSERT(args->is_last);
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);
  // Initialize data members.
  chand->combiner = grpc_combiner_create();
  grpc_connectivity_state_init(&chand->state_tracker, GRPC_CHANNEL_IDLE,
                               "client_channel");
  chand->disconnect_error = GRPC_ERROR_NONE;
  gpr_mu_init(&chand->info_mu);
  gpr_mu_init(&chand->external_connectivity_watcher_list_mu);

  gpr_mu_lock(&chand->external_connectivity_watcher_list_mu);
  chand->external_connectivity_watcher_list_head = nullptr;
  gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);

  chand->owning_stack = args->channel_stack;
  chand->deadline_checking_enabled =
      grpc_deadline_checking_enabled(args->channel_args);
  chand->interested_parties = grpc_pollset_set_create();
  grpc_client_channel_start_backup_polling(chand->interested_parties);
  // Record max per-RPC retry buffer size.
  const grpc_arg* arg = grpc_channel_args_find(
      args->channel_args, GRPC_ARG_PER_RPC_RETRY_BUFFER_SIZE);
  chand->per_rpc_retry_buffer_size = (size_t)grpc_channel_arg_get_integer(
      arg, {DEFAULT_PER_RPC_RETRY_BUFFER_SIZE, 0, INT_MAX});
  // Record enable_retries.
  arg = grpc_channel_args_find(args->channel_args, GRPC_ARG_ENABLE_RETRIES);
  chand->enable_retries = grpc_channel_arg_get_bool(arg, true);
  // Record client channel factory.
  chand->client_channel_factory =
      grpc_core::ClientChannelFactory::GetFromChannelArgs(args->channel_args);
  if (chand->client_channel_factory == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Missing client channel factory in args for client channel filter");
  }
  // Get server name to resolve, using proxy mapper if needed.
  arg = grpc_channel_args_find(args->channel_args, GRPC_ARG_SERVER_URI);
  if (arg == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Missing server uri in args for client channel filter");
  }
  if (arg->type != GRPC_ARG_STRING) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "server uri arg must be a string");
  }
  char* proxy_name = nullptr;
  grpc_channel_args* new_args = nullptr;
  grpc_proxy_mappers_map_name(arg->value.string, args->channel_args,
                              &proxy_name, &new_args);
  grpc_core::UniquePtr<char> target_uri(
      proxy_name != nullptr ? proxy_name : gpr_strdup(arg->value.string));
  // Instantiate subchannel pool.
  arg = grpc_channel_args_find(args->channel_args,
                               GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL);
  if (grpc_channel_arg_get_bool(arg, false)) {
    chand->subchannel_pool =
        grpc_core::MakeRefCounted<grpc_core::LocalSubchannelPool>();
  } else {
    chand->subchannel_pool = grpc_core::GlobalSubchannelPool::instance();
  }
  // Instantiate resolving LB policy.
  LoadBalancingPolicy::Args lb_args;
  lb_args.combiner = chand->combiner;
  lb_args.channel_control_helper =
      grpc_core::UniquePtr<LoadBalancingPolicy::ChannelControlHelper>(
          grpc_core::New<grpc_core::ClientChannelControlHelper>(chand));
  lb_args.args = new_args != nullptr ? new_args : args->channel_args;
  grpc_error* error = GRPC_ERROR_NONE;
  chand->resolving_lb_policy.reset(
      grpc_core::New<grpc_core::ResolvingLoadBalancingPolicy>(
          std::move(lb_args), &grpc_client_channel_routing_trace,
          std::move(target_uri), process_resolver_result_locked, chand,
          &error));
  grpc_channel_args_destroy(new_args);
  if (error != GRPC_ERROR_NONE) {
    // Orphan the resolving LB policy and flush the exec_ctx to ensure
    // that it finishes shutting down.  This ensures that if we are
    // failing, we destroy the ClientChannelControlHelper (and thus
    // unref the channel stack) before we return.
    // TODO(roth): This is not a complete solution, because it only
    // catches the case where channel stack initialization fails in this
    // particular filter.  If there is a failure in a different filter, we
    // will leave a dangling ref here, which can cause a crash.  Fortunately,
    // in practice, there are no other filters that can cause failures in
    // channel stack initialization, so this works for now.
    chand->resolving_lb_policy.reset();
    grpc_core::ExecCtx::Get()->Flush();
  } else {
    grpc_pollset_set_add_pollset_set(
        chand->resolving_lb_policy->interested_parties(),
        chand->interested_parties);
    if (grpc_client_channel_routing_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p: created resolving_lb_policy=%p", chand,
              chand->resolving_lb_policy.get());
    }
  }
  return error;
}

/* Destructor for channel_data */
static void cc_destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (chand->resolving_lb_policy != nullptr) {
    grpc_pollset_set_del_pollset_set(
        chand->resolving_lb_policy->interested_parties(),
        chand->interested_parties);
    chand->resolving_lb_policy.reset();
  }
  // TODO(roth): Once we convert the filter API to C++, there will no
  // longer be any need to explicitly reset these smart pointer data members.
  chand->picker.reset();
  chand->subchannel_pool.reset();
  chand->info_lb_policy_name.reset();
  chand->info_service_config_json.reset();
  chand->retry_throttle_data.reset();
  chand->method_params_table.reset();
  grpc_client_channel_stop_backup_polling(chand->interested_parties);
  grpc_pollset_set_destroy(chand->interested_parties);
  GRPC_COMBINER_UNREF(chand->combiner, "client_channel");
  GRPC_ERROR_UNREF(chand->disconnect_error);
  grpc_connectivity_state_destroy(&chand->state_tracker);
  gpr_mu_destroy(&chand->info_mu);
  gpr_mu_destroy(&chand->external_connectivity_watcher_list_mu);
}

/*************************************************************************
 * PER-CALL FUNCTIONS
 */

// Max number of batches that can be pending on a call at any given
// time.  This includes one batch for each of the following ops:
//   recv_initial_metadata
//   send_initial_metadata
//   recv_message
//   send_message
//   recv_trailing_metadata
//   send_trailing_metadata
#define MAX_PENDING_BATCHES 6

// Retry support:
//
// In order to support retries, we act as a proxy for stream op batches.
// When we get a batch from the surface, we add it to our list of pending
// batches, and we then use those batches to construct separate "child"
// batches to be started on the subchannel call.  When the child batches
// return, we then decide which pending batches have been completed and
// schedule their callbacks accordingly.  If a subchannel call fails and
// we want to retry it, we do a new pick and start again, constructing
// new "child" batches for the new subchannel call.
//
// Note that retries are committed when receiving data from the server
// (except for Trailers-Only responses).  However, there may be many
// send ops started before receiving any data, so we may have already
// completed some number of send ops (and returned the completions up to
// the surface) by the time we realize that we need to retry.  To deal
// with this, we cache data for send ops, so that we can replay them on a
// different subchannel call even after we have completed the original
// batches.
//
// There are two sets of data to maintain:
// - In call_data (in the parent channel), we maintain a list of pending
//   ops and cached data for send ops.
// - In the subchannel call, we maintain state to indicate what ops have
//   already been sent down to that call.
//
// When constructing the "child" batches, we compare those two sets of
// data to see which batches need to be sent to the subchannel call.

// TODO(roth): In subsequent PRs:
// - add support for transparent retries (including initial metadata)
// - figure out how to record stats in census for retries
//   (census filter is on top of this one)
// - add census stats for retries

namespace grpc_core {
namespace {
class QueuedPickCanceller;
}  // namespace
}  // namespace grpc_core

namespace {

struct call_data;

// State used for starting a retryable batch on a subchannel call.
// This provides its own grpc_transport_stream_op_batch and other data
// structures needed to populate the ops in the batch.
// We allocate one struct on the arena for each attempt at starting a
// batch on a given subchannel call.
struct subchannel_batch_data {
  subchannel_batch_data(grpc_call_element* elem, call_data* calld, int refcount,
                        bool set_on_complete);
  // All dtor code must be added in `destroy`. This is because we may
  // call closures in `subchannel_batch_data` after they are unrefed by
  // `batch_data_unref`, and msan would complain about accessing this class
  // after calling dtor. As a result we cannot call the `dtor` in
  // `batch_data_unref`.
  // TODO(soheil): We should try to call the dtor in `batch_data_unref`.
  ~subchannel_batch_data() { destroy(); }
  void destroy();

  gpr_refcount refs;
  grpc_call_element* elem;
  grpc_core::RefCountedPtr<grpc_core::SubchannelCall> subchannel_call;
  // The batch to use in the subchannel call.
  // Its payload field points to subchannel_call_retry_state.batch_payload.
  grpc_transport_stream_op_batch batch;
  // For intercepting on_complete.
  grpc_closure on_complete;
};

// Retry state associated with a subchannel call.
// Stored in the parent_data of the subchannel call object.
struct subchannel_call_retry_state {
  explicit subchannel_call_retry_state(grpc_call_context_element* context)
      : batch_payload(context),
        started_send_initial_metadata(false),
        completed_send_initial_metadata(false),
        started_send_trailing_metadata(false),
        completed_send_trailing_metadata(false),
        started_recv_initial_metadata(false),
        completed_recv_initial_metadata(false),
        started_recv_trailing_metadata(false),
        completed_recv_trailing_metadata(false),
        retry_dispatched(false) {}

  // subchannel_batch_data.batch.payload points to this.
  grpc_transport_stream_op_batch_payload batch_payload;
  // For send_initial_metadata.
  // Note that we need to make a copy of the initial metadata for each
  // subchannel call instead of just referring to the copy in call_data,
  // because filters in the subchannel stack will probably add entries,
  // so we need to start in a pristine state for each attempt of the call.
  grpc_linked_mdelem* send_initial_metadata_storage;
  grpc_metadata_batch send_initial_metadata;
  // For send_message.
  grpc_core::ManualConstructor<grpc_core::ByteStreamCache::CachingByteStream>
      send_message;
  // For send_trailing_metadata.
  grpc_linked_mdelem* send_trailing_metadata_storage;
  grpc_metadata_batch send_trailing_metadata;
  // For intercepting recv_initial_metadata.
  grpc_metadata_batch recv_initial_metadata;
  grpc_closure recv_initial_metadata_ready;
  bool trailing_metadata_available = false;
  // For intercepting recv_message.
  grpc_closure recv_message_ready;
  grpc_core::OrphanablePtr<grpc_core::ByteStream> recv_message;
  // For intercepting recv_trailing_metadata.
  grpc_metadata_batch recv_trailing_metadata;
  grpc_transport_stream_stats collect_stats;
  grpc_closure recv_trailing_metadata_ready;
  // These fields indicate which ops have been started and completed on
  // this subchannel call.
  size_t started_send_message_count = 0;
  size_t completed_send_message_count = 0;
  size_t started_recv_message_count = 0;
  size_t completed_recv_message_count = 0;
  bool started_send_initial_metadata : 1;
  bool completed_send_initial_metadata : 1;
  bool started_send_trailing_metadata : 1;
  bool completed_send_trailing_metadata : 1;
  bool started_recv_initial_metadata : 1;
  bool completed_recv_initial_metadata : 1;
  bool started_recv_trailing_metadata : 1;
  bool completed_recv_trailing_metadata : 1;
  // State for callback processing.
  subchannel_batch_data* recv_initial_metadata_ready_deferred_batch = nullptr;
  grpc_error* recv_initial_metadata_error = GRPC_ERROR_NONE;
  subchannel_batch_data* recv_message_ready_deferred_batch = nullptr;
  grpc_error* recv_message_error = GRPC_ERROR_NONE;
  subchannel_batch_data* recv_trailing_metadata_internal_batch = nullptr;
  // NOTE: Do not move this next to the metadata bitfields above. That would
  //       save space but will also result in a data race because compiler will
  //       generate a 2 byte store which overwrites the meta-data fields upon
  //       setting this field.
  bool retry_dispatched : 1;
};

// Pending batches stored in call data.
struct pending_batch {
  // The pending batch.  If nullptr, this slot is empty.
  grpc_transport_stream_op_batch* batch;
  // Indicates whether payload for send ops has been cached in call data.
  bool send_ops_cached;
};

/** Call data.  Holds a pointer to SubchannelCall and the
    associated machinery to create such a pointer.
    Handles queueing of stream ops until a call object is ready, waiting
    for initial metadata before trying to create a call object,
    and handling cancellation gracefully. */
struct call_data {
  call_data(grpc_call_element* elem, const channel_data& chand,
            const grpc_call_element_args& args)
      : deadline_state(elem, args.call_stack, args.call_combiner,
                       GPR_LIKELY(chand.deadline_checking_enabled)
                           ? args.deadline
                           : GRPC_MILLIS_INF_FUTURE),
        path(grpc_slice_ref_internal(args.path)),
        call_start_time(args.start_time),
        deadline(args.deadline),
        arena(args.arena),
        owning_call(args.call_stack),
        call_combiner(args.call_combiner),
        call_context(args.context),
        pending_send_initial_metadata(false),
        pending_send_message(false),
        pending_send_trailing_metadata(false),
        enable_retries(chand.enable_retries),
        retry_committed(false),
        last_attempt_got_server_pushback(false) {}

  ~call_data() {
    grpc_slice_unref_internal(path);
    GRPC_ERROR_UNREF(cancel_error);
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches); ++i) {
      GPR_ASSERT(pending_batches[i].batch == nullptr);
    }
  }

  // State for handling deadlines.
  // The code in deadline_filter.c requires this to be the first field.
  // TODO(roth): This is slightly sub-optimal in that grpc_deadline_state
  // and this struct both independently store pointers to the call stack
  // and call combiner.  If/when we have time, find a way to avoid this
  // without breaking the grpc_deadline_state abstraction.
  grpc_deadline_state deadline_state;

  grpc_slice path;  // Request path.
  gpr_timespec call_start_time;
  grpc_millis deadline;
  gpr_arena* arena;
  grpc_call_stack* owning_call;
  grpc_call_combiner* call_combiner;
  grpc_call_context_element* call_context;

  grpc_core::RefCountedPtr<ServerRetryThrottleData> retry_throttle_data;
  grpc_core::RefCountedPtr<ClientChannelMethodParams> method_params;

  grpc_core::RefCountedPtr<grpc_core::SubchannelCall> subchannel_call;

  // Set when we get a cancel_stream op.
  grpc_error* cancel_error = GRPC_ERROR_NONE;

  QueuedPick pick;
  bool pick_queued = false;
  bool service_config_applied = false;
  grpc_core::QueuedPickCanceller* pick_canceller = nullptr;
  grpc_closure pick_closure;

  grpc_polling_entity* pollent = nullptr;

  // Batches are added to this list when received from above.
  // They are removed when we are done handling the batch (i.e., when
  // either we have invoked all of the batch's callbacks or we have
  // passed the batch down to the subchannel call and are not
  // intercepting any of its callbacks).
  pending_batch pending_batches[MAX_PENDING_BATCHES] = {};
  bool pending_send_initial_metadata : 1;
  bool pending_send_message : 1;
  bool pending_send_trailing_metadata : 1;

  // Retry state.
  bool enable_retries : 1;
  bool retry_committed : 1;
  bool last_attempt_got_server_pushback : 1;
  int num_attempts_completed = 0;
  size_t bytes_buffered_for_retry = 0;
  grpc_core::ManualConstructor<grpc_core::BackOff> retry_backoff;
  grpc_timer retry_timer;

  // The number of pending retriable subchannel batches containing send ops.
  // We hold a ref to the call stack while this is non-zero, since replay
  // batches may not complete until after all callbacks have been returned
  // to the surface, and we need to make sure that the call is not destroyed
  // until all of these batches have completed.
  // Note that we actually only need to track replay batches, but it's
  // easier to track all batches with send ops.
  int num_pending_retriable_subchannel_send_batches = 0;

  // Cached data for retrying send ops.
  // send_initial_metadata
  bool seen_send_initial_metadata = false;
  grpc_linked_mdelem* send_initial_metadata_storage = nullptr;
  grpc_metadata_batch send_initial_metadata;
  uint32_t send_initial_metadata_flags;
  gpr_atm* peer_string;
  // send_message
  // When we get a send_message op, we replace the original byte stream
  // with a CachingByteStream that caches the slices to a local buffer for
  // use in retries.
  // Note: We inline the cache for the first 3 send_message ops and use
  // dynamic allocation after that.  This number was essentially picked
  // at random; it could be changed in the future to tune performance.
  grpc_core::InlinedVector<grpc_core::ByteStreamCache*, 3> send_messages;
  // send_trailing_metadata
  bool seen_send_trailing_metadata = false;
  grpc_linked_mdelem* send_trailing_metadata_storage = nullptr;
  grpc_metadata_batch send_trailing_metadata;
};

}  // namespace

// Forward declarations.
static void retry_commit(grpc_call_element* elem,
                         subchannel_call_retry_state* retry_state);
static void start_internal_recv_trailing_metadata(grpc_call_element* elem);
static void on_complete(void* arg, grpc_error* error);
static void start_retriable_subchannel_batches(void* arg, grpc_error* ignored);
static void remove_call_from_queued_picks_locked(grpc_call_element* elem);

//
// send op data caching
//

// Caches data for send ops so that it can be retried later, if not
// already cached.
static void maybe_cache_send_ops_for_batch(call_data* calld,
                                           pending_batch* pending) {
  if (pending->send_ops_cached) return;
  pending->send_ops_cached = true;
  grpc_transport_stream_op_batch* batch = pending->batch;
  // Save a copy of metadata for send_initial_metadata ops.
  if (batch->send_initial_metadata) {
    calld->seen_send_initial_metadata = true;
    GPR_ASSERT(calld->send_initial_metadata_storage == nullptr);
    grpc_metadata_batch* send_initial_metadata =
        batch->payload->send_initial_metadata.send_initial_metadata;
    calld->send_initial_metadata_storage = (grpc_linked_mdelem*)gpr_arena_alloc(
        calld->arena,
        sizeof(grpc_linked_mdelem) * send_initial_metadata->list.count);
    grpc_metadata_batch_copy(send_initial_metadata,
                             &calld->send_initial_metadata,
                             calld->send_initial_metadata_storage);
    calld->send_initial_metadata_flags =
        batch->payload->send_initial_metadata.send_initial_metadata_flags;
    calld->peer_string = batch->payload->send_initial_metadata.peer_string;
  }
  // Set up cache for send_message ops.
  if (batch->send_message) {
    grpc_core::ByteStreamCache* cache =
        static_cast<grpc_core::ByteStreamCache*>(
            gpr_arena_alloc(calld->arena, sizeof(grpc_core::ByteStreamCache)));
    new (cache) grpc_core::ByteStreamCache(
        std::move(batch->payload->send_message.send_message));
    calld->send_messages.push_back(cache);
  }
  // Save metadata batch for send_trailing_metadata ops.
  if (batch->send_trailing_metadata) {
    calld->seen_send_trailing_metadata = true;
    GPR_ASSERT(calld->send_trailing_metadata_storage == nullptr);
    grpc_metadata_batch* send_trailing_metadata =
        batch->payload->send_trailing_metadata.send_trailing_metadata;
    calld->send_trailing_metadata_storage =
        (grpc_linked_mdelem*)gpr_arena_alloc(
            calld->arena,
            sizeof(grpc_linked_mdelem) * send_trailing_metadata->list.count);
    grpc_metadata_batch_copy(send_trailing_metadata,
                             &calld->send_trailing_metadata,
                             calld->send_trailing_metadata_storage);
  }
}

// Frees cached send_initial_metadata.
static void free_cached_send_initial_metadata(channel_data* chand,
                                              call_data* calld) {
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: destroying calld->send_initial_metadata", chand,
            calld);
  }
  grpc_metadata_batch_destroy(&calld->send_initial_metadata);
}

// Frees cached send_message at index idx.
static void free_cached_send_message(channel_data* chand, call_data* calld,
                                     size_t idx) {
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: destroying calld->send_messages[%" PRIuPTR "]",
            chand, calld, idx);
  }
  calld->send_messages[idx]->Destroy();
}

// Frees cached send_trailing_metadata.
static void free_cached_send_trailing_metadata(channel_data* chand,
                                               call_data* calld) {
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: destroying calld->send_trailing_metadata",
            chand, calld);
  }
  grpc_metadata_batch_destroy(&calld->send_trailing_metadata);
}

// Frees cached send ops that have already been completed after
// committing the call.
static void free_cached_send_op_data_after_commit(
    grpc_call_element* elem, subchannel_call_retry_state* retry_state) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (retry_state->completed_send_initial_metadata) {
    free_cached_send_initial_metadata(chand, calld);
  }
  for (size_t i = 0; i < retry_state->completed_send_message_count; ++i) {
    free_cached_send_message(chand, calld, i);
  }
  if (retry_state->completed_send_trailing_metadata) {
    free_cached_send_trailing_metadata(chand, calld);
  }
}

// Frees cached send ops that were completed by the completed batch in
// batch_data.  Used when batches are completed after the call is committed.
static void free_cached_send_op_data_for_completed_batch(
    grpc_call_element* elem, subchannel_batch_data* batch_data,
    subchannel_call_retry_state* retry_state) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (batch_data->batch.send_initial_metadata) {
    free_cached_send_initial_metadata(chand, calld);
  }
  if (batch_data->batch.send_message) {
    free_cached_send_message(chand, calld,
                             retry_state->completed_send_message_count - 1);
  }
  if (batch_data->batch.send_trailing_metadata) {
    free_cached_send_trailing_metadata(chand, calld);
  }
}

//
// LB recv_trailing_metadata_ready handling
//

void maybe_inject_recv_trailing_metadata_ready_for_lb(
    const LoadBalancingPolicy::PickArgs& pick,
    grpc_transport_stream_op_batch* batch) {
  if (pick.recv_trailing_metadata_ready != nullptr) {
    *pick.original_recv_trailing_metadata_ready =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        pick.recv_trailing_metadata_ready;
    if (pick.recv_trailing_metadata != nullptr) {
      *pick.recv_trailing_metadata =
          batch->payload->recv_trailing_metadata.recv_trailing_metadata;
    }
  }
}

//
// pending_batches management
//

// Returns the index into calld->pending_batches to be used for batch.
static size_t get_batch_index(grpc_transport_stream_op_batch* batch) {
  // Note: It is important the send_initial_metadata be the first entry
  // here, since the code in pick_subchannel_locked() assumes it will be.
  if (batch->send_initial_metadata) return 0;
  if (batch->send_message) return 1;
  if (batch->send_trailing_metadata) return 2;
  if (batch->recv_initial_metadata) return 3;
  if (batch->recv_message) return 4;
  if (batch->recv_trailing_metadata) return 5;
  GPR_UNREACHABLE_CODE(return (size_t)-1);
}

// This is called via the call combiner, so access to calld is synchronized.
static void pending_batches_add(grpc_call_element* elem,
                                grpc_transport_stream_op_batch* batch) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  const size_t idx = get_batch_index(batch);
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: adding pending batch at index %" PRIuPTR, chand,
            calld, idx);
  }
  pending_batch* pending = &calld->pending_batches[idx];
  GPR_ASSERT(pending->batch == nullptr);
  pending->batch = batch;
  pending->send_ops_cached = false;
  if (calld->enable_retries) {
    // Update state in calld about pending batches.
    // Also check if the batch takes us over the retry buffer limit.
    // Note: We don't check the size of trailing metadata here, because
    // gRPC clients do not send trailing metadata.
    if (batch->send_initial_metadata) {
      calld->pending_send_initial_metadata = true;
      calld->bytes_buffered_for_retry += grpc_metadata_batch_size(
          batch->payload->send_initial_metadata.send_initial_metadata);
    }
    if (batch->send_message) {
      calld->pending_send_message = true;
      calld->bytes_buffered_for_retry +=
          batch->payload->send_message.send_message->length();
    }
    if (batch->send_trailing_metadata) {
      calld->pending_send_trailing_metadata = true;
    }
    if (GPR_UNLIKELY(calld->bytes_buffered_for_retry >
                     chand->per_rpc_retry_buffer_size)) {
      if (grpc_client_channel_call_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: exceeded retry buffer size, committing",
                chand, calld);
      }
      subchannel_call_retry_state* retry_state =
          calld->subchannel_call == nullptr
              ? nullptr
              : static_cast<subchannel_call_retry_state*>(

                    calld->subchannel_call->GetParentData());
      retry_commit(elem, retry_state);
      // If we are not going to retry and have not yet started, pretend
      // retries are disabled so that we don't bother with retry overhead.
      if (calld->num_attempts_completed == 0) {
        if (grpc_client_channel_call_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "chand=%p calld=%p: disabling retries before first attempt",
                  chand, calld);
        }
        calld->enable_retries = false;
      }
    }
  }
}

static void pending_batch_clear(call_data* calld, pending_batch* pending) {
  if (calld->enable_retries) {
    if (pending->batch->send_initial_metadata) {
      calld->pending_send_initial_metadata = false;
    }
    if (pending->batch->send_message) {
      calld->pending_send_message = false;
    }
    if (pending->batch->send_trailing_metadata) {
      calld->pending_send_trailing_metadata = false;
    }
  }
  pending->batch = nullptr;
}

// This is called via the call combiner, so access to calld is synchronized.
static void fail_pending_batch_in_call_combiner(void* arg, grpc_error* error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  call_data* calld = static_cast<call_data*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  grpc_transport_stream_op_batch_finish_with_failure(
      batch, GRPC_ERROR_REF(error), calld->call_combiner);
}

// This is called via the call combiner, so access to calld is synchronized.
// If yield_call_combiner_predicate returns true, assumes responsibility for
// yielding the call combiner.
typedef bool (*YieldCallCombinerPredicate)(
    const grpc_core::CallCombinerClosureList& closures);
static bool yield_call_combiner(
    const grpc_core::CallCombinerClosureList& closures) {
  return true;
}
static bool no_yield_call_combiner(
    const grpc_core::CallCombinerClosureList& closures) {
  return false;
}
static bool yield_call_combiner_if_pending_batches_found(
    const grpc_core::CallCombinerClosureList& closures) {
  return closures.size() > 0;
}
static void pending_batches_fail(
    grpc_call_element* elem, grpc_error* error,
    YieldCallCombinerPredicate yield_call_combiner_predicate) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_call_trace.enabled()) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
      if (calld->pending_batches[i].batch != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: failing %" PRIuPTR " pending batches: %s",
            elem->channel_data, calld, num_batches, grpc_error_string(error));
  }
  grpc_core::CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    pending_batch* pending = &calld->pending_batches[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch != nullptr) {
      if (batch->recv_trailing_metadata) {
        maybe_inject_recv_trailing_metadata_ready_for_lb(calld->pick.pick,
                                                         batch);
      }
      batch->handler_private.extra_arg = calld;
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        fail_pending_batch_in_call_combiner, batch,
                        grpc_schedule_on_exec_ctx);
      closures.Add(&batch->handler_private.closure, GRPC_ERROR_REF(error),
                   "pending_batches_fail");
      pending_batch_clear(calld, pending);
    }
  }
  if (yield_call_combiner_predicate(closures)) {
    closures.RunClosures(calld->call_combiner);
  } else {
    closures.RunClosuresWithoutYielding(calld->call_combiner);
  }
  GRPC_ERROR_UNREF(error);
}

// This is called via the call combiner, so access to calld is synchronized.
static void resume_pending_batch_in_call_combiner(void* arg,
                                                  grpc_error* ignored) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  grpc_core::SubchannelCall* subchannel_call =
      static_cast<grpc_core::SubchannelCall*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  subchannel_call->StartTransportStreamOpBatch(batch);
}

// This is called via the call combiner, so access to calld is synchronized.
static void pending_batches_resume(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (calld->enable_retries) {
    start_retriable_subchannel_batches(elem, GRPC_ERROR_NONE);
    return;
  }
  // Retries not enabled; send down batches as-is.
  if (grpc_client_channel_call_trace.enabled()) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
      if (calld->pending_batches[i].batch != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting %" PRIuPTR
            " pending batches on subchannel_call=%p",
            chand, calld, num_batches, calld->subchannel_call.get());
  }
  grpc_core::CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    pending_batch* pending = &calld->pending_batches[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch != nullptr) {
      if (batch->recv_trailing_metadata) {
        maybe_inject_recv_trailing_metadata_ready_for_lb(calld->pick.pick,
                                                         batch);
      }
      batch->handler_private.extra_arg = calld->subchannel_call.get();
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        resume_pending_batch_in_call_combiner, batch,
                        grpc_schedule_on_exec_ctx);
      closures.Add(&batch->handler_private.closure, GRPC_ERROR_NONE,
                   "pending_batches_resume");
      pending_batch_clear(calld, pending);
    }
  }
  // Note: This will release the call combiner.
  closures.RunClosures(calld->call_combiner);
}

static void maybe_clear_pending_batch(grpc_call_element* elem,
                                      pending_batch* pending) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_transport_stream_op_batch* batch = pending->batch;
  // We clear the pending batch if all of its callbacks have been
  // scheduled and reset to nullptr.
  if (batch->on_complete == nullptr &&
      (!batch->recv_initial_metadata ||
       batch->payload->recv_initial_metadata.recv_initial_metadata_ready ==
           nullptr) &&
      (!batch->recv_message ||
       batch->payload->recv_message.recv_message_ready == nullptr) &&
      (!batch->recv_trailing_metadata ||
       batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready ==
           nullptr)) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: clearing pending batch", chand,
              calld);
    }
    pending_batch_clear(calld, pending);
  }
}

// Returns a pointer to the first pending batch for which predicate(batch)
// returns true, or null if not found.
template <typename Predicate>
static pending_batch* pending_batch_find(grpc_call_element* elem,
                                         const char* log_message,
                                         Predicate predicate) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    pending_batch* pending = &calld->pending_batches[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch != nullptr && predicate(batch)) {
      if (grpc_client_channel_call_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: %s pending batch at index %" PRIuPTR, chand,
                calld, log_message, i);
      }
      return pending;
    }
  }
  return nullptr;
}

//
// retry code
//

// Commits the call so that no further retry attempts will be performed.
static void retry_commit(grpc_call_element* elem,
                         subchannel_call_retry_state* retry_state) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (calld->retry_committed) return;
  calld->retry_committed = true;
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: committing retries", chand, calld);
  }
  if (retry_state != nullptr) {
    free_cached_send_op_data_after_commit(elem, retry_state);
  }
}

// Starts a retry after appropriate back-off.
static void do_retry(grpc_call_element* elem,
                     subchannel_call_retry_state* retry_state,
                     grpc_millis server_pushback_ms) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  GPR_ASSERT(calld->method_params != nullptr);
  const ClientChannelMethodParams::RetryPolicy* retry_policy =
      calld->method_params->retry_policy();
  GPR_ASSERT(retry_policy != nullptr);
  // Reset subchannel call and connected subchannel.
  calld->subchannel_call.reset();
  calld->pick.pick.connected_subchannel.reset();
  // Compute backoff delay.
  grpc_millis next_attempt_time;
  if (server_pushback_ms >= 0) {
    next_attempt_time = grpc_core::ExecCtx::Get()->Now() + server_pushback_ms;
    calld->last_attempt_got_server_pushback = true;
  } else {
    if (calld->num_attempts_completed == 1 ||
        calld->last_attempt_got_server_pushback) {
      calld->retry_backoff.Init(
          grpc_core::BackOff::Options()
              .set_initial_backoff(retry_policy->initial_backoff)
              .set_multiplier(retry_policy->backoff_multiplier)
              .set_jitter(RETRY_BACKOFF_JITTER)
              .set_max_backoff(retry_policy->max_backoff));
      calld->last_attempt_got_server_pushback = false;
    }
    next_attempt_time = calld->retry_backoff->NextAttemptTime();
  }
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: retrying failed call in %" PRId64 " ms", chand,
            calld, next_attempt_time - grpc_core::ExecCtx::Get()->Now());
  }
  // Schedule retry after computed delay.
  GRPC_CLOSURE_INIT(&calld->pick_closure, start_pick_locked, elem,
                    grpc_combiner_scheduler(chand->combiner));
  grpc_timer_init(&calld->retry_timer, next_attempt_time, &calld->pick_closure);
  // Update bookkeeping.
  if (retry_state != nullptr) retry_state->retry_dispatched = true;
}

// Returns true if the call is being retried.
static bool maybe_retry(grpc_call_element* elem,
                        subchannel_batch_data* batch_data,
                        grpc_status_code status,
                        grpc_mdelem* server_pushback_md) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  // Get retry policy.
  if (calld->method_params == nullptr) return false;
  const ClientChannelMethodParams::RetryPolicy* retry_policy =
      calld->method_params->retry_policy();
  if (retry_policy == nullptr) return false;
  // If we've already dispatched a retry from this call, return true.
  // This catches the case where the batch has multiple callbacks
  // (i.e., it includes either recv_message or recv_initial_metadata).
  subchannel_call_retry_state* retry_state = nullptr;
  if (batch_data != nullptr) {
    retry_state = static_cast<subchannel_call_retry_state*>(
        batch_data->subchannel_call->GetParentData());
    if (retry_state->retry_dispatched) {
      if (grpc_client_channel_call_trace.enabled()) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: retry already dispatched", chand,
                calld);
      }
      return true;
    }
  }
  // Check status.
  if (GPR_LIKELY(status == GRPC_STATUS_OK)) {
    if (calld->retry_throttle_data != nullptr) {
      calld->retry_throttle_data->RecordSuccess();
    }
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: call succeeded", chand, calld);
    }
    return false;
  }
  // Status is not OK.  Check whether the status is retryable.
  if (!retry_policy->retryable_status_codes.Contains(status)) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: status %s not configured as retryable", chand,
              calld, grpc_status_code_to_string(status));
    }
    return false;
  }
  // Record the failure and check whether retries are throttled.
  // Note that it's important for this check to come after the status
  // code check above, since we should only record failures whose statuses
  // match the configured retryable status codes, so that we don't count
  // things like failures due to malformed requests (INVALID_ARGUMENT).
  // Conversely, it's important for this to come before the remaining
  // checks, so that we don't fail to record failures due to other factors.
  if (calld->retry_throttle_data != nullptr &&
      !calld->retry_throttle_data->RecordFailure()) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: retries throttled", chand, calld);
    }
    return false;
  }
  // Check whether the call is committed.
  if (calld->retry_committed) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: retries already committed", chand,
              calld);
    }
    return false;
  }
  // Check whether we have retries remaining.
  ++calld->num_attempts_completed;
  if (calld->num_attempts_completed >= retry_policy->max_attempts) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: exceeded %d retry attempts", chand,
              calld, retry_policy->max_attempts);
    }
    return false;
  }
  // If the call was cancelled from the surface, don't retry.
  if (calld->cancel_error != GRPC_ERROR_NONE) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: call cancelled from surface, not retrying",
              chand, calld);
    }
    return false;
  }
  // Check server push-back.
  grpc_millis server_pushback_ms = -1;
  if (server_pushback_md != nullptr) {
    // If the value is "-1" or any other unparseable string, we do not retry.
    uint32_t ms;
    if (!grpc_parse_slice_to_uint32(GRPC_MDVALUE(*server_pushback_md), &ms)) {
      if (grpc_client_channel_call_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: not retrying due to server push-back",
                chand, calld);
      }
      return false;
    } else {
      if (grpc_client_channel_call_trace.enabled()) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: server push-back: retry in %u ms",
                chand, calld, ms);
      }
      server_pushback_ms = (grpc_millis)ms;
    }
  }
  do_retry(elem, retry_state, server_pushback_ms);
  return true;
}

//
// subchannel_batch_data
//

namespace {

subchannel_batch_data::subchannel_batch_data(grpc_call_element* elem,
                                             call_data* calld, int refcount,
                                             bool set_on_complete)
    : elem(elem), subchannel_call(calld->subchannel_call) {
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          calld->subchannel_call->GetParentData());
  batch.payload = &retry_state->batch_payload;
  gpr_ref_init(&refs, refcount);
  if (set_on_complete) {
    GRPC_CLOSURE_INIT(&on_complete, ::on_complete, this,
                      grpc_schedule_on_exec_ctx);
    batch.on_complete = &on_complete;
  }
  GRPC_CALL_STACK_REF(calld->owning_call, "batch_data");
}

void subchannel_batch_data::destroy() {
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          subchannel_call->GetParentData());
  if (batch.send_initial_metadata) {
    grpc_metadata_batch_destroy(&retry_state->send_initial_metadata);
  }
  if (batch.send_trailing_metadata) {
    grpc_metadata_batch_destroy(&retry_state->send_trailing_metadata);
  }
  if (batch.recv_initial_metadata) {
    grpc_metadata_batch_destroy(&retry_state->recv_initial_metadata);
  }
  if (batch.recv_trailing_metadata) {
    grpc_metadata_batch_destroy(&retry_state->recv_trailing_metadata);
  }
  subchannel_call.reset();
  call_data* calld = static_cast<call_data*>(elem->call_data);
  GRPC_CALL_STACK_UNREF(calld->owning_call, "batch_data");
}

}  // namespace

// Creates a subchannel_batch_data object on the call's arena with the
// specified refcount.  If set_on_complete is true, the batch's
// on_complete callback will be set to point to on_complete();
// otherwise, the batch's on_complete callback will be null.
static subchannel_batch_data* batch_data_create(grpc_call_element* elem,
                                                int refcount,
                                                bool set_on_complete) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  subchannel_batch_data* batch_data =
      new (gpr_arena_alloc(calld->arena, sizeof(*batch_data)))
          subchannel_batch_data(elem, calld, refcount, set_on_complete);
  return batch_data;
}

static void batch_data_unref(subchannel_batch_data* batch_data) {
  if (gpr_unref(&batch_data->refs)) {
    batch_data->destroy();
  }
}

//
// recv_initial_metadata callback handling
//

// Invokes recv_initial_metadata_ready for a subchannel batch.
static void invoke_recv_initial_metadata_callback(void* arg,
                                                  grpc_error* error) {
  subchannel_batch_data* batch_data = static_cast<subchannel_batch_data*>(arg);
  // Find pending batch.
  pending_batch* pending = pending_batch_find(
      batch_data->elem, "invoking recv_initial_metadata_ready for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_initial_metadata &&
               batch->payload->recv_initial_metadata
                       .recv_initial_metadata_ready != nullptr;
      });
  GPR_ASSERT(pending != nullptr);
  // Return metadata.
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          batch_data->subchannel_call->GetParentData());
  grpc_metadata_batch_move(
      &retry_state->recv_initial_metadata,
      pending->batch->payload->recv_initial_metadata.recv_initial_metadata);
  // Update bookkeeping.
  // Note: Need to do this before invoking the callback, since invoking
  // the callback will result in yielding the call combiner.
  grpc_closure* recv_initial_metadata_ready =
      pending->batch->payload->recv_initial_metadata
          .recv_initial_metadata_ready;
  pending->batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
      nullptr;
  maybe_clear_pending_batch(batch_data->elem, pending);
  batch_data_unref(batch_data);
  // Invoke callback.
  GRPC_CLOSURE_RUN(recv_initial_metadata_ready, GRPC_ERROR_REF(error));
}

// Intercepts recv_initial_metadata_ready callback for retries.
// Commits the call and returns the initial metadata up the stack.
static void recv_initial_metadata_ready(void* arg, grpc_error* error) {
  subchannel_batch_data* batch_data = static_cast<subchannel_batch_data*>(arg);
  grpc_call_element* elem = batch_data->elem;
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: got recv_initial_metadata_ready, error=%s",
            chand, calld, grpc_error_string(error));
  }
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          batch_data->subchannel_call->GetParentData());
  retry_state->completed_recv_initial_metadata = true;
  // If a retry was already dispatched, then we're not going to use the
  // result of this recv_initial_metadata op, so do nothing.
  if (retry_state->retry_dispatched) {
    GRPC_CALL_COMBINER_STOP(
        calld->call_combiner,
        "recv_initial_metadata_ready after retry dispatched");
    return;
  }
  // If we got an error or a Trailers-Only response and have not yet gotten
  // the recv_trailing_metadata_ready callback, then defer propagating this
  // callback back to the surface.  We can evaluate whether to retry when
  // recv_trailing_metadata comes back.
  if (GPR_UNLIKELY((retry_state->trailing_metadata_available ||
                    error != GRPC_ERROR_NONE) &&
                   !retry_state->completed_recv_trailing_metadata)) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: deferring recv_initial_metadata_ready "
              "(Trailers-Only)",
              chand, calld);
    }
    retry_state->recv_initial_metadata_ready_deferred_batch = batch_data;
    retry_state->recv_initial_metadata_error = GRPC_ERROR_REF(error);
    if (!retry_state->started_recv_trailing_metadata) {
      // recv_trailing_metadata not yet started by application; start it
      // ourselves to get status.
      start_internal_recv_trailing_metadata(elem);
    } else {
      GRPC_CALL_COMBINER_STOP(
          calld->call_combiner,
          "recv_initial_metadata_ready trailers-only or error");
    }
    return;
  }
  // Received valid initial metadata, so commit the call.
  retry_commit(elem, retry_state);
  // Invoke the callback to return the result to the surface.
  // Manually invoking a callback function; it does not take ownership of error.
  invoke_recv_initial_metadata_callback(batch_data, error);
}

//
// recv_message callback handling
//

// Invokes recv_message_ready for a subchannel batch.
static void invoke_recv_message_callback(void* arg, grpc_error* error) {
  subchannel_batch_data* batch_data = static_cast<subchannel_batch_data*>(arg);
  // Find pending op.
  pending_batch* pending = pending_batch_find(
      batch_data->elem, "invoking recv_message_ready for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_message &&
               batch->payload->recv_message.recv_message_ready != nullptr;
      });
  GPR_ASSERT(pending != nullptr);
  // Return payload.
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          batch_data->subchannel_call->GetParentData());
  *pending->batch->payload->recv_message.recv_message =
      std::move(retry_state->recv_message);
  // Update bookkeeping.
  // Note: Need to do this before invoking the callback, since invoking
  // the callback will result in yielding the call combiner.
  grpc_closure* recv_message_ready =
      pending->batch->payload->recv_message.recv_message_ready;
  pending->batch->payload->recv_message.recv_message_ready = nullptr;
  maybe_clear_pending_batch(batch_data->elem, pending);
  batch_data_unref(batch_data);
  // Invoke callback.
  GRPC_CLOSURE_RUN(recv_message_ready, GRPC_ERROR_REF(error));
}

// Intercepts recv_message_ready callback for retries.
// Commits the call and returns the message up the stack.
static void recv_message_ready(void* arg, grpc_error* error) {
  subchannel_batch_data* batch_data = static_cast<subchannel_batch_data*>(arg);
  grpc_call_element* elem = batch_data->elem;
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: got recv_message_ready, error=%s",
            chand, calld, grpc_error_string(error));
  }
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          batch_data->subchannel_call->GetParentData());
  ++retry_state->completed_recv_message_count;
  // If a retry was already dispatched, then we're not going to use the
  // result of this recv_message op, so do nothing.
  if (retry_state->retry_dispatched) {
    GRPC_CALL_COMBINER_STOP(calld->call_combiner,
                            "recv_message_ready after retry dispatched");
    return;
  }
  // If we got an error or the payload was nullptr and we have not yet gotten
  // the recv_trailing_metadata_ready callback, then defer propagating this
  // callback back to the surface.  We can evaluate whether to retry when
  // recv_trailing_metadata comes back.
  if (GPR_UNLIKELY(
          (retry_state->recv_message == nullptr || error != GRPC_ERROR_NONE) &&
          !retry_state->completed_recv_trailing_metadata)) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: deferring recv_message_ready (nullptr "
              "message and recv_trailing_metadata pending)",
              chand, calld);
    }
    retry_state->recv_message_ready_deferred_batch = batch_data;
    retry_state->recv_message_error = GRPC_ERROR_REF(error);
    if (!retry_state->started_recv_trailing_metadata) {
      // recv_trailing_metadata not yet started by application; start it
      // ourselves to get status.
      start_internal_recv_trailing_metadata(elem);
    } else {
      GRPC_CALL_COMBINER_STOP(calld->call_combiner, "recv_message_ready null");
    }
    return;
  }
  // Received a valid message, so commit the call.
  retry_commit(elem, retry_state);
  // Invoke the callback to return the result to the surface.
  // Manually invoking a callback function; it does not take ownership of error.
  invoke_recv_message_callback(batch_data, error);
}

//
// recv_trailing_metadata handling
//

// Sets *status and *server_pushback_md based on md_batch and error.
// Only sets *server_pushback_md if server_pushback_md != nullptr.
static void get_call_status(grpc_call_element* elem,
                            grpc_metadata_batch* md_batch, grpc_error* error,
                            grpc_status_code* status,
                            grpc_mdelem** server_pushback_md) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (error != GRPC_ERROR_NONE) {
    grpc_error_get_status(error, calld->deadline, status, nullptr, nullptr,
                          nullptr);
  } else {
    GPR_ASSERT(md_batch->idx.named.grpc_status != nullptr);
    *status =
        grpc_get_status_code_from_metadata(md_batch->idx.named.grpc_status->md);
    if (server_pushback_md != nullptr &&
        md_batch->idx.named.grpc_retry_pushback_ms != nullptr) {
      *server_pushback_md = &md_batch->idx.named.grpc_retry_pushback_ms->md;
    }
  }
  GRPC_ERROR_UNREF(error);
}

// Adds recv_trailing_metadata_ready closure to closures.
static void add_closure_for_recv_trailing_metadata_ready(
    grpc_call_element* elem, subchannel_batch_data* batch_data,
    grpc_error* error, grpc_core::CallCombinerClosureList* closures) {
  // Find pending batch.
  pending_batch* pending = pending_batch_find(
      elem, "invoking recv_trailing_metadata for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_trailing_metadata &&
               batch->payload->recv_trailing_metadata
                       .recv_trailing_metadata_ready != nullptr;
      });
  // If we generated the recv_trailing_metadata op internally via
  // start_internal_recv_trailing_metadata(), then there will be no
  // pending batch.
  if (pending == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  // Return metadata.
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          batch_data->subchannel_call->GetParentData());
  grpc_metadata_batch_move(
      &retry_state->recv_trailing_metadata,
      pending->batch->payload->recv_trailing_metadata.recv_trailing_metadata);
  // Add closure.
  closures->Add(pending->batch->payload->recv_trailing_metadata
                    .recv_trailing_metadata_ready,
                error, "recv_trailing_metadata_ready for pending batch");
  // Update bookkeeping.
  pending->batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      nullptr;
  maybe_clear_pending_batch(elem, pending);
}

// Adds any necessary closures for deferred recv_initial_metadata and
// recv_message callbacks to closures.
static void add_closures_for_deferred_recv_callbacks(
    subchannel_batch_data* batch_data, subchannel_call_retry_state* retry_state,
    grpc_core::CallCombinerClosureList* closures) {
  if (batch_data->batch.recv_trailing_metadata) {
    // Add closure for deferred recv_initial_metadata_ready.
    if (GPR_UNLIKELY(retry_state->recv_initial_metadata_ready_deferred_batch !=
                     nullptr)) {
      GRPC_CLOSURE_INIT(&retry_state->recv_initial_metadata_ready,
                        invoke_recv_initial_metadata_callback,
                        retry_state->recv_initial_metadata_ready_deferred_batch,
                        grpc_schedule_on_exec_ctx);
      closures->Add(&retry_state->recv_initial_metadata_ready,
                    retry_state->recv_initial_metadata_error,
                    "resuming recv_initial_metadata_ready");
      retry_state->recv_initial_metadata_ready_deferred_batch = nullptr;
    }
    // Add closure for deferred recv_message_ready.
    if (GPR_UNLIKELY(retry_state->recv_message_ready_deferred_batch !=
                     nullptr)) {
      GRPC_CLOSURE_INIT(&retry_state->recv_message_ready,
                        invoke_recv_message_callback,
                        retry_state->recv_message_ready_deferred_batch,
                        grpc_schedule_on_exec_ctx);
      closures->Add(&retry_state->recv_message_ready,
                    retry_state->recv_message_error,
                    "resuming recv_message_ready");
      retry_state->recv_message_ready_deferred_batch = nullptr;
    }
  }
}

// Returns true if any op in the batch was not yet started.
// Only looks at send ops, since recv ops are always started immediately.
static bool pending_batch_is_unstarted(
    pending_batch* pending, call_data* calld,
    subchannel_call_retry_state* retry_state) {
  if (pending->batch == nullptr || pending->batch->on_complete == nullptr) {
    return false;
  }
  if (pending->batch->send_initial_metadata &&
      !retry_state->started_send_initial_metadata) {
    return true;
  }
  if (pending->batch->send_message &&
      retry_state->started_send_message_count < calld->send_messages.size()) {
    return true;
  }
  if (pending->batch->send_trailing_metadata &&
      !retry_state->started_send_trailing_metadata) {
    return true;
  }
  return false;
}

// For any pending batch containing an op that has not yet been started,
// adds the pending batch's completion closures to closures.
static void add_closures_to_fail_unstarted_pending_batches(
    grpc_call_element* elem, subchannel_call_retry_state* retry_state,
    grpc_error* error, grpc_core::CallCombinerClosureList* closures) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    pending_batch* pending = &calld->pending_batches[i];
    if (pending_batch_is_unstarted(pending, calld, retry_state)) {
      if (grpc_client_channel_call_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: failing unstarted pending batch at index "
                "%" PRIuPTR,
                chand, calld, i);
      }
      closures->Add(pending->batch->on_complete, GRPC_ERROR_REF(error),
                    "failing on_complete for pending batch");
      pending->batch->on_complete = nullptr;
      maybe_clear_pending_batch(elem, pending);
    }
  }
  GRPC_ERROR_UNREF(error);
}

// Runs necessary closures upon completion of a call attempt.
static void run_closures_for_completed_call(subchannel_batch_data* batch_data,
                                            grpc_error* error) {
  grpc_call_element* elem = batch_data->elem;
  call_data* calld = static_cast<call_data*>(elem->call_data);
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          batch_data->subchannel_call->GetParentData());
  // Construct list of closures to execute.
  grpc_core::CallCombinerClosureList closures;
  // First, add closure for recv_trailing_metadata_ready.
  add_closure_for_recv_trailing_metadata_ready(
      elem, batch_data, GRPC_ERROR_REF(error), &closures);
  // If there are deferred recv_initial_metadata_ready or recv_message_ready
  // callbacks, add them to closures.
  add_closures_for_deferred_recv_callbacks(batch_data, retry_state, &closures);
  // Add closures to fail any pending batches that have not yet been started.
  add_closures_to_fail_unstarted_pending_batches(
      elem, retry_state, GRPC_ERROR_REF(error), &closures);
  // Don't need batch_data anymore.
  batch_data_unref(batch_data);
  // Schedule all of the closures identified above.
  // Note: This will release the call combiner.
  closures.RunClosures(calld->call_combiner);
  GRPC_ERROR_UNREF(error);
}

// Intercepts recv_trailing_metadata_ready callback for retries.
// Commits the call and returns the trailing metadata up the stack.
static void recv_trailing_metadata_ready(void* arg, grpc_error* error) {
  subchannel_batch_data* batch_data = static_cast<subchannel_batch_data*>(arg);
  grpc_call_element* elem = batch_data->elem;
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: got recv_trailing_metadata_ready, error=%s",
            chand, calld, grpc_error_string(error));
  }
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          batch_data->subchannel_call->GetParentData());
  retry_state->completed_recv_trailing_metadata = true;
  // Get the call's status and check for server pushback metadata.
  grpc_status_code status = GRPC_STATUS_OK;
  grpc_mdelem* server_pushback_md = nullptr;
  grpc_metadata_batch* md_batch =
      batch_data->batch.payload->recv_trailing_metadata.recv_trailing_metadata;
  get_call_status(elem, md_batch, GRPC_ERROR_REF(error), &status,
                  &server_pushback_md);
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: call finished, status=%s", chand,
            calld, grpc_status_code_to_string(status));
  }
  // Check if we should retry.
  if (maybe_retry(elem, batch_data, status, server_pushback_md)) {
    // Unref batch_data for deferred recv_initial_metadata_ready or
    // recv_message_ready callbacks, if any.
    if (retry_state->recv_initial_metadata_ready_deferred_batch != nullptr) {
      batch_data_unref(batch_data);
      GRPC_ERROR_UNREF(retry_state->recv_initial_metadata_error);
    }
    if (retry_state->recv_message_ready_deferred_batch != nullptr) {
      batch_data_unref(batch_data);
      GRPC_ERROR_UNREF(retry_state->recv_message_error);
    }
    batch_data_unref(batch_data);
    return;
  }
  // Not retrying, so commit the call.
  retry_commit(elem, retry_state);
  // Run any necessary closures.
  run_closures_for_completed_call(batch_data, GRPC_ERROR_REF(error));
}

//
// on_complete callback handling
//

// Adds the on_complete closure for the pending batch completed in
// batch_data to closures.
static void add_closure_for_completed_pending_batch(
    grpc_call_element* elem, subchannel_batch_data* batch_data,
    subchannel_call_retry_state* retry_state, grpc_error* error,
    grpc_core::CallCombinerClosureList* closures) {
  pending_batch* pending = pending_batch_find(
      elem, "completed", [batch_data](grpc_transport_stream_op_batch* batch) {
        // Match the pending batch with the same set of send ops as the
        // subchannel batch we've just completed.
        return batch->on_complete != nullptr &&
               batch_data->batch.send_initial_metadata ==
                   batch->send_initial_metadata &&
               batch_data->batch.send_message == batch->send_message &&
               batch_data->batch.send_trailing_metadata ==
                   batch->send_trailing_metadata;
      });
  // If batch_data is a replay batch, then there will be no pending
  // batch to complete.
  if (pending == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  // Add closure.
  closures->Add(pending->batch->on_complete, error,
                "on_complete for pending batch");
  pending->batch->on_complete = nullptr;
  maybe_clear_pending_batch(elem, pending);
}

// If there are any cached ops to replay or pending ops to start on the
// subchannel call, adds a closure to closures to invoke
// start_retriable_subchannel_batches().
static void add_closures_for_replay_or_pending_send_ops(
    grpc_call_element* elem, subchannel_batch_data* batch_data,
    subchannel_call_retry_state* retry_state,
    grpc_core::CallCombinerClosureList* closures) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  bool have_pending_send_message_ops =
      retry_state->started_send_message_count < calld->send_messages.size();
  bool have_pending_send_trailing_metadata_op =
      calld->seen_send_trailing_metadata &&
      !retry_state->started_send_trailing_metadata;
  if (!have_pending_send_message_ops &&
      !have_pending_send_trailing_metadata_op) {
    for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
      pending_batch* pending = &calld->pending_batches[i];
      grpc_transport_stream_op_batch* batch = pending->batch;
      if (batch == nullptr || pending->send_ops_cached) continue;
      if (batch->send_message) have_pending_send_message_ops = true;
      if (batch->send_trailing_metadata) {
        have_pending_send_trailing_metadata_op = true;
      }
    }
  }
  if (have_pending_send_message_ops || have_pending_send_trailing_metadata_op) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: starting next batch for pending send op(s)",
              chand, calld);
    }
    GRPC_CLOSURE_INIT(&batch_data->batch.handler_private.closure,
                      start_retriable_subchannel_batches, elem,
                      grpc_schedule_on_exec_ctx);
    closures->Add(&batch_data->batch.handler_private.closure, GRPC_ERROR_NONE,
                  "starting next batch for send_* op(s)");
  }
}

// Callback used to intercept on_complete from subchannel calls.
// Called only when retries are enabled.
static void on_complete(void* arg, grpc_error* error) {
  subchannel_batch_data* batch_data = static_cast<subchannel_batch_data*>(arg);
  grpc_call_element* elem = batch_data->elem;
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_call_trace.enabled()) {
    char* batch_str = grpc_transport_stream_op_batch_string(&batch_data->batch);
    gpr_log(GPR_INFO, "chand=%p calld=%p: got on_complete, error=%s, batch=%s",
            chand, calld, grpc_error_string(error), batch_str);
    gpr_free(batch_str);
  }
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          batch_data->subchannel_call->GetParentData());
  // Update bookkeeping in retry_state.
  if (batch_data->batch.send_initial_metadata) {
    retry_state->completed_send_initial_metadata = true;
  }
  if (batch_data->batch.send_message) {
    ++retry_state->completed_send_message_count;
  }
  if (batch_data->batch.send_trailing_metadata) {
    retry_state->completed_send_trailing_metadata = true;
  }
  // If the call is committed, free cached data for send ops that we've just
  // completed.
  if (calld->retry_committed) {
    free_cached_send_op_data_for_completed_batch(elem, batch_data, retry_state);
  }
  // Construct list of closures to execute.
  grpc_core::CallCombinerClosureList closures;
  // If a retry was already dispatched, that means we saw
  // recv_trailing_metadata before this, so we do nothing here.
  // Otherwise, invoke the callback to return the result to the surface.
  if (!retry_state->retry_dispatched) {
    // Add closure for the completed pending batch, if any.
    add_closure_for_completed_pending_batch(elem, batch_data, retry_state,
                                            GRPC_ERROR_REF(error), &closures);
    // If needed, add a callback to start any replay or pending send ops on
    // the subchannel call.
    if (!retry_state->completed_recv_trailing_metadata) {
      add_closures_for_replay_or_pending_send_ops(elem, batch_data, retry_state,
                                                  &closures);
    }
  }
  // Track number of pending subchannel send batches and determine if this
  // was the last one.
  --calld->num_pending_retriable_subchannel_send_batches;
  const bool last_send_batch_complete =
      calld->num_pending_retriable_subchannel_send_batches == 0;
  // Don't need batch_data anymore.
  batch_data_unref(batch_data);
  // Schedule all of the closures identified above.
  // Note: This yeilds the call combiner.
  closures.RunClosures(calld->call_combiner);
  // If this was the last subchannel send batch, unref the call stack.
  if (last_send_batch_complete) {
    GRPC_CALL_STACK_UNREF(calld->owning_call, "subchannel_send_batches");
  }
}

//
// subchannel batch construction
//

// Helper function used to start a subchannel batch in the call combiner.
static void start_batch_in_call_combiner(void* arg, grpc_error* ignored) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  grpc_core::SubchannelCall* subchannel_call =
      static_cast<grpc_core::SubchannelCall*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  subchannel_call->StartTransportStreamOpBatch(batch);
}

// Adds a closure to closures that will execute batch in the call combiner.
static void add_closure_for_subchannel_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch,
    grpc_core::CallCombinerClosureList* closures) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  batch->handler_private.extra_arg = calld->subchannel_call.get();
  GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                    start_batch_in_call_combiner, batch,
                    grpc_schedule_on_exec_ctx);
  if (grpc_client_channel_call_trace.enabled()) {
    char* batch_str = grpc_transport_stream_op_batch_string(batch);
    gpr_log(GPR_INFO, "chand=%p calld=%p: starting subchannel batch: %s", chand,
            calld, batch_str);
    gpr_free(batch_str);
  }
  closures->Add(&batch->handler_private.closure, GRPC_ERROR_NONE,
                "start_subchannel_batch");
}

// Adds retriable send_initial_metadata op to batch_data.
static void add_retriable_send_initial_metadata_op(
    call_data* calld, subchannel_call_retry_state* retry_state,
    subchannel_batch_data* batch_data) {
  // Maps the number of retries to the corresponding metadata value slice.
  static const grpc_slice* retry_count_strings[] = {
      &GRPC_MDSTR_1, &GRPC_MDSTR_2, &GRPC_MDSTR_3, &GRPC_MDSTR_4};
  // We need to make a copy of the metadata batch for each attempt, since
  // the filters in the subchannel stack may modify this batch, and we don't
  // want those modifications to be passed forward to subsequent attempts.
  //
  // If we've already completed one or more attempts, add the
  // grpc-retry-attempts header.
  retry_state->send_initial_metadata_storage =
      static_cast<grpc_linked_mdelem*>(gpr_arena_alloc(
          calld->arena, sizeof(grpc_linked_mdelem) *
                            (calld->send_initial_metadata.list.count +
                             (calld->num_attempts_completed > 0))));
  grpc_metadata_batch_copy(&calld->send_initial_metadata,
                           &retry_state->send_initial_metadata,
                           retry_state->send_initial_metadata_storage);
  if (GPR_UNLIKELY(retry_state->send_initial_metadata.idx.named
                       .grpc_previous_rpc_attempts != nullptr)) {
    grpc_metadata_batch_remove(&retry_state->send_initial_metadata,
                               retry_state->send_initial_metadata.idx.named
                                   .grpc_previous_rpc_attempts);
  }
  if (GPR_UNLIKELY(calld->num_attempts_completed > 0)) {
    grpc_mdelem retry_md = grpc_mdelem_create(
        GRPC_MDSTR_GRPC_PREVIOUS_RPC_ATTEMPTS,
        *retry_count_strings[calld->num_attempts_completed - 1], nullptr);
    grpc_error* error = grpc_metadata_batch_add_tail(
        &retry_state->send_initial_metadata,
        &retry_state->send_initial_metadata_storage[calld->send_initial_metadata
                                                        .list.count],
        retry_md);
    if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
      gpr_log(GPR_ERROR, "error adding retry metadata: %s",
              grpc_error_string(error));
      GPR_ASSERT(false);
    }
  }
  retry_state->started_send_initial_metadata = true;
  batch_data->batch.send_initial_metadata = true;
  batch_data->batch.payload->send_initial_metadata.send_initial_metadata =
      &retry_state->send_initial_metadata;
  batch_data->batch.payload->send_initial_metadata.send_initial_metadata_flags =
      calld->send_initial_metadata_flags;
  batch_data->batch.payload->send_initial_metadata.peer_string =
      calld->peer_string;
}

// Adds retriable send_message op to batch_data.
static void add_retriable_send_message_op(
    grpc_call_element* elem, subchannel_call_retry_state* retry_state,
    subchannel_batch_data* batch_data) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting calld->send_messages[%" PRIuPTR "]",
            chand, calld, retry_state->started_send_message_count);
  }
  grpc_core::ByteStreamCache* cache =
      calld->send_messages[retry_state->started_send_message_count];
  ++retry_state->started_send_message_count;
  retry_state->send_message.Init(cache);
  batch_data->batch.send_message = true;
  batch_data->batch.payload->send_message.send_message.reset(
      retry_state->send_message.get());
}

// Adds retriable send_trailing_metadata op to batch_data.
static void add_retriable_send_trailing_metadata_op(
    call_data* calld, subchannel_call_retry_state* retry_state,
    subchannel_batch_data* batch_data) {
  // We need to make a copy of the metadata batch for each attempt, since
  // the filters in the subchannel stack may modify this batch, and we don't
  // want those modifications to be passed forward to subsequent attempts.
  retry_state->send_trailing_metadata_storage =
      static_cast<grpc_linked_mdelem*>(gpr_arena_alloc(
          calld->arena, sizeof(grpc_linked_mdelem) *
                            calld->send_trailing_metadata.list.count));
  grpc_metadata_batch_copy(&calld->send_trailing_metadata,
                           &retry_state->send_trailing_metadata,
                           retry_state->send_trailing_metadata_storage);
  retry_state->started_send_trailing_metadata = true;
  batch_data->batch.send_trailing_metadata = true;
  batch_data->batch.payload->send_trailing_metadata.send_trailing_metadata =
      &retry_state->send_trailing_metadata;
}

// Adds retriable recv_initial_metadata op to batch_data.
static void add_retriable_recv_initial_metadata_op(
    call_data* calld, subchannel_call_retry_state* retry_state,
    subchannel_batch_data* batch_data) {
  retry_state->started_recv_initial_metadata = true;
  batch_data->batch.recv_initial_metadata = true;
  grpc_metadata_batch_init(&retry_state->recv_initial_metadata);
  batch_data->batch.payload->recv_initial_metadata.recv_initial_metadata =
      &retry_state->recv_initial_metadata;
  batch_data->batch.payload->recv_initial_metadata.trailing_metadata_available =
      &retry_state->trailing_metadata_available;
  GRPC_CLOSURE_INIT(&retry_state->recv_initial_metadata_ready,
                    recv_initial_metadata_ready, batch_data,
                    grpc_schedule_on_exec_ctx);
  batch_data->batch.payload->recv_initial_metadata.recv_initial_metadata_ready =
      &retry_state->recv_initial_metadata_ready;
}

// Adds retriable recv_message op to batch_data.
static void add_retriable_recv_message_op(
    call_data* calld, subchannel_call_retry_state* retry_state,
    subchannel_batch_data* batch_data) {
  ++retry_state->started_recv_message_count;
  batch_data->batch.recv_message = true;
  batch_data->batch.payload->recv_message.recv_message =
      &retry_state->recv_message;
  GRPC_CLOSURE_INIT(&retry_state->recv_message_ready, recv_message_ready,
                    batch_data, grpc_schedule_on_exec_ctx);
  batch_data->batch.payload->recv_message.recv_message_ready =
      &retry_state->recv_message_ready;
}

// Adds retriable recv_trailing_metadata op to batch_data.
static void add_retriable_recv_trailing_metadata_op(
    call_data* calld, subchannel_call_retry_state* retry_state,
    subchannel_batch_data* batch_data) {
  retry_state->started_recv_trailing_metadata = true;
  batch_data->batch.recv_trailing_metadata = true;
  grpc_metadata_batch_init(&retry_state->recv_trailing_metadata);
  batch_data->batch.payload->recv_trailing_metadata.recv_trailing_metadata =
      &retry_state->recv_trailing_metadata;
  batch_data->batch.payload->recv_trailing_metadata.collect_stats =
      &retry_state->collect_stats;
  GRPC_CLOSURE_INIT(&retry_state->recv_trailing_metadata_ready,
                    recv_trailing_metadata_ready, batch_data,
                    grpc_schedule_on_exec_ctx);
  batch_data->batch.payload->recv_trailing_metadata
      .recv_trailing_metadata_ready =
      &retry_state->recv_trailing_metadata_ready;
  maybe_inject_recv_trailing_metadata_ready_for_lb(calld->pick.pick,
                                                   &batch_data->batch);
}

// Helper function used to start a recv_trailing_metadata batch.  This
// is used in the case where a recv_initial_metadata or recv_message
// op fails in a way that we know the call is over but when the application
// has not yet started its own recv_trailing_metadata op.
static void start_internal_recv_trailing_metadata(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: call failed but recv_trailing_metadata not "
            "started; starting it internally",
            chand, calld);
  }
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          calld->subchannel_call->GetParentData());
  // Create batch_data with 2 refs, since this batch will be unreffed twice:
  // once for the recv_trailing_metadata_ready callback when the subchannel
  // batch returns, and again when we actually get a recv_trailing_metadata
  // op from the surface.
  subchannel_batch_data* batch_data =
      batch_data_create(elem, 2, false /* set_on_complete */);
  add_retriable_recv_trailing_metadata_op(calld, retry_state, batch_data);
  retry_state->recv_trailing_metadata_internal_batch = batch_data;
  // Note: This will release the call combiner.
  calld->subchannel_call->StartTransportStreamOpBatch(&batch_data->batch);
}

// If there are any cached send ops that need to be replayed on the
// current subchannel call, creates and returns a new subchannel batch
// to replay those ops.  Otherwise, returns nullptr.
static subchannel_batch_data* maybe_create_subchannel_batch_for_replay(
    grpc_call_element* elem, subchannel_call_retry_state* retry_state) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  subchannel_batch_data* replay_batch_data = nullptr;
  // send_initial_metadata.
  if (calld->seen_send_initial_metadata &&
      !retry_state->started_send_initial_metadata &&
      !calld->pending_send_initial_metadata) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_initial_metadata op",
              chand, calld);
    }
    replay_batch_data = batch_data_create(elem, 1, true /* set_on_complete */);
    add_retriable_send_initial_metadata_op(calld, retry_state,
                                           replay_batch_data);
  }
  // send_message.
  // Note that we can only have one send_message op in flight at a time.
  if (retry_state->started_send_message_count < calld->send_messages.size() &&
      retry_state->started_send_message_count ==
          retry_state->completed_send_message_count &&
      !calld->pending_send_message) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_message op",
              chand, calld);
    }
    if (replay_batch_data == nullptr) {
      replay_batch_data =
          batch_data_create(elem, 1, true /* set_on_complete */);
    }
    add_retriable_send_message_op(elem, retry_state, replay_batch_data);
  }
  // send_trailing_metadata.
  // Note that we only add this op if we have no more send_message ops
  // to start, since we can't send down any more send_message ops after
  // send_trailing_metadata.
  if (calld->seen_send_trailing_metadata &&
      retry_state->started_send_message_count == calld->send_messages.size() &&
      !retry_state->started_send_trailing_metadata &&
      !calld->pending_send_trailing_metadata) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_trailing_metadata op",
              chand, calld);
    }
    if (replay_batch_data == nullptr) {
      replay_batch_data =
          batch_data_create(elem, 1, true /* set_on_complete */);
    }
    add_retriable_send_trailing_metadata_op(calld, retry_state,
                                            replay_batch_data);
  }
  return replay_batch_data;
}

// Adds subchannel batches for pending batches to batches, updating
// *num_batches as needed.
static void add_subchannel_batches_for_pending_batches(
    grpc_call_element* elem, subchannel_call_retry_state* retry_state,
    grpc_core::CallCombinerClosureList* closures) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    pending_batch* pending = &calld->pending_batches[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch == nullptr) continue;
    // Skip any batch that either (a) has already been started on this
    // subchannel call or (b) we can't start yet because we're still
    // replaying send ops that need to be completed first.
    // TODO(roth): Note that if any one op in the batch can't be sent
    // yet due to ops that we're replaying, we don't start any of the ops
    // in the batch.  This is probably okay, but it could conceivably
    // lead to increased latency in some cases -- e.g., we could delay
    // starting a recv op due to it being in the same batch with a send
    // op.  If/when we revamp the callback protocol in
    // transport_stream_op_batch, we may be able to fix this.
    if (batch->send_initial_metadata &&
        retry_state->started_send_initial_metadata) {
      continue;
    }
    if (batch->send_message && retry_state->completed_send_message_count <
                                   retry_state->started_send_message_count) {
      continue;
    }
    // Note that we only start send_trailing_metadata if we have no more
    // send_message ops to start, since we can't send down any more
    // send_message ops after send_trailing_metadata.
    if (batch->send_trailing_metadata &&
        (retry_state->started_send_message_count + batch->send_message <
             calld->send_messages.size() ||
         retry_state->started_send_trailing_metadata)) {
      continue;
    }
    if (batch->recv_initial_metadata &&
        retry_state->started_recv_initial_metadata) {
      continue;
    }
    if (batch->recv_message && retry_state->completed_recv_message_count <
                                   retry_state->started_recv_message_count) {
      continue;
    }
    if (batch->recv_trailing_metadata &&
        retry_state->started_recv_trailing_metadata) {
      // If we previously completed a recv_trailing_metadata op
      // initiated by start_internal_recv_trailing_metadata(), use the
      // result of that instead of trying to re-start this op.
      if (GPR_UNLIKELY((retry_state->recv_trailing_metadata_internal_batch !=
                        nullptr))) {
        // If the batch completed, then trigger the completion callback
        // directly, so that we return the previously returned results to
        // the application.  Otherwise, just unref the internally
        // started subchannel batch, since we'll propagate the
        // completion when it completes.
        if (retry_state->completed_recv_trailing_metadata) {
          // Batches containing recv_trailing_metadata always succeed.
          closures->Add(
              &retry_state->recv_trailing_metadata_ready, GRPC_ERROR_NONE,
              "re-executing recv_trailing_metadata_ready to propagate "
              "internally triggered result");
        } else {
          batch_data_unref(retry_state->recv_trailing_metadata_internal_batch);
        }
        retry_state->recv_trailing_metadata_internal_batch = nullptr;
      }
      continue;
    }
    // If we're not retrying, just send the batch as-is.
    if (calld->method_params == nullptr ||
        calld->method_params->retry_policy() == nullptr ||
        calld->retry_committed) {
      add_closure_for_subchannel_batch(elem, batch, closures);
      pending_batch_clear(calld, pending);
      continue;
    }
    // Create batch with the right number of callbacks.
    const bool has_send_ops = batch->send_initial_metadata ||
                              batch->send_message ||
                              batch->send_trailing_metadata;
    const int num_callbacks = has_send_ops + batch->recv_initial_metadata +
                              batch->recv_message +
                              batch->recv_trailing_metadata;
    subchannel_batch_data* batch_data = batch_data_create(
        elem, num_callbacks, has_send_ops /* set_on_complete */);
    // Cache send ops if needed.
    maybe_cache_send_ops_for_batch(calld, pending);
    // send_initial_metadata.
    if (batch->send_initial_metadata) {
      add_retriable_send_initial_metadata_op(calld, retry_state, batch_data);
    }
    // send_message.
    if (batch->send_message) {
      add_retriable_send_message_op(elem, retry_state, batch_data);
    }
    // send_trailing_metadata.
    if (batch->send_trailing_metadata) {
      add_retriable_send_trailing_metadata_op(calld, retry_state, batch_data);
    }
    // recv_initial_metadata.
    if (batch->recv_initial_metadata) {
      // recv_flags is only used on the server side.
      GPR_ASSERT(batch->payload->recv_initial_metadata.recv_flags == nullptr);
      add_retriable_recv_initial_metadata_op(calld, retry_state, batch_data);
    }
    // recv_message.
    if (batch->recv_message) {
      add_retriable_recv_message_op(calld, retry_state, batch_data);
    }
    // recv_trailing_metadata.
    if (batch->recv_trailing_metadata) {
      add_retriable_recv_trailing_metadata_op(calld, retry_state, batch_data);
    }
    add_closure_for_subchannel_batch(elem, &batch_data->batch, closures);
    // Track number of pending subchannel send batches.
    // If this is the first one, take a ref to the call stack.
    if (batch->send_initial_metadata || batch->send_message ||
        batch->send_trailing_metadata) {
      if (calld->num_pending_retriable_subchannel_send_batches == 0) {
        GRPC_CALL_STACK_REF(calld->owning_call, "subchannel_send_batches");
      }
      ++calld->num_pending_retriable_subchannel_send_batches;
    }
  }
}

// Constructs and starts whatever subchannel batches are needed on the
// subchannel call.
static void start_retriable_subchannel_batches(void* arg, grpc_error* ignored) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: constructing retriable batches",
            chand, calld);
  }
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          calld->subchannel_call->GetParentData());
  // Construct list of closures to execute, one for each pending batch.
  grpc_core::CallCombinerClosureList closures;
  // Replay previously-returned send_* ops if needed.
  subchannel_batch_data* replay_batch_data =
      maybe_create_subchannel_batch_for_replay(elem, retry_state);
  if (replay_batch_data != nullptr) {
    add_closure_for_subchannel_batch(elem, &replay_batch_data->batch,
                                     &closures);
    // Track number of pending subchannel send batches.
    // If this is the first one, take a ref to the call stack.
    if (calld->num_pending_retriable_subchannel_send_batches == 0) {
      GRPC_CALL_STACK_REF(calld->owning_call, "subchannel_send_batches");
    }
    ++calld->num_pending_retriable_subchannel_send_batches;
  }
  // Now add pending batches.
  add_subchannel_batches_for_pending_batches(elem, retry_state, &closures);
  // Start batches on subchannel call.
  if (grpc_client_channel_call_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting %" PRIuPTR
            " retriable batches on subchannel_call=%p",
            chand, calld, closures.size(), calld->subchannel_call.get());
  }
  // Note: This will yield the call combiner.
  closures.RunClosures(calld->call_combiner);
}

//
// LB pick
//

static void create_subchannel_call(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  const size_t parent_data_size =
      calld->enable_retries ? sizeof(subchannel_call_retry_state) : 0;
  const grpc_core::ConnectedSubchannel::CallArgs call_args = {
      calld->pollent,          // pollent
      calld->path,             // path
      calld->call_start_time,  // start_time
      calld->deadline,         // deadline
      calld->arena,            // arena
      // TODO(roth): When we implement hedging support, we will probably
      // need to use a separate call context for each subchannel call.
      calld->call_context,   // context
      calld->call_combiner,  // call_combiner
      parent_data_size       // parent_data_size
  };
  grpc_error* error = GRPC_ERROR_NONE;
  calld->subchannel_call =
      calld->pick.pick.connected_subchannel->CreateCall(call_args, &error);
  if (grpc_client_channel_routing_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: create subchannel_call=%p: error=%s",
            chand, calld, calld->subchannel_call.get(),
            grpc_error_string(error));
  }
  if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
    pending_batches_fail(elem, error, yield_call_combiner);
  } else {
    if (parent_data_size > 0) {
      new (calld->subchannel_call->GetParentData())
          subchannel_call_retry_state(calld->call_context);
    }
    pending_batches_resume(elem);
  }
}

// Invoked when a pick is completed, on both success or failure.
static void pick_done(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (error != GRPC_ERROR_NONE) {
    if (grpc_client_channel_routing_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: failed to pick subchannel: error=%s", chand,
              calld, grpc_error_string(error));
    }
    pending_batches_fail(elem, GRPC_ERROR_REF(error), yield_call_combiner);
    return;
  }
  create_subchannel_call(elem);
}

namespace grpc_core {
namespace {

// A class to handle the call combiner cancellation callback for a
// queued pick.
class QueuedPickCanceller {
 public:
  explicit QueuedPickCanceller(grpc_call_element* elem) : elem_(elem) {
    auto* calld = static_cast<call_data*>(elem->call_data);
    auto* chand = static_cast<channel_data*>(elem->channel_data);
    GRPC_CALL_STACK_REF(calld->owning_call, "QueuedPickCanceller");
    GRPC_CLOSURE_INIT(&closure_, &CancelLocked, this,
                      grpc_combiner_scheduler(chand->combiner));
    grpc_call_combiner_set_notify_on_cancel(calld->call_combiner, &closure_);
  }

 private:
  static void CancelLocked(void* arg, grpc_error* error) {
    auto* self = static_cast<QueuedPickCanceller*>(arg);
    auto* chand = static_cast<channel_data*>(self->elem_->channel_data);
    auto* calld = static_cast<call_data*>(self->elem_->call_data);
    if (grpc_client_channel_routing_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: cancelling queued pick: "
              "error=%s self=%p calld->pick_canceller=%p",
              chand, calld, grpc_error_string(error), self,
              calld->pick_canceller);
    }
    if (calld->pick_canceller == self && error != GRPC_ERROR_NONE) {
      // Remove pick from list of queued picks.
      remove_call_from_queued_picks_locked(self->elem_);
      // Fail pending batches on the call.
      pending_batches_fail(self->elem_, GRPC_ERROR_REF(error),
                           yield_call_combiner_if_pending_batches_found);
    }
    GRPC_CALL_STACK_UNREF(calld->owning_call, "QueuedPickCanceller");
    Delete(self);
  }

  grpc_call_element* elem_;
  grpc_closure closure_;
};

}  // namespace
}  // namespace grpc_core

// Removes the call from the channel's list of queued picks.
static void remove_call_from_queued_picks_locked(grpc_call_element* elem) {
  auto* chand = static_cast<channel_data*>(elem->channel_data);
  auto* calld = static_cast<call_data*>(elem->call_data);
  for (QueuedPick** pick = &chand->queued_picks; *pick != nullptr;
       pick = &(*pick)->next) {
    if (*pick == &calld->pick) {
      if (grpc_client_channel_routing_trace.enabled()) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: removing from queued picks list",
                chand, calld);
      }
      calld->pick_queued = false;
      *pick = calld->pick.next;
      // Remove call's pollent from channel's interested_parties.
      grpc_polling_entity_del_from_pollset_set(calld->pollent,
                                               chand->interested_parties);
      // Lame the call combiner canceller.
      calld->pick_canceller = nullptr;
      break;
    }
  }
}

// Adds the call to the channel's list of queued picks.
static void add_call_to_queued_picks_locked(grpc_call_element* elem) {
  auto* chand = static_cast<channel_data*>(elem->channel_data);
  auto* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_routing_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: adding to queued picks list", chand,
            calld);
  }
  calld->pick_queued = true;
  // Add call to queued picks list.
  calld->pick.elem = elem;
  calld->pick.next = chand->queued_picks;
  chand->queued_picks = &calld->pick;
  // Add call's pollent to channel's interested_parties, so that I/O
  // can be done under the call's CQ.
  grpc_polling_entity_add_to_pollset_set(calld->pollent,
                                         chand->interested_parties);
  // Register call combiner cancellation callback.
  calld->pick_canceller = grpc_core::New<grpc_core::QueuedPickCanceller>(elem);
}

// Applies service config to the call.  Must be invoked once we know
// that the resolver has returned results to the channel.
static void apply_service_config_to_call_locked(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_routing_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: applying service config to call",
            chand, calld);
  }
  if (chand->retry_throttle_data != nullptr) {
    calld->retry_throttle_data = chand->retry_throttle_data->Ref();
  }
  if (chand->method_params_table != nullptr) {
    calld->method_params = grpc_core::ServiceConfig::MethodConfigTableLookup(
        *chand->method_params_table, calld->path);
    if (calld->method_params != nullptr) {
      // If the deadline from the service config is shorter than the one
      // from the client API, reset the deadline timer.
      if (chand->deadline_checking_enabled &&
          calld->method_params->timeout() != 0) {
        const grpc_millis per_method_deadline =
            grpc_timespec_to_millis_round_up(calld->call_start_time) +
            calld->method_params->timeout();
        if (per_method_deadline < calld->deadline) {
          calld->deadline = per_method_deadline;
          grpc_deadline_state_reset(elem, calld->deadline);
        }
      }
      // If the service config set wait_for_ready and the application
      // did not explicitly set it, use the value from the service config.
      uint32_t* send_initial_metadata_flags =
          &calld->pending_batches[0]
               .batch->payload->send_initial_metadata
               .send_initial_metadata_flags;
      if (GPR_UNLIKELY(
              calld->method_params->wait_for_ready() !=
                  ClientChannelMethodParams::WAIT_FOR_READY_UNSET &&
              !(*send_initial_metadata_flags &
                GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET))) {
        if (calld->method_params->wait_for_ready() ==
            ClientChannelMethodParams::WAIT_FOR_READY_TRUE) {
          *send_initial_metadata_flags |= GRPC_INITIAL_METADATA_WAIT_FOR_READY;
        } else {
          *send_initial_metadata_flags &= ~GRPC_INITIAL_METADATA_WAIT_FOR_READY;
        }
      }
    }
  }
  // If no retry policy, disable retries.
  // TODO(roth): Remove this when adding support for transparent retries.
  if (calld->method_params == nullptr ||
      calld->method_params->retry_policy() == nullptr) {
    calld->enable_retries = false;
  }
}

// Invoked once resolver results are available.
static void maybe_apply_service_config_to_call_locked(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  // Apply service config data to the call only once, and only if the
  // channel has the data available.
  if (GPR_LIKELY(chand->have_service_config &&
                 !calld->service_config_applied)) {
    calld->service_config_applied = true;
    apply_service_config_to_call_locked(elem);
  }
}

static const char* pick_result_name(LoadBalancingPolicy::PickResult result) {
  switch (result) {
    case LoadBalancingPolicy::PICK_COMPLETE:
      return "COMPLETE";
    case LoadBalancingPolicy::PICK_QUEUE:
      return "QUEUE";
    case LoadBalancingPolicy::PICK_TRANSIENT_FAILURE:
      return "TRANSIENT_FAILURE";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

static void start_pick_locked(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  GPR_ASSERT(calld->pick.pick.connected_subchannel == nullptr);
  GPR_ASSERT(calld->subchannel_call == nullptr);
  // If this is a retry, use the send_initial_metadata payload that
  // we've cached; otherwise, use the pending batch.  The
  // send_initial_metadata batch will be the first pending batch in the
  // list, as set by get_batch_index() above.
  // TODO(roth): What if the LB policy needs to add something to the
  // call's initial metadata, and then there's a retry?  We don't want
  // the new metadata to be added twice.  We might need to somehow
  // allocate the subchannel batch earlier so that we can give the
  // subchannel's copy of the metadata batch (which is copied for each
  // attempt) to the LB policy instead the one from the parent channel.
  calld->pick.pick.initial_metadata =
      calld->seen_send_initial_metadata
          ? &calld->send_initial_metadata
          : calld->pending_batches[0]
                .batch->payload->send_initial_metadata.send_initial_metadata;
  uint32_t* send_initial_metadata_flags =
      calld->seen_send_initial_metadata
          ? &calld->send_initial_metadata_flags
          : &calld->pending_batches[0]
                 .batch->payload->send_initial_metadata
                 .send_initial_metadata_flags;
  // Apply service config to call if needed.
  maybe_apply_service_config_to_call_locked(elem);
  // When done, we schedule this closure to leave the channel combiner.
  GRPC_CLOSURE_INIT(&calld->pick_closure, pick_done, elem,
                    grpc_schedule_on_exec_ctx);
  // Attempt pick.
  error = GRPC_ERROR_NONE;
  auto pick_result = chand->picker->Pick(&calld->pick.pick, &error);
  if (grpc_client_channel_routing_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: LB pick returned %s (connected_subchannel=%p, "
            "error=%s)",
            chand, calld, pick_result_name(pick_result),
            calld->pick.pick.connected_subchannel.get(),
            grpc_error_string(error));
  }
  switch (pick_result) {
    case LoadBalancingPolicy::PICK_TRANSIENT_FAILURE:
      // If we're shutting down, fail all RPCs.
      if (chand->disconnect_error != GRPC_ERROR_NONE) {
        GRPC_ERROR_UNREF(error);
        GRPC_CLOSURE_SCHED(&calld->pick_closure,
                           GRPC_ERROR_REF(chand->disconnect_error));
        break;
      }
      // If wait_for_ready is false, then the error indicates the RPC
      // attempt's final status.
      if ((*send_initial_metadata_flags &
           GRPC_INITIAL_METADATA_WAIT_FOR_READY) == 0) {
        // Retry if appropriate; otherwise, fail.
        grpc_status_code status = GRPC_STATUS_OK;
        grpc_error_get_status(error, calld->deadline, &status, nullptr, nullptr,
                              nullptr);
        if (!calld->enable_retries ||
            !maybe_retry(elem, nullptr /* batch_data */, status,
                         nullptr /* server_pushback_md */)) {
          grpc_error* new_error =
              GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                  "Failed to create subchannel", &error, 1);
          GRPC_ERROR_UNREF(error);
          GRPC_CLOSURE_SCHED(&calld->pick_closure, new_error);
        }
        if (calld->pick_queued) remove_call_from_queued_picks_locked(elem);
        break;
      }
      // If wait_for_ready is true, then queue to retry when we get a new
      // picker.
      GRPC_ERROR_UNREF(error);
      // Fallthrough
    case LoadBalancingPolicy::PICK_QUEUE:
      if (!calld->pick_queued) add_call_to_queued_picks_locked(elem);
      break;
    default:  // PICK_COMPLETE
      // Handle drops.
      if (GPR_UNLIKELY(calld->pick.pick.connected_subchannel == nullptr)) {
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Call dropped by load balancing policy");
      }
      GRPC_CLOSURE_SCHED(&calld->pick_closure, error);
      if (calld->pick_queued) remove_call_from_queued_picks_locked(elem);
  }
}

//
// filter call vtable functions
//

static void cc_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_SCOPE("cc_start_transport_stream_op_batch", 0);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (GPR_LIKELY(chand->deadline_checking_enabled)) {
    grpc_deadline_state_client_start_transport_stream_op_batch(elem, batch);
  }
  // If we've previously been cancelled, immediately fail any new batches.
  if (GPR_UNLIKELY(calld->cancel_error != GRPC_ERROR_NONE)) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: failing batch with error: %s",
              chand, calld, grpc_error_string(calld->cancel_error));
    }
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(calld->cancel_error), calld->call_combiner);
    return;
  }
  // Handle cancellation.
  if (GPR_UNLIKELY(batch->cancel_stream)) {
    // Stash a copy of cancel_error in our call data, so that we can use
    // it for subsequent operations.  This ensures that if the call is
    // cancelled before any batches are passed down (e.g., if the deadline
    // is in the past when the call starts), we can return the right
    // error to the caller when the first batch does get passed down.
    GRPC_ERROR_UNREF(calld->cancel_error);
    calld->cancel_error =
        GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: recording cancel_error=%s", chand,
              calld, grpc_error_string(calld->cancel_error));
    }
    // If we do not have a subchannel call (i.e., a pick has not yet
    // been started), fail all pending batches.  Otherwise, send the
    // cancellation down to the subchannel call.
    if (calld->subchannel_call == nullptr) {
      // TODO(roth): If there is a pending retry callback, do we need to
      // cancel it here?
      pending_batches_fail(elem, GRPC_ERROR_REF(calld->cancel_error),
                           no_yield_call_combiner);
      // Note: This will release the call combiner.
      grpc_transport_stream_op_batch_finish_with_failure(
          batch, GRPC_ERROR_REF(calld->cancel_error), calld->call_combiner);
    } else {
      // Note: This will release the call combiner.
      calld->subchannel_call->StartTransportStreamOpBatch(batch);
    }
    return;
  }
  // Add the batch to the pending list.
  pending_batches_add(elem, batch);
  // Check if we've already gotten a subchannel call.
  // Note that once we have completed the pick, we do not need to enter
  // the channel combiner, which is more efficient (especially for
  // streaming calls).
  if (calld->subchannel_call != nullptr) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: starting batch on subchannel_call=%p", chand,
              calld, calld->subchannel_call.get());
    }
    pending_batches_resume(elem);
    return;
  }
  // We do not yet have a subchannel call.
  // For batches containing a send_initial_metadata op, enter the channel
  // combiner to start a pick.
  if (GPR_LIKELY(batch->send_initial_metadata)) {
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: entering client_channel combiner",
              chand, calld);
    }
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_INIT(&batch->handler_private.closure, start_pick_locked,
                          elem, grpc_combiner_scheduler(chand->combiner)),
        GRPC_ERROR_NONE);
  } else {
    // For all other batches, release the call combiner.
    if (grpc_client_channel_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: saved batch, yielding call combiner", chand,
              calld);
    }
    GRPC_CALL_COMBINER_STOP(calld->call_combiner,
                            "batch does not include send_initial_metadata");
  }
}

/* Constructor for call_data */
static grpc_error* cc_init_call_elem(grpc_call_element* elem,
                                     const grpc_call_element_args* args) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  new (elem->call_data) call_data(elem, *chand, *args);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void cc_destroy_call_elem(grpc_call_element* elem,
                                 const grpc_call_final_info* final_info,
                                 grpc_closure* then_schedule_closure) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (GPR_LIKELY(calld->subchannel_call != nullptr)) {
    calld->subchannel_call->SetAfterCallStackDestroy(then_schedule_closure);
    then_schedule_closure = nullptr;
  }
  calld->~call_data();
  GRPC_CLOSURE_SCHED(then_schedule_closure, GRPC_ERROR_NONE);
}

static void cc_set_pollset_or_pollset_set(grpc_call_element* elem,
                                          grpc_polling_entity* pollent) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->pollent = pollent;
}

/*************************************************************************
 * EXPORTED SYMBOLS
 */

const grpc_channel_filter grpc_client_channel_filter = {
    cc_start_transport_stream_op_batch,
    cc_start_transport_op,
    sizeof(call_data),
    cc_init_call_elem,
    cc_set_pollset_or_pollset_set,
    cc_destroy_call_elem,
    sizeof(channel_data),
    cc_init_channel_elem,
    cc_destroy_channel_elem,
    cc_get_channel_info,
    "client-channel",
};

void grpc_client_channel_set_channelz_node(
    grpc_channel_element* elem, grpc_core::channelz::ClientChannelNode* node) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->channelz_node = node;
  chand->resolving_lb_policy->set_channelz_node(node->Ref());
}

void grpc_client_channel_populate_child_refs(
    grpc_channel_element* elem,
    grpc_core::channelz::ChildRefsList* child_subchannels,
    grpc_core::channelz::ChildRefsList* child_channels) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (chand->resolving_lb_policy != nullptr) {
    chand->resolving_lb_policy->FillChildRefsForChannelz(child_subchannels,
                                                         child_channels);
  }
}

static void try_to_connect_locked(void* arg, grpc_error* error_ignored) {
  channel_data* chand = static_cast<channel_data*>(arg);
  chand->resolving_lb_policy->ExitIdleLocked();
  GRPC_CHANNEL_STACK_UNREF(chand->owning_stack, "try_to_connect");
}

grpc_connectivity_state grpc_client_channel_check_connectivity_state(
    grpc_channel_element* elem, int try_to_connect) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_connectivity_state out =
      grpc_connectivity_state_check(&chand->state_tracker);
  if (out == GRPC_CHANNEL_IDLE && try_to_connect) {
    GRPC_CHANNEL_STACK_REF(chand->owning_stack, "try_to_connect");
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_CREATE(try_to_connect_locked, chand,
                            grpc_combiner_scheduler(chand->combiner)),
        GRPC_ERROR_NONE);
  }
  return out;
}

typedef struct external_connectivity_watcher {
  channel_data* chand;
  grpc_polling_entity pollent;
  grpc_closure* on_complete;
  grpc_closure* watcher_timer_init;
  grpc_connectivity_state* state;
  grpc_closure my_closure;
  struct external_connectivity_watcher* next;
} external_connectivity_watcher;

static external_connectivity_watcher* lookup_external_connectivity_watcher(
    channel_data* chand, grpc_closure* on_complete) {
  gpr_mu_lock(&chand->external_connectivity_watcher_list_mu);
  external_connectivity_watcher* w =
      chand->external_connectivity_watcher_list_head;
  while (w != nullptr && w->on_complete != on_complete) {
    w = w->next;
  }
  gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);
  return w;
}

static void external_connectivity_watcher_list_append(
    channel_data* chand, external_connectivity_watcher* w) {
  GPR_ASSERT(!lookup_external_connectivity_watcher(chand, w->on_complete));

  gpr_mu_lock(&w->chand->external_connectivity_watcher_list_mu);
  GPR_ASSERT(!w->next);
  w->next = chand->external_connectivity_watcher_list_head;
  chand->external_connectivity_watcher_list_head = w;
  gpr_mu_unlock(&w->chand->external_connectivity_watcher_list_mu);
}

static void external_connectivity_watcher_list_remove(
    channel_data* chand, external_connectivity_watcher* to_remove) {
  GPR_ASSERT(
      lookup_external_connectivity_watcher(chand, to_remove->on_complete));
  gpr_mu_lock(&chand->external_connectivity_watcher_list_mu);
  if (to_remove == chand->external_connectivity_watcher_list_head) {
    chand->external_connectivity_watcher_list_head = to_remove->next;
    gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);
    return;
  }
  external_connectivity_watcher* w =
      chand->external_connectivity_watcher_list_head;
  while (w != nullptr) {
    if (w->next == to_remove) {
      w->next = w->next->next;
      gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);
      return;
    }
    w = w->next;
  }
  GPR_UNREACHABLE_CODE(return );
}

int grpc_client_channel_num_external_connectivity_watchers(
    grpc_channel_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  int count = 0;

  gpr_mu_lock(&chand->external_connectivity_watcher_list_mu);
  external_connectivity_watcher* w =
      chand->external_connectivity_watcher_list_head;
  while (w != nullptr) {
    count++;
    w = w->next;
  }
  gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);

  return count;
}

static void on_external_watch_complete_locked(void* arg, grpc_error* error) {
  external_connectivity_watcher* w =
      static_cast<external_connectivity_watcher*>(arg);
  grpc_closure* follow_up = w->on_complete;
  grpc_polling_entity_del_from_pollset_set(&w->pollent,
                                           w->chand->interested_parties);
  GRPC_CHANNEL_STACK_UNREF(w->chand->owning_stack,
                           "external_connectivity_watcher");
  external_connectivity_watcher_list_remove(w->chand, w);
  gpr_free(w);
  GRPC_CLOSURE_SCHED(follow_up, GRPC_ERROR_REF(error));
}

static void watch_connectivity_state_locked(void* arg,
                                            grpc_error* error_ignored) {
  external_connectivity_watcher* w =
      static_cast<external_connectivity_watcher*>(arg);
  external_connectivity_watcher* found = nullptr;
  if (w->state != nullptr) {
    external_connectivity_watcher_list_append(w->chand, w);
    // An assumption is being made that the closure is scheduled on the exec ctx
    // scheduler and that GRPC_CLOSURE_RUN would run the closure immediately.
    GRPC_CLOSURE_RUN(w->watcher_timer_init, GRPC_ERROR_NONE);
    GRPC_CLOSURE_INIT(&w->my_closure, on_external_watch_complete_locked, w,
                      grpc_combiner_scheduler(w->chand->combiner));
    grpc_connectivity_state_notify_on_state_change(&w->chand->state_tracker,
                                                   w->state, &w->my_closure);
  } else {
    GPR_ASSERT(w->watcher_timer_init == nullptr);
    found = lookup_external_connectivity_watcher(w->chand, w->on_complete);
    if (found) {
      GPR_ASSERT(found->on_complete == w->on_complete);
      grpc_connectivity_state_notify_on_state_change(
          &found->chand->state_tracker, nullptr, &found->my_closure);
    }
    grpc_polling_entity_del_from_pollset_set(&w->pollent,
                                             w->chand->interested_parties);
    GRPC_CHANNEL_STACK_UNREF(w->chand->owning_stack,
                             "external_connectivity_watcher");
    gpr_free(w);
  }
}

void grpc_client_channel_watch_connectivity_state(
    grpc_channel_element* elem, grpc_polling_entity pollent,
    grpc_connectivity_state* state, grpc_closure* closure,
    grpc_closure* watcher_timer_init) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  external_connectivity_watcher* w =
      static_cast<external_connectivity_watcher*>(gpr_zalloc(sizeof(*w)));
  w->chand = chand;
  w->pollent = pollent;
  w->on_complete = closure;
  w->state = state;
  w->watcher_timer_init = watcher_timer_init;
  grpc_polling_entity_add_to_pollset_set(&w->pollent,
                                         chand->interested_parties);
  GRPC_CHANNEL_STACK_REF(w->chand->owning_stack,
                         "external_connectivity_watcher");
  GRPC_CLOSURE_SCHED(
      GRPC_CLOSURE_INIT(&w->my_closure, watch_connectivity_state_locked, w,
                        grpc_combiner_scheduler(chand->combiner)),
      GRPC_ERROR_NONE);
}

grpc_core::RefCountedPtr<grpc_core::SubchannelCall>
grpc_client_channel_get_subchannel_call(grpc_call_element* elem) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  return calld->subchannel_call;
}
