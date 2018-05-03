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
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/method_params.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
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
#include "src/core/lib/transport/service_config.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_metadata.h"

using grpc_core::internal::ClientChannelMethodParams;
using grpc_core::internal::ServerRetryThrottleData;

/* Client channel implementation */

// By default, we buffer 256 KiB per RPC for retries.
// TODO(roth): Do we have any data to suggest a better value?
#define DEFAULT_PER_RPC_RETRY_BUFFER_SIZE (256 << 10)

// This value was picked arbitrarily.  It can be changed if there is
// any even moderately compelling reason to do so.
#define RETRY_BACKOFF_JITTER 0.2

grpc_core::TraceFlag grpc_client_channel_trace(false, "client_channel");

/*************************************************************************
 * CHANNEL-WIDE FUNCTIONS
 */

struct external_connectivity_watcher;

typedef grpc_core::SliceHashTable<
    grpc_core::RefCountedPtr<ClientChannelMethodParams>>
    MethodParamsTable;

typedef struct client_channel_channel_data {
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver;
  bool started_resolving;
  bool deadline_checking_enabled;
  grpc_client_channel_factory* client_channel_factory;
  bool enable_retries;
  size_t per_rpc_retry_buffer_size;

  /** combiner protecting all variables below in this data structure */
  grpc_combiner* combiner;
  /** currently active load balancer */
  grpc_core::OrphanablePtr<grpc_core::LoadBalancingPolicy> lb_policy;
  /** retry throttle data */
  grpc_core::RefCountedPtr<ServerRetryThrottleData> retry_throttle_data;
  /** maps method names to method_parameters structs */
  grpc_core::RefCountedPtr<MethodParamsTable> method_params_table;
  /** incoming resolver result - set by resolver.next() */
  grpc_channel_args* resolver_result;
  /** a list of closures that are all waiting for resolver result to come in */
  grpc_closure_list waiting_for_resolver_result_closures;
  /** resolver callback */
  grpc_closure on_resolver_result_changed;
  /** connectivity state being tracked */
  grpc_connectivity_state_tracker state_tracker;
  /** when an lb_policy arrives, should we try to exit idle */
  bool exit_idle_when_lb_policy_arrives;
  /** owning stack */
  grpc_channel_stack* owning_stack;
  /** interested parties (owned) */
  grpc_pollset_set* interested_parties;

  /* external_connectivity_watcher_list head is guarded by its own mutex, since
   * counts need to be grabbed immediately without polling on a cq */
  gpr_mu external_connectivity_watcher_list_mu;
  struct external_connectivity_watcher* external_connectivity_watcher_list_head;

  /* the following properties are guarded by a mutex since APIs require them
     to be instantaneously available */
  gpr_mu info_mu;
  char* info_lb_policy_name;
  /** service config in JSON form */
  char* info_service_config_json;
} channel_data;

typedef struct {
  channel_data* chand;
  /** used as an identifier, don't dereference it because the LB policy may be
   * non-existing when the callback is run */
  grpc_core::LoadBalancingPolicy* lb_policy;
  grpc_closure closure;
} reresolution_request_args;

/** We create one watcher for each new lb_policy that is returned from a
    resolver, to watch for state changes from the lb_policy. When a state
    change is seen, we update the channel, and create a new watcher. */
typedef struct {
  channel_data* chand;
  grpc_closure on_changed;
  grpc_connectivity_state state;
  grpc_core::LoadBalancingPolicy* lb_policy;
} lb_policy_connectivity_watcher;

static void watch_lb_policy_locked(channel_data* chand,
                                   grpc_core::LoadBalancingPolicy* lb_policy,
                                   grpc_connectivity_state current_state);

static void set_channel_connectivity_state_locked(channel_data* chand,
                                                  grpc_connectivity_state state,
                                                  grpc_error* error,
                                                  const char* reason) {
  /* TODO: Improve failure handling:
   * - Make it possible for policies to return GRPC_CHANNEL_TRANSIENT_FAILURE.
   * - Hand over pending picks from old policies during the switch that happens
   *   when resolver provides an update. */
  if (chand->lb_policy != nullptr) {
    if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      /* cancel picks with wait_for_ready=false */
      chand->lb_policy->CancelMatchingPicksLocked(
          /* mask= */ GRPC_INITIAL_METADATA_WAIT_FOR_READY,
          /* check= */ 0, GRPC_ERROR_REF(error));
    } else if (state == GRPC_CHANNEL_SHUTDOWN) {
      /* cancel all picks */
      chand->lb_policy->CancelMatchingPicksLocked(/* mask= */ 0, /* check= */ 0,
                                                  GRPC_ERROR_REF(error));
    }
  }
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p: setting connectivity state to %s", chand,
            grpc_connectivity_state_name(state));
  }
  grpc_connectivity_state_set(&chand->state_tracker, state, error, reason);
}

static void on_lb_policy_state_changed_locked(void* arg, grpc_error* error) {
  lb_policy_connectivity_watcher* w =
      static_cast<lb_policy_connectivity_watcher*>(arg);
  /* check if the notification is for the latest policy */
  if (w->lb_policy == w->chand->lb_policy.get()) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p: lb_policy=%p state changed to %s", w->chand,
              w->lb_policy, grpc_connectivity_state_name(w->state));
    }
    set_channel_connectivity_state_locked(w->chand, w->state,
                                          GRPC_ERROR_REF(error), "lb_changed");
    if (w->state != GRPC_CHANNEL_SHUTDOWN) {
      watch_lb_policy_locked(w->chand, w->lb_policy, w->state);
    }
  }
  GRPC_CHANNEL_STACK_UNREF(w->chand->owning_stack, "watch_lb_policy");
  gpr_free(w);
}

static void watch_lb_policy_locked(channel_data* chand,
                                   grpc_core::LoadBalancingPolicy* lb_policy,
                                   grpc_connectivity_state current_state) {
  lb_policy_connectivity_watcher* w =
      static_cast<lb_policy_connectivity_watcher*>(gpr_malloc(sizeof(*w)));
  GRPC_CHANNEL_STACK_REF(chand->owning_stack, "watch_lb_policy");
  w->chand = chand;
  GRPC_CLOSURE_INIT(&w->on_changed, on_lb_policy_state_changed_locked, w,
                    grpc_combiner_scheduler(chand->combiner));
  w->state = current_state;
  w->lb_policy = lb_policy;
  lb_policy->NotifyOnStateChangeLocked(&w->state, &w->on_changed);
}

static void start_resolving_locked(channel_data* chand) {
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p: starting name resolution", chand);
  }
  GPR_ASSERT(!chand->started_resolving);
  chand->started_resolving = true;
  GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
  chand->resolver->NextLocked(&chand->resolver_result,
                              &chand->on_resolver_result_changed);
}

typedef struct {
  char* server_name;
  grpc_core::RefCountedPtr<ServerRetryThrottleData> retry_throttle_data;
} service_config_parsing_state;

static void parse_retry_throttle_params(
    const grpc_json* field, service_config_parsing_state* parsing_state) {
  if (strcmp(field->key, "retryThrottling") == 0) {
    if (parsing_state->retry_throttle_data != nullptr) return;  // Duplicate.
    if (field->type != GRPC_JSON_OBJECT) return;
    int max_milli_tokens = 0;
    int milli_token_ratio = 0;
    for (grpc_json* sub_field = field->child; sub_field != nullptr;
         sub_field = sub_field->next) {
      if (sub_field->key == nullptr) return;
      if (strcmp(sub_field->key, "maxTokens") == 0) {
        if (max_milli_tokens != 0) return;  // Duplicate.
        if (sub_field->type != GRPC_JSON_NUMBER) return;
        max_milli_tokens = gpr_parse_nonnegative_int(sub_field->value);
        if (max_milli_tokens == -1) return;
        max_milli_tokens *= 1000;
      } else if (strcmp(sub_field->key, "tokenRatio") == 0) {
        if (milli_token_ratio != 0) return;  // Duplicate.
        if (sub_field->type != GRPC_JSON_NUMBER) return;
        // We support up to 3 decimal digits.
        size_t whole_len = strlen(sub_field->value);
        uint32_t multiplier = 1;
        uint32_t decimal_value = 0;
        const char* decimal_point = strchr(sub_field->value, '.');
        if (decimal_point != nullptr) {
          whole_len = static_cast<size_t>(decimal_point - sub_field->value);
          multiplier = 1000;
          size_t decimal_len = strlen(decimal_point + 1);
          if (decimal_len > 3) decimal_len = 3;
          if (!gpr_parse_bytes_to_uint32(decimal_point + 1, decimal_len,
                                         &decimal_value)) {
            return;
          }
          uint32_t decimal_multiplier = 1;
          for (size_t i = 0; i < (3 - decimal_len); ++i) {
            decimal_multiplier *= 10;
          }
          decimal_value *= decimal_multiplier;
        }
        uint32_t whole_value;
        if (!gpr_parse_bytes_to_uint32(sub_field->value, whole_len,
                                       &whole_value)) {
          return;
        }
        milli_token_ratio =
            static_cast<int>((whole_value * multiplier) + decimal_value);
        if (milli_token_ratio <= 0) return;
      }
    }
    parsing_state->retry_throttle_data =
        grpc_core::internal::ServerRetryThrottleMap::GetDataForServer(
            parsing_state->server_name, max_milli_tokens, milli_token_ratio);
  }
}

static void request_reresolution_locked(void* arg, grpc_error* error) {
  reresolution_request_args* args =
      static_cast<reresolution_request_args*>(arg);
  channel_data* chand = args->chand;
  // If this invocation is for a stale LB policy, treat it as an LB shutdown
  // signal.
  if (args->lb_policy != chand->lb_policy.get() || error != GRPC_ERROR_NONE ||
      chand->resolver == nullptr) {
    GRPC_CHANNEL_STACK_UNREF(chand->owning_stack, "re-resolution");
    gpr_free(args);
    return;
  }
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p: started name re-resolving", chand);
  }
  chand->resolver->RequestReresolutionLocked();
  // Give back the closure to the LB policy.
  chand->lb_policy->SetReresolutionClosureLocked(&args->closure);
}

// TODO(roth): The logic in this function is very hard to follow.  We
// should refactor this so that it's easier to understand, perhaps as
// part of changing the resolver API to more clearly differentiate
// between transient failures and shutdown.
static void on_resolver_result_changed_locked(void* arg, grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(arg);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p: got resolver result: resolver_result=%p error=%s", chand,
            chand->resolver_result, grpc_error_string(error));
  }
  // Extract the following fields from the resolver result, if non-nullptr.
  bool lb_policy_updated = false;
  bool lb_policy_created = false;
  char* lb_policy_name_dup = nullptr;
  bool lb_policy_name_changed = false;
  grpc_core::OrphanablePtr<grpc_core::LoadBalancingPolicy> new_lb_policy;
  char* service_config_json = nullptr;
  grpc_core::RefCountedPtr<ServerRetryThrottleData> retry_throttle_data;
  grpc_core::RefCountedPtr<MethodParamsTable> method_params_table;
  if (chand->resolver_result != nullptr) {
    if (chand->resolver != nullptr) {
      // Find LB policy name.
      const grpc_arg* channel_arg = grpc_channel_args_find(
          chand->resolver_result, GRPC_ARG_LB_POLICY_NAME);
      const char* lb_policy_name = grpc_channel_arg_get_string(channel_arg);
      // Special case: If at least one balancer address is present, we use
      // the grpclb policy, regardless of what the resolver actually specified.
      channel_arg =
          grpc_channel_args_find(chand->resolver_result, GRPC_ARG_LB_ADDRESSES);
      if (channel_arg != nullptr && channel_arg->type == GRPC_ARG_POINTER) {
        grpc_lb_addresses* addresses =
            static_cast<grpc_lb_addresses*>(channel_arg->value.pointer.p);
        bool found_balancer_address = false;
        for (size_t i = 0; i < addresses->num_addresses; ++i) {
          if (addresses->addresses[i].is_balancer) {
            found_balancer_address = true;
            break;
          }
        }
        if (found_balancer_address) {
          if (lb_policy_name != nullptr &&
              strcmp(lb_policy_name, "grpclb") != 0) {
            gpr_log(GPR_INFO,
                    "resolver requested LB policy %s but provided at least one "
                    "balancer address -- forcing use of grpclb LB policy",
                    lb_policy_name);
          }
          lb_policy_name = "grpclb";
        }
      }
      // Use pick_first if nothing was specified and we didn't select grpclb
      // above.
      if (lb_policy_name == nullptr) lb_policy_name = "pick_first";
      // Check to see if we're already using the right LB policy.
      // Note: It's safe to use chand->info_lb_policy_name here without
      // taking a lock on chand->info_mu, because this function is the
      // only thing that modifies its value, and it can only be invoked
      // once at any given time.
      lb_policy_name_changed =
          chand->info_lb_policy_name == nullptr ||
          gpr_stricmp(chand->info_lb_policy_name, lb_policy_name) != 0;
      if (chand->lb_policy != nullptr && !lb_policy_name_changed) {
        // Continue using the same LB policy.  Update with new addresses.
        lb_policy_updated = true;
        chand->lb_policy->UpdateLocked(*chand->resolver_result);
      } else {
        // Instantiate new LB policy.
        grpc_core::LoadBalancingPolicy::Args lb_policy_args;
        lb_policy_args.combiner = chand->combiner;
        lb_policy_args.client_channel_factory = chand->client_channel_factory;
        lb_policy_args.args = chand->resolver_result;
        new_lb_policy =
            grpc_core::LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
                lb_policy_name, lb_policy_args);
        if (new_lb_policy == nullptr) {
          gpr_log(GPR_ERROR, "could not create LB policy \"%s\"",
                  lb_policy_name);
        } else {
          lb_policy_created = true;
          reresolution_request_args* args =
              static_cast<reresolution_request_args*>(
                  gpr_zalloc(sizeof(*args)));
          args->chand = chand;
          args->lb_policy = new_lb_policy.get();
          GRPC_CLOSURE_INIT(&args->closure, request_reresolution_locked, args,
                            grpc_combiner_scheduler(chand->combiner));
          GRPC_CHANNEL_STACK_REF(chand->owning_stack, "re-resolution");
          new_lb_policy->SetReresolutionClosureLocked(&args->closure);
        }
      }
      // Before we clean up, save a copy of lb_policy_name, since it might
      // be pointing to data inside chand->resolver_result.
      // The copy will be saved in chand->lb_policy_name below.
      lb_policy_name_dup = gpr_strdup(lb_policy_name);
      // Find service config.
      channel_arg = grpc_channel_args_find(chand->resolver_result,
                                           GRPC_ARG_SERVICE_CONFIG);
      service_config_json =
          gpr_strdup(grpc_channel_arg_get_string(channel_arg));
      if (service_config_json != nullptr) {
        grpc_core::UniquePtr<grpc_core::ServiceConfig> service_config =
            grpc_core::ServiceConfig::Create(service_config_json);
        if (service_config != nullptr) {
          if (chand->enable_retries) {
            channel_arg = grpc_channel_args_find(chand->resolver_result,
                                                 GRPC_ARG_SERVER_URI);
            const char* server_uri = grpc_channel_arg_get_string(channel_arg);
            GPR_ASSERT(server_uri != nullptr);
            grpc_uri* uri = grpc_uri_parse(server_uri, true);
            GPR_ASSERT(uri->path[0] != '\0');
            service_config_parsing_state parsing_state;
            memset(&parsing_state, 0, sizeof(parsing_state));
            parsing_state.server_name =
                uri->path[0] == '/' ? uri->path + 1 : uri->path;
            service_config->ParseGlobalParams(parse_retry_throttle_params,
                                              &parsing_state);
            grpc_uri_destroy(uri);
            retry_throttle_data = std::move(parsing_state.retry_throttle_data);
          }
          method_params_table = service_config->CreateMethodConfigTable(
              ClientChannelMethodParams::CreateFromJson);
        }
      }
    }
  }
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p: resolver result: lb_policy_name=\"%s\"%s, "
            "service_config=\"%s\"",
            chand, lb_policy_name_dup,
            lb_policy_name_changed ? " (changed)" : "", service_config_json);
  }
  // Now swap out fields in chand.  Note that the new values may still
  // be nullptr if (e.g.) the resolver failed to return results or the
  // results did not contain the necessary data.
  //
  // First, swap out the data used by cc_get_channel_info().
  gpr_mu_lock(&chand->info_mu);
  if (lb_policy_name_dup != nullptr) {
    gpr_free(chand->info_lb_policy_name);
    chand->info_lb_policy_name = lb_policy_name_dup;
  }
  if (service_config_json != nullptr) {
    gpr_free(chand->info_service_config_json);
    chand->info_service_config_json = service_config_json;
  }
  gpr_mu_unlock(&chand->info_mu);
  // Swap out the retry throttle data.
  chand->retry_throttle_data = std::move(retry_throttle_data);
  // Swap out the method params table.
  chand->method_params_table = std::move(method_params_table);
  // If we have a new LB policy or are shutting down (in which case
  // new_lb_policy will be nullptr), swap out the LB policy, unreffing the
  // old one and removing its fds from chand->interested_parties.
  // Note that we do NOT do this if either (a) we updated the existing
  // LB policy above or (b) we failed to create the new LB policy (in
  // which case we want to continue using the most recent one we had).
  if (new_lb_policy != nullptr || error != GRPC_ERROR_NONE ||
      chand->resolver == nullptr) {
    if (chand->lb_policy != nullptr) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO, "chand=%p: unreffing lb_policy=%p", chand,
                chand->lb_policy.get());
      }
      grpc_pollset_set_del_pollset_set(chand->lb_policy->interested_parties(),
                                       chand->interested_parties);
      chand->lb_policy->HandOffPendingPicksLocked(new_lb_policy.get());
      chand->lb_policy.reset();
    }
    chand->lb_policy = std::move(new_lb_policy);
  }
  // Now that we've swapped out the relevant fields of chand, check for
  // error or shutdown.
  if (error != GRPC_ERROR_NONE || chand->resolver == nullptr) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p: shutting down", chand);
    }
    if (chand->resolver != nullptr) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO, "chand=%p: shutting down resolver", chand);
      }
      chand->resolver.reset();
    }
    set_channel_connectivity_state_locked(
        chand, GRPC_CHANNEL_SHUTDOWN,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "Got resolver result after disconnection", &error, 1),
        "resolver_gone");
    grpc_closure_list_fail_all(&chand->waiting_for_resolver_result_closures,
                               GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                   "Channel disconnected", &error, 1));
    GRPC_CLOSURE_LIST_SCHED(&chand->waiting_for_resolver_result_closures);
    GRPC_CHANNEL_STACK_UNREF(chand->owning_stack, "resolver");
    grpc_channel_args_destroy(chand->resolver_result);
    chand->resolver_result = nullptr;
  } else {  // Not shutting down.
    grpc_connectivity_state state = GRPC_CHANNEL_TRANSIENT_FAILURE;
    grpc_error* state_error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("No load balancing policy");
    if (lb_policy_created) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO, "chand=%p: initializing new LB policy", chand);
      }
      GRPC_ERROR_UNREF(state_error);
      state = chand->lb_policy->CheckConnectivityLocked(&state_error);
      grpc_pollset_set_add_pollset_set(chand->lb_policy->interested_parties(),
                                       chand->interested_parties);
      GRPC_CLOSURE_LIST_SCHED(&chand->waiting_for_resolver_result_closures);
      if (chand->exit_idle_when_lb_policy_arrives) {
        chand->lb_policy->ExitIdleLocked();
        chand->exit_idle_when_lb_policy_arrives = false;
      }
      watch_lb_policy_locked(chand, chand->lb_policy.get(), state);
    } else if (chand->resolver_result == nullptr) {
      // Transient failure.
      GRPC_CLOSURE_LIST_SCHED(&chand->waiting_for_resolver_result_closures);
    }
    if (!lb_policy_updated) {
      set_channel_connectivity_state_locked(
          chand, state, GRPC_ERROR_REF(state_error), "new_lb+resolver");
    }
    grpc_channel_args_destroy(chand->resolver_result);
    chand->resolver_result = nullptr;
    chand->resolver->NextLocked(&chand->resolver_result,
                                &chand->on_resolver_result_changed);
    GRPC_ERROR_UNREF(state_error);
  }
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
    if (chand->lb_policy == nullptr) {
      GRPC_CLOSURE_SCHED(
          op->send_ping.on_initiate,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Ping with no load balancing"));
      GRPC_CLOSURE_SCHED(
          op->send_ping.on_ack,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Ping with no load balancing"));
    } else {
      chand->lb_policy->PingOneLocked(op->send_ping.on_initiate,
                                      op->send_ping.on_ack);
      op->bind_pollset = nullptr;
    }
    op->send_ping.on_initiate = nullptr;
    op->send_ping.on_ack = nullptr;
  }

  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    if (chand->resolver != nullptr) {
      set_channel_connectivity_state_locked(
          chand, GRPC_CHANNEL_SHUTDOWN,
          GRPC_ERROR_REF(op->disconnect_with_error), "disconnect");
      chand->resolver.reset();
      if (!chand->started_resolving) {
        grpc_closure_list_fail_all(&chand->waiting_for_resolver_result_closures,
                                   GRPC_ERROR_REF(op->disconnect_with_error));
        GRPC_CLOSURE_LIST_SCHED(&chand->waiting_for_resolver_result_closures);
      }
      if (chand->lb_policy != nullptr) {
        grpc_pollset_set_del_pollset_set(chand->lb_policy->interested_parties(),
                                         chand->interested_parties);
        chand->lb_policy.reset();
      }
    }
    GRPC_ERROR_UNREF(op->disconnect_with_error);
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
    *info->lb_policy_name = chand->info_lb_policy_name == nullptr
                                ? nullptr
                                : gpr_strdup(chand->info_lb_policy_name);
  }
  if (info->service_config_json != nullptr) {
    *info->service_config_json =
        chand->info_service_config_json == nullptr
            ? nullptr
            : gpr_strdup(chand->info_service_config_json);
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
  gpr_mu_init(&chand->info_mu);
  gpr_mu_init(&chand->external_connectivity_watcher_list_mu);

  gpr_mu_lock(&chand->external_connectivity_watcher_list_mu);
  chand->external_connectivity_watcher_list_head = nullptr;
  gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);

  chand->owning_stack = args->channel_stack;
  GRPC_CLOSURE_INIT(&chand->on_resolver_result_changed,
                    on_resolver_result_changed_locked, chand,
                    grpc_combiner_scheduler(chand->combiner));
  chand->interested_parties = grpc_pollset_set_create();
  grpc_connectivity_state_init(&chand->state_tracker, GRPC_CHANNEL_IDLE,
                               "client_channel");
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
  arg = grpc_channel_args_find(args->channel_args,
                               GRPC_ARG_CLIENT_CHANNEL_FACTORY);
  if (arg == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Missing client channel factory in args for client channel filter");
  }
  if (arg->type != GRPC_ARG_POINTER) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "client channel factory arg must be a pointer");
  }
  grpc_client_channel_factory_ref(
      static_cast<grpc_client_channel_factory*>(arg->value.pointer.p));
  chand->client_channel_factory =
      static_cast<grpc_client_channel_factory*>(arg->value.pointer.p);
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
  // Instantiate resolver.
  chand->resolver = grpc_core::ResolverRegistry::CreateResolver(
      proxy_name != nullptr ? proxy_name : arg->value.string,
      new_args != nullptr ? new_args : args->channel_args,
      chand->interested_parties, chand->combiner);
  if (proxy_name != nullptr) gpr_free(proxy_name);
  if (new_args != nullptr) grpc_channel_args_destroy(new_args);
  if (chand->resolver == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("resolver creation failed");
  }
  chand->deadline_checking_enabled =
      grpc_deadline_checking_enabled(args->channel_args);
  return GRPC_ERROR_NONE;
}

static void shutdown_resolver_locked(void* arg, grpc_error* error) {
  grpc_core::Resolver* resolver = static_cast<grpc_core::Resolver*>(arg);
  resolver->Orphan();
}

/* Destructor for channel_data */
static void cc_destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (chand->resolver != nullptr) {
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_CREATE(shutdown_resolver_locked, chand->resolver.release(),
                            grpc_combiner_scheduler(chand->combiner)),
        GRPC_ERROR_NONE);
  }
  if (chand->client_channel_factory != nullptr) {
    grpc_client_channel_factory_unref(chand->client_channel_factory);
  }
  if (chand->lb_policy != nullptr) {
    grpc_pollset_set_del_pollset_set(chand->lb_policy->interested_parties(),
                                     chand->interested_parties);
    chand->lb_policy.reset();
  }
  gpr_free(chand->info_lb_policy_name);
  gpr_free(chand->info_service_config_json);
  chand->retry_throttle_data.reset();
  chand->method_params_table.reset();
  grpc_client_channel_stop_backup_polling(chand->interested_parties);
  grpc_connectivity_state_destroy(&chand->state_tracker);
  grpc_pollset_set_destroy(chand->interested_parties);
  GRPC_COMBINER_UNREF(chand->combiner, "client_channel");
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

// State used for starting a retryable batch on a subchannel call.
// This provides its own grpc_transport_stream_op_batch and other data
// structures needed to populate the ops in the batch.
// We allocate one struct on the arena for each attempt at starting a
// batch on a given subchannel call.
typedef struct {
  gpr_refcount refs;
  grpc_call_element* elem;
  grpc_subchannel_call* subchannel_call;  // Holds a ref.
  // The batch to use in the subchannel call.
  // Its payload field points to subchannel_call_retry_state.batch_payload.
  grpc_transport_stream_op_batch batch;
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
  bool trailing_metadata_available;
  // For intercepting recv_message.
  grpc_closure recv_message_ready;
  grpc_core::OrphanablePtr<grpc_core::ByteStream> recv_message;
  // For intercepting recv_trailing_metadata.
  grpc_metadata_batch recv_trailing_metadata;
  grpc_transport_stream_stats collect_stats;
  // For intercepting on_complete.
  grpc_closure on_complete;
} subchannel_batch_data;

// Retry state associated with a subchannel call.
// Stored in the parent_data of the subchannel call object.
typedef struct {
  // subchannel_batch_data.batch.payload points to this.
  grpc_transport_stream_op_batch_payload batch_payload;
  // These fields indicate which ops have been started and completed on
  // this subchannel call.
  size_t started_send_message_count;
  size_t completed_send_message_count;
  size_t started_recv_message_count;
  size_t completed_recv_message_count;
  bool started_send_initial_metadata : 1;
  bool completed_send_initial_metadata : 1;
  bool started_send_trailing_metadata : 1;
  bool completed_send_trailing_metadata : 1;
  bool started_recv_initial_metadata : 1;
  bool completed_recv_initial_metadata : 1;
  bool started_recv_trailing_metadata : 1;
  bool completed_recv_trailing_metadata : 1;
  // State for callback processing.
  bool retry_dispatched : 1;
  subchannel_batch_data* recv_initial_metadata_ready_deferred_batch;
  grpc_error* recv_initial_metadata_error;
  subchannel_batch_data* recv_message_ready_deferred_batch;
  grpc_error* recv_message_error;
  subchannel_batch_data* recv_trailing_metadata_internal_batch;
} subchannel_call_retry_state;

// Pending batches stored in call data.
typedef struct {
  // The pending batch.  If nullptr, this slot is empty.
  grpc_transport_stream_op_batch* batch;
  // Indicates whether payload for send ops has been cached in call data.
  bool send_ops_cached;
} pending_batch;

/** Call data.  Holds a pointer to grpc_subchannel_call and the
    associated machinery to create such a pointer.
    Handles queueing of stream ops until a call object is ready, waiting
    for initial metadata before trying to create a call object,
    and handling cancellation gracefully. */
typedef struct client_channel_call_data {
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

  grpc_core::RefCountedPtr<ServerRetryThrottleData> retry_throttle_data;
  grpc_core::RefCountedPtr<ClientChannelMethodParams> method_params;

  grpc_subchannel_call* subchannel_call;

  // Set when we get a cancel_stream op.
  grpc_error* cancel_error;

  grpc_core::LoadBalancingPolicy::PickState pick;
  grpc_closure pick_closure;
  grpc_closure pick_cancel_closure;

  grpc_polling_entity* pollent;

  // Batches are added to this list when received from above.
  // They are removed when we are done handling the batch (i.e., when
  // either we have invoked all of the batch's callbacks or we have
  // passed the batch down to the subchannel call and are not
  // intercepting any of its callbacks).
  pending_batch pending_batches[MAX_PENDING_BATCHES];
  bool pending_send_initial_metadata : 1;
  bool pending_send_message : 1;
  bool pending_send_trailing_metadata : 1;

  // Retry state.
  bool enable_retries : 1;
  bool retry_committed : 1;
  bool last_attempt_got_server_pushback : 1;
  int num_attempts_completed;
  size_t bytes_buffered_for_retry;
  grpc_core::ManualConstructor<grpc_core::BackOff> retry_backoff;
  grpc_timer retry_timer;

  // Cached data for retrying send ops.
  // send_initial_metadata
  bool seen_send_initial_metadata;
  grpc_linked_mdelem* send_initial_metadata_storage;
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
  grpc_core::ManualConstructor<
      grpc_core::InlinedVector<grpc_core::ByteStreamCache*, 3>>
      send_messages;
  // send_trailing_metadata
  bool seen_send_trailing_metadata;
  grpc_linked_mdelem* send_trailing_metadata_storage;
  grpc_metadata_batch send_trailing_metadata;
} call_data;

// Forward declarations.
static void retry_commit(grpc_call_element* elem,
                         subchannel_call_retry_state* retry_state);
static void start_internal_recv_trailing_metadata(grpc_call_element* elem);
static void on_complete(void* arg, grpc_error* error);
static void start_retriable_subchannel_batches(void* arg, grpc_error* ignored);
static void pick_after_resolver_result_start_locked(grpc_call_element* elem);
static void start_pick_locked(void* arg, grpc_error* ignored);

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
    calld->send_messages->push_back(cache);
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
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: destroying calld->send_initial_metadata", chand,
            calld);
  }
  grpc_metadata_batch_destroy(&calld->send_initial_metadata);
}

// Frees cached send_message at index idx.
static void free_cached_send_message(channel_data* chand, call_data* calld,
                                     size_t idx) {
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: destroying calld->send_messages[%" PRIuPTR "]",
            chand, calld, idx);
  }
  (*calld->send_messages)[idx]->Destroy();
}

// Frees cached send_trailing_metadata.
static void free_cached_send_trailing_metadata(channel_data* chand,
                                               call_data* calld) {
  if (grpc_client_channel_trace.enabled()) {
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
  if (grpc_client_channel_trace.enabled()) {
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
    if (calld->bytes_buffered_for_retry > chand->per_rpc_retry_buffer_size) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: exceeded retry buffer size, committing",
                chand, calld);
      }
      subchannel_call_retry_state* retry_state =
          calld->subchannel_call == nullptr
              ? nullptr
              : static_cast<subchannel_call_retry_state*>(
                    grpc_connected_subchannel_call_get_parent_data(
                        calld->subchannel_call));
      retry_commit(elem, retry_state);
      // If we are not going to retry and have not yet started, pretend
      // retries are disabled so that we don't bother with retry overhead.
      if (calld->num_attempts_completed == 0) {
        if (grpc_client_channel_trace.enabled()) {
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
// If yield_call_combiner is true, assumes responsibility for yielding
// the call combiner.
static void pending_batches_fail(grpc_call_element* elem, grpc_error* error,
                                 bool yield_call_combiner) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
      if (calld->pending_batches[i].batch != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: failing %" PRIuPTR " pending batches: %s",
            elem->channel_data, calld, num_batches, grpc_error_string(error));
  }
  grpc_transport_stream_op_batch*
      batches[GPR_ARRAY_SIZE(calld->pending_batches)];
  size_t num_batches = 0;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    pending_batch* pending = &calld->pending_batches[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch != nullptr) {
      batches[num_batches++] = batch;
      pending_batch_clear(calld, pending);
    }
  }
  for (size_t i = yield_call_combiner ? 1 : 0; i < num_batches; ++i) {
    grpc_transport_stream_op_batch* batch = batches[i];
    batch->handler_private.extra_arg = calld;
    GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                      fail_pending_batch_in_call_combiner, batch,
                      grpc_schedule_on_exec_ctx);
    GRPC_CALL_COMBINER_START(calld->call_combiner,
                             &batch->handler_private.closure,
                             GRPC_ERROR_REF(error), "pending_batches_fail");
  }
  if (yield_call_combiner) {
    if (num_batches > 0) {
      // Note: This will release the call combiner.
      grpc_transport_stream_op_batch_finish_with_failure(
          batches[0], GRPC_ERROR_REF(error), calld->call_combiner);
    } else {
      GRPC_CALL_COMBINER_STOP(calld->call_combiner, "pending_batches_fail");
    }
  }
  GRPC_ERROR_UNREF(error);
}

// This is called via the call combiner, so access to calld is synchronized.
static void resume_pending_batch_in_call_combiner(void* arg,
                                                  grpc_error* ignored) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  grpc_subchannel_call* subchannel_call =
      static_cast<grpc_subchannel_call*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  grpc_subchannel_call_process_op(subchannel_call, batch);
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
  if (grpc_client_channel_trace.enabled()) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
      if (calld->pending_batches[i].batch != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting %" PRIuPTR
            " pending batches on subchannel_call=%p",
            chand, calld, num_batches, calld->subchannel_call);
  }
  grpc_transport_stream_op_batch*
      batches[GPR_ARRAY_SIZE(calld->pending_batches)];
  size_t num_batches = 0;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    pending_batch* pending = &calld->pending_batches[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch != nullptr) {
      batches[num_batches++] = batch;
      pending_batch_clear(calld, pending);
    }
  }
  for (size_t i = 1; i < num_batches; ++i) {
    grpc_transport_stream_op_batch* batch = batches[i];
    batch->handler_private.extra_arg = calld->subchannel_call;
    GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                      resume_pending_batch_in_call_combiner, batch,
                      grpc_schedule_on_exec_ctx);
    GRPC_CALL_COMBINER_START(calld->call_combiner,
                             &batch->handler_private.closure, GRPC_ERROR_NONE,
                             "pending_batches_resume");
  }
  GPR_ASSERT(num_batches > 0);
  // Note: This will release the call combiner.
  grpc_subchannel_call_process_op(calld->subchannel_call, batches[0]);
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
       batch->payload->recv_message.recv_message_ready == nullptr)) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: clearing pending batch", chand,
              calld);
    }
    pending_batch_clear(calld, pending);
  }
}

// Returns true if all ops in the pending batch have been completed.
static bool pending_batch_is_completed(
    pending_batch* pending, call_data* calld,
    subchannel_call_retry_state* retry_state) {
  if (pending->batch == nullptr || pending->batch->on_complete == nullptr) {
    return false;
  }
  if (pending->batch->send_initial_metadata &&
      !retry_state->completed_send_initial_metadata) {
    return false;
  }
  if (pending->batch->send_message &&
      retry_state->completed_send_message_count <
          calld->send_messages->size()) {
    return false;
  }
  if (pending->batch->send_trailing_metadata &&
      !retry_state->completed_send_trailing_metadata) {
    return false;
  }
  if (pending->batch->recv_initial_metadata &&
      !retry_state->completed_recv_initial_metadata) {
    return false;
  }
  if (pending->batch->recv_message &&
      retry_state->completed_recv_message_count <
          retry_state->started_recv_message_count) {
    return false;
  }
  if (pending->batch->recv_trailing_metadata &&
      !retry_state->completed_recv_trailing_metadata) {
    return false;
  }
  return true;
}

// Returns true if any op in the batch was not yet started.
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
      retry_state->started_send_message_count < calld->send_messages->size()) {
    return true;
  }
  if (pending->batch->send_trailing_metadata &&
      !retry_state->started_send_trailing_metadata) {
    return true;
  }
  if (pending->batch->recv_initial_metadata &&
      !retry_state->started_recv_initial_metadata) {
    return true;
  }
  if (pending->batch->recv_message &&
      retry_state->completed_recv_message_count ==
          retry_state->started_recv_message_count) {
    return true;
  }
  if (pending->batch->recv_trailing_metadata &&
      !retry_state->started_recv_trailing_metadata) {
    return true;
  }
  return false;
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
  if (grpc_client_channel_trace.enabled()) {
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
  if (calld->subchannel_call != nullptr) {
    GRPC_SUBCHANNEL_CALL_UNREF(calld->subchannel_call,
                               "client_channel_call_retry");
    calld->subchannel_call = nullptr;
  }
  if (calld->pick.connected_subchannel != nullptr) {
    calld->pick.connected_subchannel.reset();
  }
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
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: retrying failed call in %" PRIuPTR " ms", chand,
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
        grpc_connected_subchannel_call_get_parent_data(
            batch_data->subchannel_call));
    if (retry_state->retry_dispatched) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: retry already dispatched", chand,
                calld);
      }
      return true;
    }
  }
  // Check status.
  if (status == GRPC_STATUS_OK) {
    if (calld->retry_throttle_data != nullptr) {
      calld->retry_throttle_data->RecordSuccess();
    }
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: call succeeded", chand, calld);
    }
    return false;
  }
  // Status is not OK.  Check whether the status is retryable.
  if (!retry_policy->retryable_status_codes.Contains(status)) {
    if (grpc_client_channel_trace.enabled()) {
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
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: retries throttled", chand, calld);
    }
    return false;
  }
  // Check whether the call is committed.
  if (calld->retry_committed) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: retries already committed", chand,
              calld);
    }
    return false;
  }
  // Check whether we have retries remaining.
  ++calld->num_attempts_completed;
  if (calld->num_attempts_completed >= retry_policy->max_attempts) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: exceeded %d retry attempts", chand,
              calld, retry_policy->max_attempts);
    }
    return false;
  }
  // If the call was cancelled from the surface, don't retry.
  if (calld->cancel_error != GRPC_ERROR_NONE) {
    if (grpc_client_channel_trace.enabled()) {
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
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: not retrying due to server push-back",
                chand, calld);
      }
      return false;
    } else {
      if (grpc_client_channel_trace.enabled()) {
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

static subchannel_batch_data* batch_data_create(grpc_call_element* elem,
                                                int refcount) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          grpc_connected_subchannel_call_get_parent_data(
              calld->subchannel_call));
  subchannel_batch_data* batch_data = static_cast<subchannel_batch_data*>(
      gpr_arena_alloc(calld->arena, sizeof(*batch_data)));
  batch_data->elem = elem;
  batch_data->subchannel_call =
      GRPC_SUBCHANNEL_CALL_REF(calld->subchannel_call, "batch_data_create");
  batch_data->batch.payload = &retry_state->batch_payload;
  gpr_ref_init(&batch_data->refs, refcount);
  GRPC_CLOSURE_INIT(&batch_data->on_complete, on_complete, batch_data,
                    grpc_schedule_on_exec_ctx);
  batch_data->batch.on_complete = &batch_data->on_complete;
  GRPC_CALL_STACK_REF(calld->owning_call, "batch_data");
  return batch_data;
}

static void batch_data_unref(subchannel_batch_data* batch_data) {
  if (gpr_unref(&batch_data->refs)) {
    if (batch_data->send_initial_metadata_storage != nullptr) {
      grpc_metadata_batch_destroy(&batch_data->send_initial_metadata);
    }
    if (batch_data->send_trailing_metadata_storage != nullptr) {
      grpc_metadata_batch_destroy(&batch_data->send_trailing_metadata);
    }
    if (batch_data->batch.recv_initial_metadata) {
      grpc_metadata_batch_destroy(&batch_data->recv_initial_metadata);
    }
    if (batch_data->batch.recv_trailing_metadata) {
      grpc_metadata_batch_destroy(&batch_data->recv_trailing_metadata);
    }
    GRPC_SUBCHANNEL_CALL_UNREF(batch_data->subchannel_call, "batch_data_unref");
    call_data* calld = static_cast<call_data*>(batch_data->elem->call_data);
    GRPC_CALL_STACK_UNREF(calld->owning_call, "batch_data");
  }
}

//
// recv_initial_metadata callback handling
//

// Invokes recv_initial_metadata_ready for a subchannel batch.
static void invoke_recv_initial_metadata_callback(void* arg,
                                                  grpc_error* error) {
  subchannel_batch_data* batch_data = static_cast<subchannel_batch_data*>(arg);
  channel_data* chand =
      static_cast<channel_data*>(batch_data->elem->channel_data);
  call_data* calld = static_cast<call_data*>(batch_data->elem->call_data);
  // Find pending batch.
  pending_batch* pending = nullptr;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    grpc_transport_stream_op_batch* batch = calld->pending_batches[i].batch;
    if (batch != nullptr && batch->recv_initial_metadata &&
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready !=
            nullptr) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: invoking recv_initial_metadata_ready for "
                "pending batch at index %" PRIuPTR,
                chand, calld, i);
      }
      pending = &calld->pending_batches[i];
      break;
    }
  }
  GPR_ASSERT(pending != nullptr);
  // Return metadata.
  grpc_metadata_batch_move(
      &batch_data->recv_initial_metadata,
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
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: got recv_initial_metadata_ready, error=%s",
            chand, calld, grpc_error_string(error));
  }
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          grpc_connected_subchannel_call_get_parent_data(
              batch_data->subchannel_call));
  // If we got an error or a Trailers-Only response and have not yet gotten
  // the recv_trailing_metadata on_complete callback, then defer
  // propagating this callback back to the surface.  We can evaluate whether
  // to retry when recv_trailing_metadata comes back.
  if ((batch_data->trailing_metadata_available || error != GRPC_ERROR_NONE) &&
      !retry_state->completed_recv_trailing_metadata) {
    if (grpc_client_channel_trace.enabled()) {
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
  // Manually invoking a callback function; it does not take ownership of error.
  invoke_recv_initial_metadata_callback(batch_data, error);
  GRPC_ERROR_UNREF(error);
}

//
// recv_message callback handling
//

// Invokes recv_message_ready for a subchannel batch.
static void invoke_recv_message_callback(void* arg, grpc_error* error) {
  subchannel_batch_data* batch_data = static_cast<subchannel_batch_data*>(arg);
  channel_data* chand =
      static_cast<channel_data*>(batch_data->elem->channel_data);
  call_data* calld = static_cast<call_data*>(batch_data->elem->call_data);
  // Find pending op.
  pending_batch* pending = nullptr;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    grpc_transport_stream_op_batch* batch = calld->pending_batches[i].batch;
    if (batch != nullptr && batch->recv_message &&
        batch->payload->recv_message.recv_message_ready != nullptr) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: invoking recv_message_ready for "
                "pending batch at index %" PRIuPTR,
                chand, calld, i);
      }
      pending = &calld->pending_batches[i];
      break;
    }
  }
  GPR_ASSERT(pending != nullptr);
  // Return payload.
  *pending->batch->payload->recv_message.recv_message =
      std::move(batch_data->recv_message);
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
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: got recv_message_ready, error=%s",
            chand, calld, grpc_error_string(error));
  }
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          grpc_connected_subchannel_call_get_parent_data(
              batch_data->subchannel_call));
  // If we got an error or the payload was nullptr and we have not yet gotten
  // the recv_trailing_metadata on_complete callback, then defer
  // propagating this callback back to the surface.  We can evaluate whether
  // to retry when recv_trailing_metadata comes back.
  if ((batch_data->recv_message == nullptr || error != GRPC_ERROR_NONE) &&
      !retry_state->completed_recv_trailing_metadata) {
    if (grpc_client_channel_trace.enabled()) {
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
  // Manually invoking a callback function; it does not take ownership of error.
  invoke_recv_message_callback(batch_data, error);
  GRPC_ERROR_UNREF(error);
}

//
// list of closures to execute in call combiner
//

// Represents a closure that needs to run in the call combiner as part of
// starting or completing a batch.
typedef struct {
  grpc_closure* closure;
  grpc_error* error;
  const char* reason;
  bool free_reason = false;
} closure_to_execute;

static void execute_closures_in_call_combiner(grpc_call_element* elem,
                                              const char* caller,
                                              closure_to_execute* closures,
                                              size_t num_closures) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  // Note that the call combiner will be yielded for each closure that
  // we schedule.  We're already running in the call combiner, so one of
  // the closures can be scheduled directly, but the others will
  // have to re-enter the call combiner.
  if (num_closures > 0) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: %s starting closure: %s", chand,
              calld, caller, closures[0].reason);
    }
    GRPC_CLOSURE_SCHED(closures[0].closure, closures[0].error);
    if (closures[0].free_reason) {
      gpr_free(const_cast<char*>(closures[0].reason));
    }
    for (size_t i = 1; i < num_closures; ++i) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: %s starting closure in call combiner: %s",
                chand, calld, caller, closures[i].reason);
      }
      GRPC_CALL_COMBINER_START(calld->call_combiner, closures[i].closure,
                               closures[i].error, closures[i].reason);
      if (closures[i].free_reason) {
        gpr_free(const_cast<char*>(closures[i].reason));
      }
    }
  } else {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: no closures to run for %s", chand,
              calld, caller);
    }
    GRPC_CALL_COMBINER_STOP(calld->call_combiner, "no closures to run");
  }
}

//
// on_complete callback handling
//

// Updates retry_state to reflect the ops completed in batch_data.
static void update_retry_state_for_completed_batch(
    subchannel_batch_data* batch_data,
    subchannel_call_retry_state* retry_state) {
  if (batch_data->batch.send_initial_metadata) {
    retry_state->completed_send_initial_metadata = true;
  }
  if (batch_data->batch.send_message) {
    ++retry_state->completed_send_message_count;
  }
  if (batch_data->batch.send_trailing_metadata) {
    retry_state->completed_send_trailing_metadata = true;
  }
  if (batch_data->batch.recv_initial_metadata) {
    retry_state->completed_recv_initial_metadata = true;
  }
  if (batch_data->batch.recv_message) {
    ++retry_state->completed_recv_message_count;
  }
  if (batch_data->batch.recv_trailing_metadata) {
    retry_state->completed_recv_trailing_metadata = true;
  }
}

// Adds any necessary closures for deferred recv_initial_metadata and
// recv_message callbacks to closures, updating *num_closures as needed.
static void add_closures_for_deferred_recv_callbacks(
    subchannel_batch_data* batch_data, subchannel_call_retry_state* retry_state,
    closure_to_execute* closures, size_t* num_closures) {
  if (batch_data->batch.recv_trailing_metadata) {
    // Add closure for deferred recv_initial_metadata_ready.
    if (retry_state->recv_initial_metadata_ready_deferred_batch != nullptr) {
      closure_to_execute* closure = &closures[(*num_closures)++];
      closure->closure = GRPC_CLOSURE_INIT(
          &batch_data->recv_initial_metadata_ready,
          invoke_recv_initial_metadata_callback,
          retry_state->recv_initial_metadata_ready_deferred_batch,
          grpc_schedule_on_exec_ctx);
      closure->error = retry_state->recv_initial_metadata_error;
      closure->reason = "resuming recv_initial_metadata_ready";
      retry_state->recv_initial_metadata_ready_deferred_batch = nullptr;
    }
    // Add closure for deferred recv_message_ready.
    if (retry_state->recv_message_ready_deferred_batch != nullptr) {
      closure_to_execute* closure = &closures[(*num_closures)++];
      closure->closure = GRPC_CLOSURE_INIT(
          &batch_data->recv_message_ready, invoke_recv_message_callback,
          retry_state->recv_message_ready_deferred_batch,
          grpc_schedule_on_exec_ctx);
      closure->error = retry_state->recv_message_error;
      closure->reason = "resuming recv_message_ready";
      retry_state->recv_message_ready_deferred_batch = nullptr;
    }
  }
}

// If there are any cached ops to replay or pending ops to start on the
// subchannel call, adds a closure to closures to invoke
// start_retriable_subchannel_batches(), updating *num_closures as needed.
static void add_closures_for_replay_or_pending_send_ops(
    grpc_call_element* elem, subchannel_batch_data* batch_data,
    subchannel_call_retry_state* retry_state, closure_to_execute* closures,
    size_t* num_closures) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  bool have_pending_send_message_ops =
      retry_state->started_send_message_count < calld->send_messages->size();
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
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: starting next batch for pending send op(s)",
              chand, calld);
    }
    closure_to_execute* closure = &closures[(*num_closures)++];
    closure->closure = GRPC_CLOSURE_INIT(
        &batch_data->batch.handler_private.closure,
        start_retriable_subchannel_batches, elem, grpc_schedule_on_exec_ctx);
    closure->error = GRPC_ERROR_NONE;
    closure->reason = "starting next batch for send_* op(s)";
  }
}

// For any pending batch completed in batch_data, adds the necessary
// completion closures to closures, updating *num_closures as needed.
static void add_closures_for_completed_pending_batches(
    grpc_call_element* elem, subchannel_batch_data* batch_data,
    subchannel_call_retry_state* retry_state, grpc_error* error,
    closure_to_execute* closures, size_t* num_closures) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    pending_batch* pending = &calld->pending_batches[i];
    if (pending_batch_is_completed(pending, calld, retry_state)) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: pending batch completed at index %" PRIuPTR,
                chand, calld, i);
      }
      // Copy the trailing metadata to return it to the surface.
      if (batch_data->batch.recv_trailing_metadata) {
        grpc_metadata_batch_move(&batch_data->recv_trailing_metadata,
                                 pending->batch->payload->recv_trailing_metadata
                                     .recv_trailing_metadata);
      }
      closure_to_execute* closure = &closures[(*num_closures)++];
      closure->closure = pending->batch->on_complete;
      closure->error = GRPC_ERROR_REF(error);
      closure->reason = "on_complete for pending batch";
      pending->batch->on_complete = nullptr;
      maybe_clear_pending_batch(elem, pending);
    }
  }
  GRPC_ERROR_UNREF(error);
}

// For any pending batch containing an op that has not yet been started,
// adds the pending batch's completion closures to closures, updating
// *num_closures as needed.
static void add_closures_to_fail_unstarted_pending_batches(
    grpc_call_element* elem, subchannel_call_retry_state* retry_state,
    grpc_error* error, closure_to_execute* closures, size_t* num_closures) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    pending_batch* pending = &calld->pending_batches[i];
    if (pending_batch_is_unstarted(pending, calld, retry_state)) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: failing unstarted pending batch at index "
                "%" PRIuPTR,
                chand, calld, i);
      }
      if (pending->batch->recv_initial_metadata) {
        closure_to_execute* closure = &closures[(*num_closures)++];
        closure->closure = pending->batch->payload->recv_initial_metadata
                               .recv_initial_metadata_ready;
        closure->error = GRPC_ERROR_REF(error);
        closure->reason =
            "failing recv_initial_metadata_ready for pending batch";
        pending->batch->payload->recv_initial_metadata
            .recv_initial_metadata_ready = nullptr;
      }
      if (pending->batch->recv_message) {
        *pending->batch->payload->recv_message.recv_message = nullptr;
        closure_to_execute* closure = &closures[(*num_closures)++];
        closure->closure =
            pending->batch->payload->recv_message.recv_message_ready;
        closure->error = GRPC_ERROR_REF(error);
        closure->reason = "failing recv_message_ready for pending batch";
        pending->batch->payload->recv_message.recv_message_ready = nullptr;
      }
      closure_to_execute* closure = &closures[(*num_closures)++];
      closure->closure = pending->batch->on_complete;
      closure->error = GRPC_ERROR_REF(error);
      closure->reason = "failing on_complete for pending batch";
      pending->batch->on_complete = nullptr;
      maybe_clear_pending_batch(elem, pending);
    }
  }
  GRPC_ERROR_UNREF(error);
}

// Callback used to intercept on_complete from subchannel calls.
// Called only when retries are enabled.
static void on_complete(void* arg, grpc_error* error) {
  subchannel_batch_data* batch_data = static_cast<subchannel_batch_data*>(arg);
  grpc_call_element* elem = batch_data->elem;
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    char* batch_str = grpc_transport_stream_op_batch_string(&batch_data->batch);
    gpr_log(GPR_INFO, "chand=%p calld=%p: got on_complete, error=%s, batch=%s",
            chand, calld, grpc_error_string(error), batch_str);
    gpr_free(batch_str);
  }
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          grpc_connected_subchannel_call_get_parent_data(
              batch_data->subchannel_call));
  // If we have previously completed recv_trailing_metadata, then the
  // call is finished.
  bool call_finished = retry_state->completed_recv_trailing_metadata;
  // Record whether we were already committed before receiving this callback.
  const bool previously_committed = calld->retry_committed;
  // Update bookkeeping in retry_state.
  update_retry_state_for_completed_batch(batch_data, retry_state);
  if (call_finished) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: call already finished", chand,
              calld);
    }
  } else {
    // Check if this batch finished the call, and if so, get its status.
    // The call is finished if either (a) this callback was invoked with
    // an error or (b) we receive status.
    grpc_status_code status = GRPC_STATUS_OK;
    grpc_mdelem* server_pushback_md = nullptr;
    if (error != GRPC_ERROR_NONE) {  // Case (a).
      call_finished = true;
      grpc_error_get_status(error, calld->deadline, &status, nullptr, nullptr,
                            nullptr);
    } else if (batch_data->batch.recv_trailing_metadata) {  // Case (b).
      call_finished = true;
      grpc_metadata_batch* md_batch =
          batch_data->batch.payload->recv_trailing_metadata
              .recv_trailing_metadata;
      GPR_ASSERT(md_batch->idx.named.grpc_status != nullptr);
      status = grpc_get_status_code_from_metadata(
          md_batch->idx.named.grpc_status->md);
      if (md_batch->idx.named.grpc_retry_pushback_ms != nullptr) {
        server_pushback_md = &md_batch->idx.named.grpc_retry_pushback_ms->md;
      }
    }
    // If the call just finished, check if we should retry.
    if (call_finished) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: call finished, status=%s", chand,
                calld, grpc_status_code_to_string(status));
      }
      if (maybe_retry(elem, batch_data, status, server_pushback_md)) {
        // Unref batch_data for deferred recv_initial_metadata_ready or
        // recv_message_ready callbacks, if any.
        if (batch_data->batch.recv_trailing_metadata &&
            retry_state->recv_initial_metadata_ready_deferred_batch !=
                nullptr) {
          batch_data_unref(batch_data);
          GRPC_ERROR_UNREF(retry_state->recv_initial_metadata_error);
        }
        if (batch_data->batch.recv_trailing_metadata &&
            retry_state->recv_message_ready_deferred_batch != nullptr) {
          batch_data_unref(batch_data);
          GRPC_ERROR_UNREF(retry_state->recv_message_error);
        }
        batch_data_unref(batch_data);
        return;
      }
      // Not retrying, so commit the call.
      retry_commit(elem, retry_state);
    }
  }
  // If we were already committed before receiving this callback, free
  // cached data for send ops that we've just completed.  (If the call has
  // just now finished, the call to retry_commit() above will have freed all
  // cached send ops, so we don't need to do it here.)
  if (previously_committed) {
    free_cached_send_op_data_for_completed_batch(elem, batch_data, retry_state);
  }
  // Call not being retried.
  // Construct list of closures to execute.
  // Max number of closures is number of pending batches plus one for
  // each of:
  // - recv_initial_metadata_ready (either deferred or unstarted)
  // - recv_message_ready (either deferred or unstarted)
  // - starting a new batch for pending send ops
  closure_to_execute closures[GPR_ARRAY_SIZE(calld->pending_batches) + 3];
  size_t num_closures = 0;
  // If there are deferred recv_initial_metadata_ready or recv_message_ready
  // callbacks, add them to closures.
  add_closures_for_deferred_recv_callbacks(batch_data, retry_state, closures,
                                           &num_closures);
  // Find pending batches whose ops are now complete and add their
  // on_complete callbacks to closures.
  add_closures_for_completed_pending_batches(elem, batch_data, retry_state,
                                             GRPC_ERROR_REF(error), closures,
                                             &num_closures);
  // Add closures to handle any pending batches that have not yet been started.
  // If the call is finished, we fail these batches; otherwise, we add a
  // callback to start_retriable_subchannel_batches() to start them on
  // the subchannel call.
  if (call_finished) {
    add_closures_to_fail_unstarted_pending_batches(
        elem, retry_state, GRPC_ERROR_REF(error), closures, &num_closures);
  } else {
    add_closures_for_replay_or_pending_send_ops(elem, batch_data, retry_state,
                                                closures, &num_closures);
  }
  // Don't need batch_data anymore.
  batch_data_unref(batch_data);
  // Schedule all of the closures identified above.
  execute_closures_in_call_combiner(elem, "on_complete", closures,
                                    num_closures);
}

//
// subchannel batch construction
//

// Helper function used to start a subchannel batch in the call combiner.
static void start_batch_in_call_combiner(void* arg, grpc_error* ignored) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  grpc_subchannel_call* subchannel_call =
      static_cast<grpc_subchannel_call*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  grpc_subchannel_call_process_op(subchannel_call, batch);
}

// Adds a closure to closures that will execute batch in the call combiner.
static void add_closure_for_subchannel_batch(
    call_data* calld, grpc_transport_stream_op_batch* batch,
    closure_to_execute* closures, size_t* num_closures) {
  batch->handler_private.extra_arg = calld->subchannel_call;
  GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                    start_batch_in_call_combiner, batch,
                    grpc_schedule_on_exec_ctx);
  closure_to_execute* closure = &closures[(*num_closures)++];
  closure->closure = &batch->handler_private.closure;
  closure->error = GRPC_ERROR_NONE;
  // If the tracer is enabled, we log a more detailed message, which
  // requires dynamic allocation.  This will be freed in
  // start_retriable_subchannel_batches().
  if (grpc_client_channel_trace.enabled()) {
    char* batch_str = grpc_transport_stream_op_batch_string(batch);
    gpr_asprintf(const_cast<char**>(&closure->reason),
                 "starting batch in call combiner: %s", batch_str);
    gpr_free(batch_str);
    closure->free_reason = true;
  } else {
    closure->reason = "start_subchannel_batch";
  }
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
  batch_data->send_initial_metadata_storage =
      static_cast<grpc_linked_mdelem*>(gpr_arena_alloc(
          calld->arena, sizeof(grpc_linked_mdelem) *
                            (calld->send_initial_metadata.list.count +
                             (calld->num_attempts_completed > 0))));
  grpc_metadata_batch_copy(&calld->send_initial_metadata,
                           &batch_data->send_initial_metadata,
                           batch_data->send_initial_metadata_storage);
  if (batch_data->send_initial_metadata.idx.named.grpc_previous_rpc_attempts !=
      nullptr) {
    grpc_metadata_batch_remove(
        &batch_data->send_initial_metadata,
        batch_data->send_initial_metadata.idx.named.grpc_previous_rpc_attempts);
  }
  if (calld->num_attempts_completed > 0) {
    grpc_mdelem retry_md = grpc_mdelem_from_slices(
        GRPC_MDSTR_GRPC_PREVIOUS_RPC_ATTEMPTS,
        *retry_count_strings[calld->num_attempts_completed - 1]);
    grpc_error* error = grpc_metadata_batch_add_tail(
        &batch_data->send_initial_metadata,
        &batch_data->send_initial_metadata_storage[calld->send_initial_metadata
                                                       .list.count],
        retry_md);
    if (error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "error adding retry metadata: %s",
              grpc_error_string(error));
      GPR_ASSERT(false);
    }
  }
  retry_state->started_send_initial_metadata = true;
  batch_data->batch.send_initial_metadata = true;
  batch_data->batch.payload->send_initial_metadata.send_initial_metadata =
      &batch_data->send_initial_metadata;
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
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting calld->send_messages[%" PRIuPTR "]",
            chand, calld, retry_state->started_send_message_count);
  }
  grpc_core::ByteStreamCache* cache =
      (*calld->send_messages)[retry_state->started_send_message_count];
  ++retry_state->started_send_message_count;
  batch_data->send_message.Init(cache);
  batch_data->batch.send_message = true;
  batch_data->batch.payload->send_message.send_message.reset(
      batch_data->send_message.get());
}

// Adds retriable send_trailing_metadata op to batch_data.
static void add_retriable_send_trailing_metadata_op(
    call_data* calld, subchannel_call_retry_state* retry_state,
    subchannel_batch_data* batch_data) {
  // We need to make a copy of the metadata batch for each attempt, since
  // the filters in the subchannel stack may modify this batch, and we don't
  // want those modifications to be passed forward to subsequent attempts.
  batch_data->send_trailing_metadata_storage =
      static_cast<grpc_linked_mdelem*>(gpr_arena_alloc(
          calld->arena, sizeof(grpc_linked_mdelem) *
                            calld->send_trailing_metadata.list.count));
  grpc_metadata_batch_copy(&calld->send_trailing_metadata,
                           &batch_data->send_trailing_metadata,
                           batch_data->send_trailing_metadata_storage);
  retry_state->started_send_trailing_metadata = true;
  batch_data->batch.send_trailing_metadata = true;
  batch_data->batch.payload->send_trailing_metadata.send_trailing_metadata =
      &batch_data->send_trailing_metadata;
}

// Adds retriable recv_initial_metadata op to batch_data.
static void add_retriable_recv_initial_metadata_op(
    call_data* calld, subchannel_call_retry_state* retry_state,
    subchannel_batch_data* batch_data) {
  retry_state->started_recv_initial_metadata = true;
  batch_data->batch.recv_initial_metadata = true;
  grpc_metadata_batch_init(&batch_data->recv_initial_metadata);
  batch_data->batch.payload->recv_initial_metadata.recv_initial_metadata =
      &batch_data->recv_initial_metadata;
  batch_data->batch.payload->recv_initial_metadata.trailing_metadata_available =
      &batch_data->trailing_metadata_available;
  GRPC_CLOSURE_INIT(&batch_data->recv_initial_metadata_ready,
                    recv_initial_metadata_ready, batch_data,
                    grpc_schedule_on_exec_ctx);
  batch_data->batch.payload->recv_initial_metadata.recv_initial_metadata_ready =
      &batch_data->recv_initial_metadata_ready;
}

// Adds retriable recv_message op to batch_data.
static void add_retriable_recv_message_op(
    call_data* calld, subchannel_call_retry_state* retry_state,
    subchannel_batch_data* batch_data) {
  ++retry_state->started_recv_message_count;
  batch_data->batch.recv_message = true;
  batch_data->batch.payload->recv_message.recv_message =
      &batch_data->recv_message;
  GRPC_CLOSURE_INIT(&batch_data->recv_message_ready, recv_message_ready,
                    batch_data, grpc_schedule_on_exec_ctx);
  batch_data->batch.payload->recv_message.recv_message_ready =
      &batch_data->recv_message_ready;
}

// Adds retriable recv_trailing_metadata op to batch_data.
static void add_retriable_recv_trailing_metadata_op(
    call_data* calld, subchannel_call_retry_state* retry_state,
    subchannel_batch_data* batch_data) {
  retry_state->started_recv_trailing_metadata = true;
  batch_data->batch.recv_trailing_metadata = true;
  grpc_metadata_batch_init(&batch_data->recv_trailing_metadata);
  batch_data->batch.payload->recv_trailing_metadata.recv_trailing_metadata =
      &batch_data->recv_trailing_metadata;
  batch_data->batch.collect_stats = true;
  batch_data->batch.payload->collect_stats.collect_stats =
      &batch_data->collect_stats;
}

// Helper function used to start a recv_trailing_metadata batch.  This
// is used in the case where a recv_initial_metadata or recv_message
// op fails in a way that we know the call is over but when the application
// has not yet started its own recv_trailing_metadata op.
static void start_internal_recv_trailing_metadata(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: call failed but recv_trailing_metadata not "
            "started; starting it internally",
            chand, calld);
  }
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          grpc_connected_subchannel_call_get_parent_data(
              calld->subchannel_call));
  // Create batch_data with 2 refs, since this batch will be unreffed twice:
  // once when the subchannel batch returns, and again when we actually get
  // a recv_trailing_metadata op from the surface.
  subchannel_batch_data* batch_data = batch_data_create(elem, 2);
  add_retriable_recv_trailing_metadata_op(calld, retry_state, batch_data);
  retry_state->recv_trailing_metadata_internal_batch = batch_data;
  // Note: This will release the call combiner.
  grpc_subchannel_call_process_op(calld->subchannel_call, &batch_data->batch);
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
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_initial_metadata op",
              chand, calld);
    }
    replay_batch_data = batch_data_create(elem, 1);
    add_retriable_send_initial_metadata_op(calld, retry_state,
                                           replay_batch_data);
  }
  // send_message.
  // Note that we can only have one send_message op in flight at a time.
  if (retry_state->started_send_message_count < calld->send_messages->size() &&
      retry_state->started_send_message_count ==
          retry_state->completed_send_message_count &&
      !calld->pending_send_message) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_message op",
              chand, calld);
    }
    if (replay_batch_data == nullptr) {
      replay_batch_data = batch_data_create(elem, 1);
    }
    add_retriable_send_message_op(elem, retry_state, replay_batch_data);
  }
  // send_trailing_metadata.
  // Note that we only add this op if we have no more send_message ops
  // to start, since we can't send down any more send_message ops after
  // send_trailing_metadata.
  if (calld->seen_send_trailing_metadata &&
      retry_state->started_send_message_count == calld->send_messages->size() &&
      !retry_state->started_send_trailing_metadata &&
      !calld->pending_send_trailing_metadata) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_trailing_metadata op",
              chand, calld);
    }
    if (replay_batch_data == nullptr) {
      replay_batch_data = batch_data_create(elem, 1);
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
    closure_to_execute* closures, size_t* num_closures) {
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
             calld->send_messages->size() ||
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
      if (retry_state->recv_trailing_metadata_internal_batch != nullptr) {
        // If the batch completed, then trigger the completion callback
        // directly, so that we return the previously returned results to
        // the application.  Otherwise, just unref the internally
        // started subchannel batch, since we'll propagate the
        // completion when it completes.
        if (retry_state->completed_recv_trailing_metadata) {
          subchannel_batch_data* batch_data =
              retry_state->recv_trailing_metadata_internal_batch;
          closure_to_execute* closure = &closures[(*num_closures)++];
          closure->closure = &batch_data->on_complete;
          // Batches containing recv_trailing_metadata always succeed.
          closure->error = GRPC_ERROR_NONE;
          closure->reason =
              "re-executing on_complete for recv_trailing_metadata "
              "to propagate internally triggered result";
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
      add_closure_for_subchannel_batch(calld, batch, closures, num_closures);
      pending_batch_clear(calld, pending);
      continue;
    }
    // Create batch with the right number of callbacks.
    const int num_callbacks =
        1 + batch->recv_initial_metadata + batch->recv_message;
    subchannel_batch_data* batch_data = batch_data_create(elem, num_callbacks);
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
      GPR_ASSERT(batch->collect_stats);
      add_retriable_recv_trailing_metadata_op(calld, retry_state, batch_data);
    }
    add_closure_for_subchannel_batch(calld, &batch_data->batch, closures,
                                     num_closures);
  }
}

// Constructs and starts whatever subchannel batches are needed on the
// subchannel call.
static void start_retriable_subchannel_batches(void* arg, grpc_error* ignored) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: constructing retriable batches",
            chand, calld);
  }
  subchannel_call_retry_state* retry_state =
      static_cast<subchannel_call_retry_state*>(
          grpc_connected_subchannel_call_get_parent_data(
              calld->subchannel_call));
  // Construct list of closures to execute, one for each pending batch.
  // We can start up to 6 batches.
  closure_to_execute closures[GPR_ARRAY_SIZE(calld->pending_batches)];
  size_t num_closures = 0;
  // Replay previously-returned send_* ops if needed.
  subchannel_batch_data* replay_batch_data =
      maybe_create_subchannel_batch_for_replay(elem, retry_state);
  if (replay_batch_data != nullptr) {
    add_closure_for_subchannel_batch(calld, &replay_batch_data->batch, closures,
                                     &num_closures);
  }
  // Now add pending batches.
  add_subchannel_batches_for_pending_batches(elem, retry_state, closures,
                                             &num_closures);
  // Start batches on subchannel call.
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting %" PRIuPTR
            " retriable batches on subchannel_call=%p",
            chand, calld, num_closures, calld->subchannel_call);
  }
  execute_closures_in_call_combiner(elem, "start_retriable_subchannel_batches",
                                    closures, num_closures);
}

//
// LB pick
//

static void create_subchannel_call(grpc_call_element* elem, grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  const size_t parent_data_size =
      calld->enable_retries ? sizeof(subchannel_call_retry_state) : 0;
  const grpc_core::ConnectedSubchannel::CallArgs call_args = {
      calld->pollent,                       // pollent
      calld->path,                          // path
      calld->call_start_time,               // start_time
      calld->deadline,                      // deadline
      calld->arena,                         // arena
      calld->pick.subchannel_call_context,  // context
      calld->call_combiner,                 // call_combiner
      parent_data_size                      // parent_data_size
  };
  grpc_error* new_error = calld->pick.connected_subchannel->CreateCall(
      call_args, &calld->subchannel_call);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: create subchannel_call=%p: error=%s",
            chand, calld, calld->subchannel_call, grpc_error_string(new_error));
  }
  if (new_error != GRPC_ERROR_NONE) {
    new_error = grpc_error_add_child(new_error, error);
    pending_batches_fail(elem, new_error, true /* yield_call_combiner */);
  } else {
    if (parent_data_size > 0) {
      subchannel_call_retry_state* retry_state =
          static_cast<subchannel_call_retry_state*>(
              grpc_connected_subchannel_call_get_parent_data(
                  calld->subchannel_call));
      retry_state->batch_payload.context = calld->pick.subchannel_call_context;
    }
    pending_batches_resume(elem);
  }
  GRPC_ERROR_UNREF(error);
}

// Invoked when a pick is completed, on both success or failure.
static void pick_done(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (calld->pick.connected_subchannel == nullptr) {
    // Failed to create subchannel.
    // If there was no error, this is an LB policy drop, in which case
    // we return an error; otherwise, we may retry.
    grpc_status_code status = GRPC_STATUS_OK;
    grpc_error_get_status(error, calld->deadline, &status, nullptr, nullptr,
                          nullptr);
    if (error == GRPC_ERROR_NONE || !calld->enable_retries ||
        !maybe_retry(elem, nullptr /* batch_data */, status,
                     nullptr /* server_pushback_md */)) {
      grpc_error* new_error =
          error == GRPC_ERROR_NONE
              ? GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "Call dropped by load balancing policy")
              : GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                    "Failed to create subchannel", &error, 1);
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: failed to create subchannel: error=%s",
                chand, calld, grpc_error_string(new_error));
      }
      pending_batches_fail(elem, new_error, true /* yield_call_combiner */);
    }
  } else {
    /* Create call on subchannel. */
    create_subchannel_call(elem, GRPC_ERROR_REF(error));
  }
}

// Invoked when a pick is completed to leave the client_channel combiner
// and continue processing in the call combiner.
static void pick_done_locked(grpc_call_element* elem, grpc_error* error) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  GRPC_CLOSURE_INIT(&calld->pick_closure, pick_done, elem,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_SCHED(&calld->pick_closure, error);
}

// A wrapper around pick_done_locked() that is used in cases where
// either (a) the pick was deferred pending a resolver result or (b) the
// pick was done asynchronously.  Removes the call's polling entity from
// chand->interested_parties before invoking pick_done_locked().
static void async_pick_done_locked(grpc_call_element* elem, grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_polling_entity_del_from_pollset_set(calld->pollent,
                                           chand->interested_parties);
  pick_done_locked(elem, error);
}

// Note: This runs under the client_channel combiner, but will NOT be
// holding the call combiner.
static void pick_callback_cancel_locked(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  // Note: chand->lb_policy may have changed since we started our pick,
  // in which case we will be cancelling the pick on a policy other than
  // the one we started it on.  However, this will just be a no-op.
  if (error != GRPC_ERROR_NONE && chand->lb_policy != nullptr) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: cancelling pick from LB policy %p",
              chand, calld, chand->lb_policy.get());
    }
    chand->lb_policy->CancelPickLocked(&calld->pick, GRPC_ERROR_REF(error));
  }
  GRPC_CALL_STACK_UNREF(calld->owning_call, "pick_callback_cancel");
}

// Callback invoked by LoadBalancingPolicy::PickLocked() for async picks.
// Unrefs the LB policy and invokes async_pick_done_locked().
static void pick_callback_done_locked(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: pick completed asynchronously", chand,
            calld);
  }
  async_pick_done_locked(elem, GRPC_ERROR_REF(error));
  GRPC_CALL_STACK_UNREF(calld->owning_call, "pick_callback");
}

// Applies service config to the call.  Must be invoked once we know
// that the resolver has returned results to the channel.
static void apply_service_config_to_call_locked(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
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
    }
  }
  // If no retry policy, disable retries.
  // TODO(roth): Remove this when adding support for transparent retries.
  if (calld->method_params == nullptr ||
      calld->method_params->retry_policy() == nullptr) {
    calld->enable_retries = false;
  }
}

// Starts a pick on chand->lb_policy.
// Returns true if pick is completed synchronously.
static bool pick_callback_start_locked(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: starting pick on lb_policy=%p", chand,
            calld, chand->lb_policy.get());
  }
  // Only get service config data on the first attempt.
  if (calld->num_attempts_completed == 0) {
    apply_service_config_to_call_locked(elem);
  }
  // If the application explicitly set wait_for_ready, use that.
  // Otherwise, if the service config specified a value for this
  // method, use that.
  //
  // The send_initial_metadata batch will be the first one in the list,
  // as set by get_batch_index() above.
  calld->pick.initial_metadata =
      calld->seen_send_initial_metadata
          ? &calld->send_initial_metadata
          : calld->pending_batches[0]
                .batch->payload->send_initial_metadata.send_initial_metadata;
  uint32_t send_initial_metadata_flags =
      calld->seen_send_initial_metadata
          ? calld->send_initial_metadata_flags
          : calld->pending_batches[0]
                .batch->payload->send_initial_metadata
                .send_initial_metadata_flags;
  const bool wait_for_ready_set_from_api =
      send_initial_metadata_flags &
      GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
  const bool wait_for_ready_set_from_service_config =
      calld->method_params != nullptr &&
      calld->method_params->wait_for_ready() !=
          ClientChannelMethodParams::WAIT_FOR_READY_UNSET;
  if (!wait_for_ready_set_from_api && wait_for_ready_set_from_service_config) {
    if (calld->method_params->wait_for_ready() ==
        ClientChannelMethodParams::WAIT_FOR_READY_TRUE) {
      send_initial_metadata_flags |= GRPC_INITIAL_METADATA_WAIT_FOR_READY;
    } else {
      send_initial_metadata_flags &= ~GRPC_INITIAL_METADATA_WAIT_FOR_READY;
    }
  }
  calld->pick.initial_metadata_flags = send_initial_metadata_flags;
  GRPC_CLOSURE_INIT(&calld->pick_closure, pick_callback_done_locked, elem,
                    grpc_combiner_scheduler(chand->combiner));
  calld->pick.on_complete = &calld->pick_closure;
  GRPC_CALL_STACK_REF(calld->owning_call, "pick_callback");
  const bool pick_done = chand->lb_policy->PickLocked(&calld->pick);
  if (pick_done) {
    // Pick completed synchronously.
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: pick completed synchronously",
              chand, calld);
    }
    GRPC_CALL_STACK_UNREF(calld->owning_call, "pick_callback");
  } else {
    GRPC_CALL_STACK_REF(calld->owning_call, "pick_callback_cancel");
    grpc_call_combiner_set_notify_on_cancel(
        calld->call_combiner,
        GRPC_CLOSURE_INIT(&calld->pick_cancel_closure,
                          pick_callback_cancel_locked, elem,
                          grpc_combiner_scheduler(chand->combiner)));
  }
  return pick_done;
}

typedef struct {
  grpc_call_element* elem;
  bool finished;
  grpc_closure closure;
  grpc_closure cancel_closure;
} pick_after_resolver_result_args;

// Note: This runs under the client_channel combiner, but will NOT be
// holding the call combiner.
static void pick_after_resolver_result_cancel_locked(void* arg,
                                                     grpc_error* error) {
  pick_after_resolver_result_args* args =
      static_cast<pick_after_resolver_result_args*>(arg);
  if (args->finished) {
    gpr_free(args);
    return;
  }
  // If we don't yet have a resolver result, then a closure for
  // pick_after_resolver_result_done_locked() will have been added to
  // chand->waiting_for_resolver_result_closures, and it may not be invoked
  // until after this call has been destroyed.  We mark the operation as
  // finished, so that when pick_after_resolver_result_done_locked()
  // is called, it will be a no-op.  We also immediately invoke
  // async_pick_done_locked() to propagate the error back to the caller.
  args->finished = true;
  grpc_call_element* elem = args->elem;
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: cancelling pick waiting for resolver result",
            chand, calld);
  }
  // Note: Although we are not in the call combiner here, we are
  // basically stealing the call combiner from the pending pick, so
  // it's safe to call async_pick_done_locked() here -- we are
  // essentially calling it here instead of calling it in
  // pick_after_resolver_result_done_locked().
  async_pick_done_locked(elem, GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                   "Pick cancelled", &error, 1));
}

static void pick_after_resolver_result_done_locked(void* arg,
                                                   grpc_error* error) {
  pick_after_resolver_result_args* args =
      static_cast<pick_after_resolver_result_args*>(arg);
  if (args->finished) {
    /* cancelled, do nothing */
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "call cancelled before resolver result");
    }
    gpr_free(args);
    return;
  }
  args->finished = true;
  grpc_call_element* elem = args->elem;
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (error != GRPC_ERROR_NONE) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: resolver failed to return data",
              chand, calld);
    }
    async_pick_done_locked(elem, GRPC_ERROR_REF(error));
  } else if (chand->resolver == nullptr) {
    // Shutting down.
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: resolver disconnected", chand,
              calld);
    }
    async_pick_done_locked(
        elem, GRPC_ERROR_CREATE_FROM_STATIC_STRING("Disconnected"));
  } else if (chand->lb_policy == nullptr) {
    // Transient resolver failure.
    // If call has wait_for_ready=true, try again; otherwise, fail.
    uint32_t send_initial_metadata_flags =
        calld->seen_send_initial_metadata
            ? calld->send_initial_metadata_flags
            : calld->pending_batches[0]
                  .batch->payload->send_initial_metadata
                  .send_initial_metadata_flags;
    if (send_initial_metadata_flags & GRPC_INITIAL_METADATA_WAIT_FOR_READY) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: resolver returned but no LB policy; "
                "wait_for_ready=true; trying again",
                chand, calld);
      }
      pick_after_resolver_result_start_locked(elem);
    } else {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: resolver returned but no LB policy; "
                "wait_for_ready=false; failing",
                chand, calld);
      }
      async_pick_done_locked(
          elem,
          grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("Name resolution failure"),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
    }
  } else {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: resolver returned, doing pick",
              chand, calld);
    }
    if (pick_callback_start_locked(elem)) {
      // Even if the LB policy returns a result synchronously, we have
      // already added our polling entity to chand->interested_parties
      // in order to wait for the resolver result, so we need to
      // remove it here.  Therefore, we call async_pick_done_locked()
      // instead of pick_done_locked().
      async_pick_done_locked(elem, GRPC_ERROR_NONE);
    }
  }
}

static void pick_after_resolver_result_start_locked(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: deferring pick pending resolver result", chand,
            calld);
  }
  pick_after_resolver_result_args* args =
      static_cast<pick_after_resolver_result_args*>(gpr_zalloc(sizeof(*args)));
  args->elem = elem;
  GRPC_CLOSURE_INIT(&args->closure, pick_after_resolver_result_done_locked,
                    args, grpc_combiner_scheduler(chand->combiner));
  grpc_closure_list_append(&chand->waiting_for_resolver_result_closures,
                           &args->closure, GRPC_ERROR_NONE);
  grpc_call_combiner_set_notify_on_cancel(
      calld->call_combiner,
      GRPC_CLOSURE_INIT(&args->cancel_closure,
                        pick_after_resolver_result_cancel_locked, args,
                        grpc_combiner_scheduler(chand->combiner)));
}

static void start_pick_locked(void* arg, grpc_error* ignored) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  GPR_ASSERT(calld->pick.connected_subchannel == nullptr);
  GPR_ASSERT(calld->subchannel_call == nullptr);
  if (chand->lb_policy != nullptr) {
    // We already have an LB policy, so ask it for a pick.
    if (pick_callback_start_locked(elem)) {
      // Pick completed synchronously.
      pick_done_locked(elem, GRPC_ERROR_NONE);
      return;
    }
  } else {
    // We do not yet have an LB policy, so wait for a resolver result.
    if (chand->resolver == nullptr) {
      pick_done_locked(elem,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Disconnected"));
      return;
    }
    if (!chand->started_resolving) {
      start_resolving_locked(chand);
    }
    pick_after_resolver_result_start_locked(elem);
  }
  // We need to wait for either a resolver result or for an async result
  // from the LB policy.  Add the polling entity from call_data to the
  // channel_data's interested_parties, so that the I/O of the LB policy
  // and resolver can be done under it.  The polling entity will be
  // removed in async_pick_done_locked().
  grpc_polling_entity_add_to_pollset_set(calld->pollent,
                                         chand->interested_parties);
}

//
// filter call vtable functions
//

static void cc_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_SCOPE("cc_start_transport_stream_op_batch", 0);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (chand->deadline_checking_enabled) {
    grpc_deadline_state_client_start_transport_stream_op_batch(elem, batch);
  }
  // If we've previously been cancelled, immediately fail any new batches.
  if (calld->cancel_error != GRPC_ERROR_NONE) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: failing batch with error: %s",
              chand, calld, grpc_error_string(calld->cancel_error));
    }
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(calld->cancel_error), calld->call_combiner);
    return;
  }
  // Handle cancellation.
  if (batch->cancel_stream) {
    // Stash a copy of cancel_error in our call data, so that we can use
    // it for subsequent operations.  This ensures that if the call is
    // cancelled before any batches are passed down (e.g., if the deadline
    // is in the past when the call starts), we can return the right
    // error to the caller when the first batch does get passed down.
    GRPC_ERROR_UNREF(calld->cancel_error);
    calld->cancel_error =
        GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: recording cancel_error=%s", chand,
              calld, grpc_error_string(calld->cancel_error));
    }
    // If we do not have a subchannel call (i.e., a pick has not yet
    // been started), fail all pending batches.  Otherwise, send the
    // cancellation down to the subchannel call.
    if (calld->subchannel_call == nullptr) {
      pending_batches_fail(elem, GRPC_ERROR_REF(calld->cancel_error),
                           false /* yield_call_combiner */);
      // Note: This will release the call combiner.
      grpc_transport_stream_op_batch_finish_with_failure(
          batch, GRPC_ERROR_REF(calld->cancel_error), calld->call_combiner);
    } else {
      // Note: This will release the call combiner.
      grpc_subchannel_call_process_op(calld->subchannel_call, batch);
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
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: starting batch on subchannel_call=%p", chand,
              calld, calld->subchannel_call);
    }
    pending_batches_resume(elem);
    return;
  }
  // We do not yet have a subchannel call.
  // For batches containing a send_initial_metadata op, enter the channel
  // combiner to start a pick.
  if (batch->send_initial_metadata) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: entering client_channel combiner",
              chand, calld);
    }
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_INIT(&batch->handler_private.closure, start_pick_locked,
                          elem, grpc_combiner_scheduler(chand->combiner)),
        GRPC_ERROR_NONE);
  } else {
    // For all other batches, release the call combiner.
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: saved batch, yeilding call combiner", chand,
              calld);
    }
    GRPC_CALL_COMBINER_STOP(calld->call_combiner,
                            "batch does not include send_initial_metadata");
  }
}

/* Constructor for call_data */
static grpc_error* cc_init_call_elem(grpc_call_element* elem,
                                     const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  // Initialize data members.
  calld->path = grpc_slice_ref_internal(args->path);
  calld->call_start_time = args->start_time;
  calld->deadline = args->deadline;
  calld->arena = args->arena;
  calld->owning_call = args->call_stack;
  calld->call_combiner = args->call_combiner;
  if (chand->deadline_checking_enabled) {
    grpc_deadline_state_init(elem, args->call_stack, args->call_combiner,
                             calld->deadline);
  }
  calld->enable_retries = chand->enable_retries;
  calld->send_messages.Init();
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void cc_destroy_call_elem(grpc_call_element* elem,
                                 const grpc_call_final_info* final_info,
                                 grpc_closure* then_schedule_closure) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (chand->deadline_checking_enabled) {
    grpc_deadline_state_destroy(elem);
  }
  grpc_slice_unref_internal(calld->path);
  calld->retry_throttle_data.reset();
  calld->method_params.reset();
  GRPC_ERROR_UNREF(calld->cancel_error);
  if (calld->subchannel_call != nullptr) {
    grpc_subchannel_call_set_cleanup_closure(calld->subchannel_call,
                                             then_schedule_closure);
    then_schedule_closure = nullptr;
    GRPC_SUBCHANNEL_CALL_UNREF(calld->subchannel_call,
                               "client_channel_destroy_call");
  }
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    GPR_ASSERT(calld->pending_batches[i].batch == nullptr);
  }
  if (calld->pick.connected_subchannel != nullptr) {
    calld->pick.connected_subchannel.reset();
  }
  for (size_t i = 0; i < GRPC_CONTEXT_COUNT; ++i) {
    if (calld->pick.subchannel_call_context[i].value != nullptr) {
      calld->pick.subchannel_call_context[i].destroy(
          calld->pick.subchannel_call_context[i].value);
    }
  }
  calld->send_messages.Destroy();
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

static void try_to_connect_locked(void* arg, grpc_error* error_ignored) {
  channel_data* chand = static_cast<channel_data*>(arg);
  if (chand->lb_policy != nullptr) {
    chand->lb_policy->ExitIdleLocked();
  } else {
    chand->exit_idle_when_lb_policy_arrives = true;
    if (!chand->started_resolving && chand->resolver != nullptr) {
      start_resolving_locked(chand);
    }
  }
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
    channel_data* chand, external_connectivity_watcher* too_remove) {
  GPR_ASSERT(
      lookup_external_connectivity_watcher(chand, too_remove->on_complete));
  gpr_mu_lock(&chand->external_connectivity_watcher_list_mu);
  if (too_remove == chand->external_connectivity_watcher_list_head) {
    chand->external_connectivity_watcher_list_head = too_remove->next;
    gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);
    return;
  }
  external_connectivity_watcher* w =
      chand->external_connectivity_watcher_list_head;
  while (w != nullptr) {
    if (w->next == too_remove) {
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
  GRPC_CLOSURE_RUN(follow_up, GRPC_ERROR_REF(error));
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

grpc_subchannel_call* grpc_client_channel_get_subchannel_call(
    grpc_call_element* elem) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  return calld->subchannel_call;
}
