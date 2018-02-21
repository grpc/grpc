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
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/service_config.h"
#include "src/core/lib/transport/static_metadata.h"

/* Client channel implementation */

grpc_core::TraceFlag grpc_client_channel_trace(false, "client_channel");

/*************************************************************************
 * METHOD-CONFIG TABLE
 */

typedef enum {
  /* zero so it can be default initialized */
  WAIT_FOR_READY_UNSET = 0,
  WAIT_FOR_READY_FALSE,
  WAIT_FOR_READY_TRUE
} wait_for_ready_value;

typedef struct {
  gpr_refcount refs;
  grpc_millis timeout;
  wait_for_ready_value wait_for_ready;
} method_parameters;

static method_parameters* method_parameters_ref(
    method_parameters* method_params) {
  gpr_ref(&method_params->refs);
  return method_params;
}

static void method_parameters_unref(method_parameters* method_params) {
  if (gpr_unref(&method_params->refs)) {
    gpr_free(method_params);
  }
}

// Wrappers to pass to grpc_service_config_create_method_config_table().
static void* method_parameters_ref_wrapper(void* value) {
  return method_parameters_ref(static_cast<method_parameters*>(value));
}
static void method_parameters_unref_wrapper(void* value) {
  method_parameters_unref(static_cast<method_parameters*>(value));
}

static bool parse_wait_for_ready(grpc_json* field,
                                 wait_for_ready_value* wait_for_ready) {
  if (field->type != GRPC_JSON_TRUE && field->type != GRPC_JSON_FALSE) {
    return false;
  }
  *wait_for_ready = field->type == GRPC_JSON_TRUE ? WAIT_FOR_READY_TRUE
                                                  : WAIT_FOR_READY_FALSE;
  return true;
}

static bool parse_timeout(grpc_json* field, grpc_millis* timeout) {
  if (field->type != GRPC_JSON_STRING) return false;
  size_t len = strlen(field->value);
  if (field->value[len - 1] != 's') return false;
  char* buf = gpr_strdup(field->value);
  buf[len - 1] = '\0';  // Remove trailing 's'.
  char* decimal_point = strchr(buf, '.');
  int nanos = 0;
  if (decimal_point != nullptr) {
    *decimal_point = '\0';
    nanos = gpr_parse_nonnegative_int(decimal_point + 1);
    if (nanos == -1) {
      gpr_free(buf);
      return false;
    }
    int num_digits = static_cast<int>(strlen(decimal_point + 1));
    if (num_digits > 9) {  // We don't accept greater precision than nanos.
      gpr_free(buf);
      return false;
    }
    for (int i = 0; i < (9 - num_digits); ++i) {
      nanos *= 10;
    }
  }
  int seconds = decimal_point == buf ? 0 : gpr_parse_nonnegative_int(buf);
  gpr_free(buf);
  if (seconds == -1) return false;
  *timeout = seconds * GPR_MS_PER_SEC + nanos / GPR_NS_PER_MS;
  return true;
}

static void* method_parameters_create_from_json(const grpc_json* json) {
  wait_for_ready_value wait_for_ready = WAIT_FOR_READY_UNSET;
  grpc_millis timeout = 0;
  for (grpc_json* field = json->child; field != nullptr; field = field->next) {
    if (field->key == nullptr) continue;
    if (strcmp(field->key, "waitForReady") == 0) {
      if (wait_for_ready != WAIT_FOR_READY_UNSET) return nullptr;  // Duplicate.
      if (!parse_wait_for_ready(field, &wait_for_ready)) return nullptr;
    } else if (strcmp(field->key, "timeout") == 0) {
      if (timeout > 0) return nullptr;  // Duplicate.
      if (!parse_timeout(field, &timeout)) return nullptr;
    }
  }
  method_parameters* value =
      static_cast<method_parameters*>(gpr_malloc(sizeof(method_parameters)));
  gpr_ref_init(&value->refs, 1);
  value->timeout = timeout;
  value->wait_for_ready = wait_for_ready;
  return value;
}

struct external_connectivity_watcher;

/*************************************************************************
 * CHANNEL-WIDE FUNCTIONS
 */

typedef struct client_channel_channel_data {
  /** resolver for this channel */
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver;
  /** have we started resolving this channel */
  bool started_resolving;
  /** is deadline checking enabled? */
  bool deadline_checking_enabled;
  /** client channel factory */
  grpc_client_channel_factory* client_channel_factory;

  /** combiner protecting all variables below in this data structure */
  grpc_combiner* combiner;
  /** currently active load balancer */
  grpc_core::OrphanablePtr<grpc_core::LoadBalancingPolicy> lb_policy;
  /** retry throttle data */
  grpc_server_retry_throttle_data* retry_throttle_data;
  /** maps method names to method_parameters structs */
  grpc_slice_hash_table* method_params_table;
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

  /* the following properties are guarded by a mutex since API's require them
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
    gpr_log(GPR_DEBUG, "chand=%p: setting connectivity state to %s", chand,
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
      gpr_log(GPR_DEBUG, "chand=%p: lb_policy=%p state changed to %s", w->chand,
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
    gpr_log(GPR_DEBUG, "chand=%p: starting name resolution", chand);
  }
  GPR_ASSERT(!chand->started_resolving);
  chand->started_resolving = true;
  GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
  chand->resolver->NextLocked(&chand->resolver_result,
                              &chand->on_resolver_result_changed);
}

typedef struct {
  char* server_name;
  grpc_server_retry_throttle_data* retry_throttle_data;
} service_config_parsing_state;

static void parse_retry_throttle_params(const grpc_json* field, void* arg) {
  service_config_parsing_state* parsing_state =
      static_cast<service_config_parsing_state*>(arg);
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
        grpc_retry_throttle_map_get_data_for_server(
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
    gpr_log(GPR_DEBUG, "chand=%p: started name re-resolving", chand);
  }
  chand->resolver->RequestReresolutionLocked();
  // Give back the closure to the LB policy.
  chand->lb_policy->SetReresolutionClosureLocked(&args->closure);
}

static void on_resolver_result_changed_locked(void* arg, grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(arg);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_DEBUG, "chand=%p: got resolver result: error=%s", chand,
            grpc_error_string(error));
  }
  // Extract the following fields from the resolver result, if non-NULL.
  bool lb_policy_updated = false;
  bool lb_policy_created = false;
  char* lb_policy_name_dup = nullptr;
  bool lb_policy_name_changed = false;
  grpc_core::OrphanablePtr<grpc_core::LoadBalancingPolicy> new_lb_policy;
  char* service_config_json = nullptr;
  grpc_server_retry_throttle_data* retry_throttle_data = nullptr;
  grpc_slice_hash_table* method_params_table = nullptr;
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
      // Find service config.
      channel_arg = grpc_channel_args_find(chand->resolver_result,
                                           GRPC_ARG_SERVICE_CONFIG);
      service_config_json =
          gpr_strdup(grpc_channel_arg_get_string(channel_arg));
      if (service_config_json != nullptr) {
        grpc_service_config* service_config =
            grpc_service_config_create(service_config_json);
        if (service_config != nullptr) {
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
          grpc_service_config_parse_global_params(
              service_config, parse_retry_throttle_params, &parsing_state);
          grpc_uri_destroy(uri);
          retry_throttle_data = parsing_state.retry_throttle_data;
          method_params_table = grpc_service_config_create_method_config_table(
              service_config, method_parameters_create_from_json,
              method_parameters_ref_wrapper, method_parameters_unref_wrapper);
          grpc_service_config_destroy(service_config);
        }
      }
      // Before we clean up, save a copy of lb_policy_name, since it might
      // be pointing to data inside chand->resolver_result.
      // The copy will be saved in chand->lb_policy_name below.
      lb_policy_name_dup = gpr_strdup(lb_policy_name);
    }
    grpc_channel_args_destroy(chand->resolver_result);
    chand->resolver_result = nullptr;
  }
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "chand=%p: resolver result: lb_policy_name=\"%s\"%s, "
            "service_config=\"%s\"",
            chand, lb_policy_name_dup,
            lb_policy_name_changed ? " (changed)" : "", service_config_json);
  }
  // Now swap out fields in chand.  Note that the new values may still
  // be NULL if (e.g.) the resolver failed to return results or the
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
  if (chand->retry_throttle_data != nullptr) {
    grpc_server_retry_throttle_data_unref(chand->retry_throttle_data);
  }
  chand->retry_throttle_data = retry_throttle_data;
  // Swap out the method params table.
  if (chand->method_params_table != nullptr) {
    grpc_slice_hash_table_unref(chand->method_params_table);
  }
  chand->method_params_table = method_params_table;
  // If we have a new LB policy or are shutting down (in which case
  // new_lb_policy will be NULL), swap out the LB policy, unreffing the old one
  // and removing its fds from chand->interested_parties. Note that we do NOT do
  // this if either (a) we updated the existing LB policy above or (b) we failed
  // to create the new LB policy (in which case we want to continue using the
  // most recent one we had).
  if (new_lb_policy != nullptr || error != GRPC_ERROR_NONE ||
      chand->resolver == nullptr) {
    if (chand->lb_policy != nullptr) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_DEBUG, "chand=%p: unreffing lb_policy=%p", chand,
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
      gpr_log(GPR_DEBUG, "chand=%p: shutting down", chand);
    }
    if (chand->resolver != nullptr) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_DEBUG, "chand=%p: shutting down resolver", chand);
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
  } else {  // Not shutting down.
    grpc_connectivity_state state = GRPC_CHANNEL_TRANSIENT_FAILURE;
    grpc_error* state_error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("No load balancing policy");
    if (lb_policy_created) {
      if (grpc_client_channel_trace.enabled()) {
        gpr_log(GPR_DEBUG, "chand=%p: initializing new LB policy", chand);
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
    }
    if (!lb_policy_updated) {
      set_channel_connectivity_state_locked(
          chand, state, GRPC_ERROR_REF(state_error), "new_lb+resolver");
    }
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
  // Record client channel factory.
  const grpc_arg* arg = grpc_channel_args_find(args->channel_args,
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
  if (chand->retry_throttle_data != nullptr) {
    grpc_server_retry_throttle_data_unref(chand->retry_throttle_data);
  }
  if (chand->method_params_table != nullptr) {
    grpc_slice_hash_table_unref(chand->method_params_table);
  }
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
// time.  This includes:
//   recv_initial_metadata
//   send_initial_metadata
//   recv_message
//   send_message
//   recv_trailing_metadata
//   send_trailing_metadata
// We also add room for a single cancel_stream batch.
#define MAX_WAITING_BATCHES 7

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

  grpc_server_retry_throttle_data* retry_throttle_data;
  method_parameters* method_params;

  grpc_subchannel_call* subchannel_call;
  grpc_error* error;

  grpc_core::LoadBalancingPolicy::PickState pick;
  grpc_closure lb_pick_closure;
  grpc_closure lb_pick_cancel_closure;

  grpc_polling_entity* pollent;

  grpc_transport_stream_op_batch* waiting_for_pick_batches[MAX_WAITING_BATCHES];
  size_t waiting_for_pick_batches_count;
  grpc_closure handle_pending_batch_in_call_combiner[MAX_WAITING_BATCHES];

  grpc_transport_stream_op_batch* initial_metadata_batch;

  grpc_closure on_complete;
  grpc_closure* original_on_complete;
} call_data;

grpc_subchannel_call* grpc_client_channel_get_subchannel_call(
    grpc_call_element* elem) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  return calld->subchannel_call;
}

// This is called via the call combiner, so access to calld is synchronized.
static void waiting_for_pick_batches_add(
    call_data* calld, grpc_transport_stream_op_batch* batch) {
  if (batch->send_initial_metadata) {
    GPR_ASSERT(calld->initial_metadata_batch == nullptr);
    calld->initial_metadata_batch = batch;
  } else {
    GPR_ASSERT(calld->waiting_for_pick_batches_count < MAX_WAITING_BATCHES);
    calld->waiting_for_pick_batches[calld->waiting_for_pick_batches_count++] =
        batch;
  }
}

// This is called via the call combiner, so access to calld is synchronized.
static void fail_pending_batch_in_call_combiner(void* arg, grpc_error* error) {
  call_data* calld = static_cast<call_data*>(arg);
  if (calld->waiting_for_pick_batches_count > 0) {
    --calld->waiting_for_pick_batches_count;
    grpc_transport_stream_op_batch_finish_with_failure(
        calld->waiting_for_pick_batches[calld->waiting_for_pick_batches_count],
        GRPC_ERROR_REF(error), calld->call_combiner);
  }
}

// This is called via the call combiner, so access to calld is synchronized.
static void waiting_for_pick_batches_fail(grpc_call_element* elem,
                                          grpc_error* error) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "chand=%p calld=%p: failing %" PRIuPTR " pending batches: %s",
            elem->channel_data, calld, calld->waiting_for_pick_batches_count,
            grpc_error_string(error));
  }
  for (size_t i = 0; i < calld->waiting_for_pick_batches_count; ++i) {
    GRPC_CLOSURE_INIT(&calld->handle_pending_batch_in_call_combiner[i],
                      fail_pending_batch_in_call_combiner, calld,
                      grpc_schedule_on_exec_ctx);
    GRPC_CALL_COMBINER_START(
        calld->call_combiner, &calld->handle_pending_batch_in_call_combiner[i],
        GRPC_ERROR_REF(error), "waiting_for_pick_batches_fail");
  }
  if (calld->initial_metadata_batch != nullptr) {
    grpc_transport_stream_op_batch_finish_with_failure(
        calld->initial_metadata_batch, GRPC_ERROR_REF(error),
        calld->call_combiner);
  } else {
    GRPC_CALL_COMBINER_STOP(calld->call_combiner,
                            "waiting_for_pick_batches_fail");
  }
  GRPC_ERROR_UNREF(error);
}

// This is called via the call combiner, so access to calld is synchronized.
static void run_pending_batch_in_call_combiner(void* arg, grpc_error* ignored) {
  call_data* calld = static_cast<call_data*>(arg);
  if (calld->waiting_for_pick_batches_count > 0) {
    --calld->waiting_for_pick_batches_count;
    grpc_subchannel_call_process_op(
        calld->subchannel_call,
        calld->waiting_for_pick_batches[calld->waiting_for_pick_batches_count]);
  }
}

// This is called via the call combiner, so access to calld is synchronized.
static void waiting_for_pick_batches_resume(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "chand=%p calld=%p: sending %" PRIuPTR
            " pending batches to subchannel_call=%p",
            chand, calld, calld->waiting_for_pick_batches_count,
            calld->subchannel_call);
  }
  for (size_t i = 0; i < calld->waiting_for_pick_batches_count; ++i) {
    GRPC_CLOSURE_INIT(&calld->handle_pending_batch_in_call_combiner[i],
                      run_pending_batch_in_call_combiner, calld,
                      grpc_schedule_on_exec_ctx);
    GRPC_CALL_COMBINER_START(
        calld->call_combiner, &calld->handle_pending_batch_in_call_combiner[i],
        GRPC_ERROR_NONE, "waiting_for_pick_batches_resume");
  }
  GPR_ASSERT(calld->initial_metadata_batch != nullptr);
  grpc_subchannel_call_process_op(calld->subchannel_call,
                                  calld->initial_metadata_batch);
}

// Applies service config to the call.  Must be invoked once we know
// that the resolver has returned results to the channel.
static void apply_service_config_to_call_locked(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_DEBUG, "chand=%p calld=%p: applying service config to call",
            chand, calld);
  }
  if (chand->retry_throttle_data != nullptr) {
    calld->retry_throttle_data =
        grpc_server_retry_throttle_data_ref(chand->retry_throttle_data);
  }
  if (chand->method_params_table != nullptr) {
    calld->method_params = static_cast<method_parameters*>(
        grpc_method_config_table_get(chand->method_params_table, calld->path));
    if (calld->method_params != nullptr) {
      method_parameters_ref(calld->method_params);
      // If the deadline from the service config is shorter than the one
      // from the client API, reset the deadline timer.
      if (chand->deadline_checking_enabled &&
          calld->method_params->timeout != 0) {
        const grpc_millis per_method_deadline =
            grpc_timespec_to_millis_round_up(calld->call_start_time) +
            calld->method_params->timeout;
        if (per_method_deadline < calld->deadline) {
          calld->deadline = per_method_deadline;
          grpc_deadline_state_reset(elem, calld->deadline);
        }
      }
    }
  }
}

static void create_subchannel_call_locked(grpc_call_element* elem,
                                          grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  const grpc_core::ConnectedSubchannel::CallArgs call_args = {
      calld->pollent,                       // pollent
      calld->path,                          // path
      calld->call_start_time,               // start_time
      calld->deadline,                      // deadline
      calld->arena,                         // arena
      calld->pick.subchannel_call_context,  // context
      calld->call_combiner                  // call_combiner
  };
  grpc_error* new_error = calld->pick.connected_subchannel->CreateCall(
      call_args, &calld->subchannel_call);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_DEBUG, "chand=%p calld=%p: create subchannel_call=%p: error=%s",
            chand, calld, calld->subchannel_call, grpc_error_string(new_error));
  }
  if (new_error != GRPC_ERROR_NONE) {
    new_error = grpc_error_add_child(new_error, error);
    waiting_for_pick_batches_fail(elem, new_error);
  } else {
    waiting_for_pick_batches_resume(elem);
  }
  GRPC_ERROR_UNREF(error);
}

// Invoked when a pick is completed, on both success or failure.
static void pick_done_locked(grpc_call_element* elem, grpc_error* error) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (calld->pick.connected_subchannel == nullptr) {
    // Failed to create subchannel.
    GRPC_ERROR_UNREF(calld->error);
    calld->error = error == GRPC_ERROR_NONE
                       ? GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                             "Call dropped by load balancing policy")
                       : GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Failed to create subchannel", &error, 1);
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "chand=%p calld=%p: failed to create subchannel: error=%s", chand,
              calld, grpc_error_string(calld->error));
    }
    waiting_for_pick_batches_fail(elem, GRPC_ERROR_REF(calld->error));
  } else {
    /* Create call on subchannel. */
    create_subchannel_call_locked(elem, GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
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
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: cancelling pick from LB policy %p",
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
    gpr_log(GPR_DEBUG, "chand=%p calld=%p: pick completed asynchronously",
            chand, calld);
  }
  async_pick_done_locked(elem, GRPC_ERROR_REF(error));
  GRPC_CALL_STACK_UNREF(calld->owning_call, "pick_callback");
}

// Starts a pick on chand->lb_policy.
// Returns true if pick is completed synchronously.
static bool pick_callback_start_locked(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_DEBUG, "chand=%p calld=%p: starting pick on lb_policy=%p",
            chand, calld, chand->lb_policy.get());
  }
  apply_service_config_to_call_locked(elem);
  // If the application explicitly set wait_for_ready, use that.
  // Otherwise, if the service config specified a value for this
  // method, use that.
  uint32_t initial_metadata_flags =
      calld->initial_metadata_batch->payload->send_initial_metadata
          .send_initial_metadata_flags;
  const bool wait_for_ready_set_from_api =
      initial_metadata_flags &
      GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
  const bool wait_for_ready_set_from_service_config =
      calld->method_params != nullptr &&
      calld->method_params->wait_for_ready != WAIT_FOR_READY_UNSET;
  if (!wait_for_ready_set_from_api && wait_for_ready_set_from_service_config) {
    if (calld->method_params->wait_for_ready == WAIT_FOR_READY_TRUE) {
      initial_metadata_flags |= GRPC_INITIAL_METADATA_WAIT_FOR_READY;
    } else {
      initial_metadata_flags &= ~GRPC_INITIAL_METADATA_WAIT_FOR_READY;
    }
  }
  calld->pick.initial_metadata =
      calld->initial_metadata_batch->payload->send_initial_metadata
          .send_initial_metadata;
  calld->pick.initial_metadata_flags = initial_metadata_flags;
  GRPC_CLOSURE_INIT(&calld->lb_pick_closure, pick_callback_done_locked, elem,
                    grpc_combiner_scheduler(chand->combiner));
  calld->pick.on_complete = &calld->lb_pick_closure;
  GRPC_CALL_STACK_REF(calld->owning_call, "pick_callback");
  const bool pick_done = chand->lb_policy->PickLocked(&calld->pick);
  if (pick_done) {
    // Pick completed synchronously.
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: pick completed synchronously",
              chand, calld);
    }
    GRPC_CALL_STACK_UNREF(calld->owning_call, "pick_callback");
  } else {
    GRPC_CALL_STACK_REF(calld->owning_call, "pick_callback_cancel");
    grpc_call_combiner_set_notify_on_cancel(
        calld->call_combiner,
        GRPC_CLOSURE_INIT(&calld->lb_pick_cancel_closure,
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
    gpr_log(GPR_DEBUG,
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

static void pick_after_resolver_result_start_locked(grpc_call_element* elem);

static void pick_after_resolver_result_done_locked(void* arg,
                                                   grpc_error* error) {
  pick_after_resolver_result_args* args =
      static_cast<pick_after_resolver_result_args*>(arg);
  if (args->finished) {
    /* cancelled, do nothing */
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_DEBUG, "call cancelled before resolver result");
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
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: resolver failed to return data",
              chand, calld);
    }
    async_pick_done_locked(elem, GRPC_ERROR_REF(error));
  } else if (chand->lb_policy != nullptr) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: resolver returned, doing pick",
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
  // TODO(roth): It should be impossible for chand->lb_policy to be NULL
  // here, so the rest of this code should never actually be executed.
  // However, we have reports of a crash on iOS that triggers this case,
  // so we are temporarily adding this to restore branches that were
  // removed in https://github.com/grpc/grpc/pull/12297.  Need to figure
  // out what is actually causing this to occur and then figure out the
  // right way to deal with it.
  else if (chand->resolver != nullptr) {
    // No LB policy, so try again.
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "chand=%p calld=%p: resolver returned but no LB policy, "
              "trying again",
              chand, calld);
    }
    pick_after_resolver_result_start_locked(elem);
  } else {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: resolver disconnected", chand,
              calld);
    }
    async_pick_done_locked(
        elem, GRPC_ERROR_CREATE_FROM_STATIC_STRING("Disconnected"));
  }
}

static void pick_after_resolver_result_start_locked(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (grpc_client_channel_trace.enabled()) {
    gpr_log(GPR_DEBUG,
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

static void on_complete(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (calld->retry_throttle_data != nullptr) {
    if (error == GRPC_ERROR_NONE) {
      grpc_server_retry_throttle_data_record_success(
          calld->retry_throttle_data);
    } else {
      // TODO(roth): In a subsequent PR, check the return value here and
      // decide whether or not to retry.  Note that we should only
      // record failures whose statuses match the configured retryable
      // or non-fatal status codes.
      grpc_server_retry_throttle_data_record_failure(
          calld->retry_throttle_data);
    }
  }
  GRPC_CLOSURE_RUN(calld->original_on_complete, GRPC_ERROR_REF(error));
}

static void cc_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_SCOPE("cc_start_transport_stream_op_batch", 0);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (chand->deadline_checking_enabled) {
    grpc_deadline_state_client_start_transport_stream_op_batch(elem, batch);
  }
  // If we've previously been cancelled, immediately fail any new batches.
  if (calld->error != GRPC_ERROR_NONE) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: failing batch with error: %s",
              chand, calld, grpc_error_string(calld->error));
    }
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(calld->error), calld->call_combiner);
    return;
  }
  if (batch->cancel_stream) {
    // Stash a copy of cancel_error in our call data, so that we can use
    // it for subsequent operations.  This ensures that if the call is
    // cancelled before any batches are passed down (e.g., if the deadline
    // is in the past when the call starts), we can return the right
    // error to the caller when the first batch does get passed down.
    GRPC_ERROR_UNREF(calld->error);
    calld->error = GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: recording cancel_error=%s", chand,
              calld, grpc_error_string(calld->error));
    }
    // If we have a subchannel call, send the cancellation batch down.
    // Otherwise, fail all pending batches.
    if (calld->subchannel_call != nullptr) {
      grpc_subchannel_call_process_op(calld->subchannel_call, batch);
    } else {
      waiting_for_pick_batches_add(calld, batch);
      waiting_for_pick_batches_fail(elem, GRPC_ERROR_REF(calld->error));
    }
    return;
  }
  // Intercept on_complete for recv_trailing_metadata so that we can
  // check retry throttle status.
  if (batch->recv_trailing_metadata) {
    GPR_ASSERT(batch->on_complete != nullptr);
    calld->original_on_complete = batch->on_complete;
    GRPC_CLOSURE_INIT(&calld->on_complete, on_complete, elem,
                      grpc_schedule_on_exec_ctx);
    batch->on_complete = &calld->on_complete;
  }
  // Check if we've already gotten a subchannel call.
  // Note that once we have completed the pick, we do not need to enter
  // the channel combiner, which is more efficient (especially for
  // streaming calls).
  if (calld->subchannel_call != nullptr) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "chand=%p calld=%p: sending batch to subchannel_call=%p", chand,
              calld, calld->subchannel_call);
    }
    grpc_subchannel_call_process_op(calld->subchannel_call, batch);
    return;
  }
  // We do not yet have a subchannel call.
  // Add the batch to the waiting-for-pick list.
  waiting_for_pick_batches_add(calld, batch);
  // For batches containing a send_initial_metadata op, enter the channel
  // combiner to start a pick.
  if (batch->send_initial_metadata) {
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: entering client_channel combiner",
              chand, calld);
    }
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_INIT(&batch->handler_private.closure, start_pick_locked,
                          elem, grpc_combiner_scheduler(chand->combiner)),
        GRPC_ERROR_NONE);
  } else {
    // For all other batches, release the call combiner.
    if (grpc_client_channel_trace.enabled()) {
      gpr_log(GPR_DEBUG,
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
  if (calld->method_params != nullptr) {
    method_parameters_unref(calld->method_params);
  }
  GRPC_ERROR_UNREF(calld->error);
  if (calld->subchannel_call != nullptr) {
    grpc_subchannel_call_set_cleanup_closure(calld->subchannel_call,
                                             then_schedule_closure);
    then_schedule_closure = nullptr;
    GRPC_SUBCHANNEL_CALL_UNREF(calld->subchannel_call,
                               "client_channel_destroy_call");
  }
  GPR_ASSERT(calld->waiting_for_pick_batches_count == 0);
  if (calld->pick.connected_subchannel != nullptr) {
    calld->pick.connected_subchannel.reset();
  }
  for (size_t i = 0; i < GRPC_CONTEXT_COUNT; ++i) {
    if (calld->pick.subchannel_call_context[i].value != nullptr) {
      calld->pick.subchannel_call_context[i].destroy(
          calld->pick.subchannel_call_context[i].value);
    }
  }
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
