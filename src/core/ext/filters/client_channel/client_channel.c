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

#include "src/core/ext/filters/client_channel/client_channel.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/status_string.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/support/backoff.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/service_config.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_metadata.h"

/* Client channel implementation */

// FIXME: what's the right default for this?
#define DEFAULT_PER_RPC_RETRY_BUFFER_SIZE (1<<30)

// FIXME: what's the right value for this?
#define RETRY_BACKOFF_JITTER 0.2

grpc_tracer_flag grpc_client_channel_trace =
    GRPC_TRACER_INITIALIZER(false, "client_channel");

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
  int max_retry_attempts;
  int initial_backoff_ms;
  int max_backoff_ms;
  int backoff_multiplier;
  grpc_status_code *retryable_status_codes;
  size_t num_retryable_status_codes;
} retry_policy_params;

typedef struct {
  gpr_refcount refs;
  gpr_timespec timeout;
  wait_for_ready_value wait_for_ready;
  retry_policy_params *retry_policy;
} method_parameters;

static method_parameters *method_parameters_ref(
    method_parameters *method_params) {
  gpr_ref(&method_params->refs);
  return method_params;
}

static void method_parameters_unref(method_parameters *method_params) {
  if (gpr_unref(&method_params->refs)) {
    if (method_params->retry_policy != NULL) {
      gpr_free(method_params->retry_policy->retryable_status_codes);
    }
    gpr_free(method_params->retry_policy);
    gpr_free(method_params);
  }
}

static void method_parameters_free(grpc_exec_ctx *exec_ctx, void *value) {
  method_parameters_unref(value);
}

static bool parse_wait_for_ready(grpc_json *field,
                                 wait_for_ready_value *wait_for_ready) {
  if (field->type != GRPC_JSON_TRUE && field->type != GRPC_JSON_FALSE) {
    return false;
  }
  *wait_for_ready = field->type == GRPC_JSON_TRUE ? WAIT_FOR_READY_TRUE
                                                  : WAIT_FOR_READY_FALSE;
  return true;
}

static bool parse_timeout(grpc_json *field, gpr_timespec *timeout) {
  if (field->type != GRPC_JSON_STRING) return false;
  size_t len = strlen(field->value);
  if (field->value[len - 1] != 's') return false;
  char *buf = gpr_strdup(field->value);
  buf[len - 1] = '\0';  // Remove trailing 's'.
  char *decimal_point = strchr(buf, '.');
  if (decimal_point != NULL) {
    *decimal_point = '\0';
    timeout->tv_nsec = gpr_parse_nonnegative_int(decimal_point + 1);
    if (timeout->tv_nsec == -1) {
      gpr_free(buf);
      return false;
    }
    // There should always be exactly 3, 6, or 9 fractional digits.
    int multiplier = 1;
    switch (strlen(decimal_point + 1)) {
      case 9:
        break;
      case 6:
        multiplier *= 1000;
        break;
      case 3:
        multiplier *= 1000000;
        break;
      default:  // Unsupported number of digits.
        gpr_free(buf);
        return false;
    }
    timeout->tv_nsec *= multiplier;
  }
  timeout->tv_sec = gpr_parse_nonnegative_int(buf);
  gpr_free(buf);
  if (timeout->tv_sec == -1) return false;
  return true;
}

static bool parse_retry_policy(grpc_json *field,
                               retry_policy_params* retry_policy) {
  if (field->type != GRPC_JSON_OBJECT) return false;
  for (grpc_json *sub_field = field->child; sub_field != NULL;
       sub_field = sub_field->next) {
    if (sub_field->key == NULL) return false;
    if (strcmp(sub_field->key, "maxRetryAttempts") == 0) {
      if (retry_policy->max_retry_attempts != 0) return false;  // Duplicate.
      if (sub_field->type != GRPC_JSON_NUMBER) return false;
      retry_policy->max_retry_attempts =
          gpr_parse_nonnegative_int(sub_field->value);
      if (retry_policy->max_retry_attempts <= 0) return false;
    } else if (strcmp(sub_field->key, "initialBackoffMs") == 0) {
      if (retry_policy->initial_backoff_ms != 0) return false;  // Duplicate.
      if (sub_field->type != GRPC_JSON_NUMBER) return false;
      retry_policy->initial_backoff_ms =
          gpr_parse_nonnegative_int(sub_field->value);
      if (retry_policy->initial_backoff_ms <= 0) return false;
    } else if (strcmp(sub_field->key, "maxBackoffMs") == 0) {
      if (retry_policy->max_backoff_ms != 0) return false;  // Duplicate.
      if (sub_field->type != GRPC_JSON_NUMBER) return false;
      retry_policy->max_backoff_ms =
          gpr_parse_nonnegative_int(sub_field->value);
      if (retry_policy->max_backoff_ms <= 0) return false;
    } else if (strcmp(sub_field->key, "retryableStatusCodes") == 0) {
      if (retry_policy->retryable_status_codes != NULL) {
        return false;  // Duplicate.
      }
      if (sub_field->type != GRPC_JSON_ARRAY) return false;
      for (grpc_json *element = sub_field->child; element != NULL;
           element = element->next) {
        if (element->type != GRPC_JSON_STRING) return false;
        ++retry_policy->num_retryable_status_codes;
        retry_policy->retryable_status_codes =
            gpr_realloc(retry_policy->retryable_status_codes,
                        retry_policy->num_retryable_status_codes *
                        sizeof(grpc_status_code));
        if (!grpc_status_from_string(
                element->value,
                &retry_policy->retryable_status_codes[
                    retry_policy->num_retryable_status_codes - 1])) {
          return false;
        }
      }
    }
  }
  return true;
}

static void *method_parameters_create_from_json(const grpc_json *json) {
  wait_for_ready_value wait_for_ready = WAIT_FOR_READY_UNSET;
  gpr_timespec timeout = {0, 0, GPR_TIMESPAN};
  retry_policy_params *retry_policy = NULL;
  for (grpc_json *field = json->child; field != NULL; field = field->next) {
    if (field->key == NULL) continue;
    if (strcmp(field->key, "waitForReady") == 0) {
      if (wait_for_ready != WAIT_FOR_READY_UNSET) goto error;  // Duplicate.
      if (!parse_wait_for_ready(field, &wait_for_ready)) goto error;
    } else if (strcmp(field->key, "timeout") == 0) {
      if (timeout.tv_sec > 0 || timeout.tv_nsec > 0) goto error;  // Duplicate.
      if (!parse_timeout(field, &timeout)) goto error;
    } else if (strcmp(field->key, "retryPolicy") == 0) {
      if (retry_policy != NULL) goto error;  // Duplicate.
      retry_policy = gpr_malloc(sizeof(*retry_policy));
      memset(retry_policy, 0, sizeof(*retry_policy));
      if (!parse_retry_policy(field, retry_policy)) goto error;
    }
  }
  method_parameters *value = gpr_malloc(sizeof(method_parameters));
  gpr_ref_init(&value->refs, 1);
  value->timeout = timeout;
  value->wait_for_ready = wait_for_ready;
  value->retry_policy = retry_policy;
  return value;
error:
  if (retry_policy != NULL) gpr_free(retry_policy->retryable_status_codes);
  gpr_free(retry_policy);
  return NULL;
}

struct external_connectivity_watcher;

/*************************************************************************
 * CHANNEL-WIDE FUNCTIONS
 */

typedef struct client_channel_channel_data {
  /** resolver for this channel */
  grpc_resolver *resolver;
  /** have we started resolving this channel */
  bool started_resolving;
  /** is deadline checking enabled? */
  bool deadline_checking_enabled;
  /** client channel factory */
  grpc_client_channel_factory *client_channel_factory;
  /** per-RPC retry buffer size */
  size_t per_rpc_retry_buffer_size;

  /** combiner protecting all variables below in this data structure */
  grpc_combiner *combiner;
  /** currently active load balancer */
  grpc_lb_policy *lb_policy;
  /** retry throttle data */
  grpc_server_retry_throttle_data *retry_throttle_data;
  /** maps method names to method_parameters structs */
  grpc_slice_hash_table *method_params_table;
  /** incoming resolver result - set by resolver.next() */
  grpc_channel_args *resolver_result;
  /** a list of closures that are all waiting for resolver result to come in */
  grpc_closure_list waiting_for_resolver_result_closures;
  /** resolver callback */
  grpc_closure on_resolver_result_changed;
  /** connectivity state being tracked */
  grpc_connectivity_state_tracker state_tracker;
  /** when an lb_policy arrives, should we try to exit idle */
  bool exit_idle_when_lb_policy_arrives;
  /** owning stack */
  grpc_channel_stack *owning_stack;
  /** interested parties (owned) */
  grpc_pollset_set *interested_parties;

  /* external_connectivity_watcher_list head is guarded by its own mutex, since
   * counts need to be grabbed immediately without polling on a cq */
  gpr_mu external_connectivity_watcher_list_mu;
  struct external_connectivity_watcher *external_connectivity_watcher_list_head;

  /* the following properties are guarded by a mutex since API's require them
     to be instantaneously available */
  gpr_mu info_mu;
  char *info_lb_policy_name;
  /** service config in JSON form */
  char *info_service_config_json;
} channel_data;

/** We create one watcher for each new lb_policy that is returned from a
    resolver, to watch for state changes from the lb_policy. When a state
    change is seen, we update the channel, and create a new watcher. */
typedef struct {
  channel_data *chand;
  grpc_closure on_changed;
  grpc_connectivity_state state;
  grpc_lb_policy *lb_policy;
} lb_policy_connectivity_watcher;

static void watch_lb_policy_locked(grpc_exec_ctx *exec_ctx, channel_data *chand,
                                   grpc_lb_policy *lb_policy,
                                   grpc_connectivity_state current_state);

static void set_channel_connectivity_state_locked(grpc_exec_ctx *exec_ctx,
                                                  channel_data *chand,
                                                  grpc_connectivity_state state,
                                                  grpc_error *error,
                                                  const char *reason) {
  /* TODO: Improve failure handling:
   * - Make it possible for policies to return GRPC_CHANNEL_TRANSIENT_FAILURE.
   * - Hand over pending picks from old policies during the switch that happens
   *   when resolver provides an update. */
  if (chand->lb_policy != NULL) {
    if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      /* cancel picks with wait_for_ready=false */
      grpc_lb_policy_cancel_picks_locked(
          exec_ctx, chand->lb_policy,
          /* mask= */ GRPC_INITIAL_METADATA_WAIT_FOR_READY,
          /* check= */ 0, GRPC_ERROR_REF(error));
    } else if (state == GRPC_CHANNEL_SHUTDOWN) {
      /* cancel all picks */
      grpc_lb_policy_cancel_picks_locked(exec_ctx, chand->lb_policy,
                                         /* mask= */ 0, /* check= */ 0,
                                         GRPC_ERROR_REF(error));
    }
  }
  if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
    gpr_log(GPR_DEBUG, "chand=%p: setting connectivity state to %s", chand,
            grpc_connectivity_state_name(state));
  }
  grpc_connectivity_state_set(exec_ctx, &chand->state_tracker, state, error,
                              reason);
}

static void on_lb_policy_state_changed_locked(grpc_exec_ctx *exec_ctx,
                                              void *arg, grpc_error *error) {
  lb_policy_connectivity_watcher *w = arg;
  grpc_connectivity_state publish_state = w->state;
  /* check if the notification is for the latest policy */
  if (w->lb_policy == w->chand->lb_policy) {
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG, "chand=%p: lb_policy=%p state changed to %s", w->chand,
              w->lb_policy, grpc_connectivity_state_name(w->state));
    }
    if (publish_state == GRPC_CHANNEL_SHUTDOWN && w->chand->resolver != NULL) {
      publish_state = GRPC_CHANNEL_TRANSIENT_FAILURE;
      grpc_resolver_channel_saw_error_locked(exec_ctx, w->chand->resolver);
      GRPC_LB_POLICY_UNREF(exec_ctx, w->chand->lb_policy, "channel");
      w->chand->lb_policy = NULL;
    }
    set_channel_connectivity_state_locked(exec_ctx, w->chand, publish_state,
                                          GRPC_ERROR_REF(error), "lb_changed");
    if (w->state != GRPC_CHANNEL_SHUTDOWN) {
      watch_lb_policy_locked(exec_ctx, w->chand, w->lb_policy, w->state);
    }
  }
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, w->chand->owning_stack, "watch_lb_policy");
  gpr_free(w);
}

static void watch_lb_policy_locked(grpc_exec_ctx *exec_ctx, channel_data *chand,
                                   grpc_lb_policy *lb_policy,
                                   grpc_connectivity_state current_state) {
  lb_policy_connectivity_watcher *w = gpr_malloc(sizeof(*w));
  GRPC_CHANNEL_STACK_REF(chand->owning_stack, "watch_lb_policy");
  w->chand = chand;
  GRPC_CLOSURE_INIT(&w->on_changed, on_lb_policy_state_changed_locked, w,
                    grpc_combiner_scheduler(chand->combiner));
  w->state = current_state;
  w->lb_policy = lb_policy;
  grpc_lb_policy_notify_on_state_change_locked(exec_ctx, lb_policy, &w->state,
                                               &w->on_changed);
}

static void start_resolving_locked(grpc_exec_ctx *exec_ctx,
                                   channel_data *chand) {
  if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
    gpr_log(GPR_DEBUG, "chand=%p: starting name resolution", chand);
  }
  GPR_ASSERT(!chand->started_resolving);
  chand->started_resolving = true;
  GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
  grpc_resolver_next_locked(exec_ctx, chand->resolver, &chand->resolver_result,
                            &chand->on_resolver_result_changed);
}

typedef struct {
  char *server_name;
  grpc_server_retry_throttle_data *retry_throttle_data;
} service_config_parsing_state;

static void parse_retry_throttle_params(const grpc_json *field, void *arg) {
  service_config_parsing_state *parsing_state = arg;
  if (strcmp(field->key, "retryThrottling") == 0) {
    if (parsing_state->retry_throttle_data != NULL) return;  // Duplicate.
    if (field->type != GRPC_JSON_OBJECT) return;
    int max_milli_tokens = 0;
    int milli_token_ratio = 0;
    for (grpc_json *sub_field = field->child; sub_field != NULL;
         sub_field = sub_field->next) {
      if (sub_field->key == NULL) return;
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
        const char *decimal_point = strchr(sub_field->value, '.');
        if (decimal_point != NULL) {
          whole_len = (size_t)(decimal_point - sub_field->value);
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
        milli_token_ratio = (int)((whole_value * multiplier) + decimal_value);
        if (milli_token_ratio <= 0) return;
      }
    }
    parsing_state->retry_throttle_data =
        grpc_retry_throttle_map_get_data_for_server(
            parsing_state->server_name, max_milli_tokens, milli_token_ratio);
  }
}

static void on_resolver_result_changed_locked(grpc_exec_ctx *exec_ctx,
                                              void *arg, grpc_error *error) {
  channel_data *chand = arg;
  if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
    gpr_log(GPR_DEBUG, "chand=%p: got resolver result: error=%s", chand,
            grpc_error_string(error));
  }
  // Extract the following fields from the resolver result, if non-NULL.
  bool lb_policy_updated = false;
  char *lb_policy_name = NULL;
  bool lb_policy_name_changed = false;
  grpc_lb_policy *new_lb_policy = NULL;
  char *service_config_json = NULL;
  grpc_server_retry_throttle_data *retry_throttle_data = NULL;
  grpc_slice_hash_table *method_params_table = NULL;
  if (chand->resolver_result != NULL) {
    // Find LB policy name.
    const grpc_arg *channel_arg =
        grpc_channel_args_find(chand->resolver_result, GRPC_ARG_LB_POLICY_NAME);
    if (channel_arg != NULL) {
      GPR_ASSERT(channel_arg->type == GRPC_ARG_STRING);
      lb_policy_name = channel_arg->value.string;
    }
    // Special case: If at least one balancer address is present, we use
    // the grpclb policy, regardless of what the resolver actually specified.
    channel_arg =
        grpc_channel_args_find(chand->resolver_result, GRPC_ARG_LB_ADDRESSES);
    if (channel_arg != NULL && channel_arg->type == GRPC_ARG_POINTER) {
      grpc_lb_addresses *addresses = channel_arg->value.pointer.p;
      bool found_balancer_address = false;
      for (size_t i = 0; i < addresses->num_addresses; ++i) {
        if (addresses->addresses[i].is_balancer) {
          found_balancer_address = true;
          break;
        }
      }
      if (found_balancer_address) {
        if (lb_policy_name != NULL && strcmp(lb_policy_name, "grpclb") != 0) {
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
    if (lb_policy_name == NULL) lb_policy_name = "pick_first";
    grpc_lb_policy_args lb_policy_args;
    lb_policy_args.args = chand->resolver_result;
    lb_policy_args.client_channel_factory = chand->client_channel_factory;
    lb_policy_args.combiner = chand->combiner;
    // Check to see if we're already using the right LB policy.
    // Note: It's safe to use chand->info_lb_policy_name here without
    // taking a lock on chand->info_mu, because this function is the
    // only thing that modifies its value, and it can only be invoked
    // once at any given time.
    lb_policy_name_changed =
        chand->info_lb_policy_name == NULL ||
        strcmp(chand->info_lb_policy_name, lb_policy_name) != 0;
    if (chand->lb_policy != NULL && !lb_policy_name_changed) {
      // Continue using the same LB policy.  Update with new addresses.
      lb_policy_updated = true;
      grpc_lb_policy_update_locked(exec_ctx, chand->lb_policy, &lb_policy_args);
    } else {
      // Instantiate new LB policy.
      new_lb_policy =
          grpc_lb_policy_create(exec_ctx, lb_policy_name, &lb_policy_args);
      if (new_lb_policy == NULL) {
        gpr_log(GPR_ERROR, "could not create LB policy \"%s\"", lb_policy_name);
      }
    }
    // Find service config.
    channel_arg =
        grpc_channel_args_find(chand->resolver_result, GRPC_ARG_SERVICE_CONFIG);
    if (channel_arg != NULL) {
      GPR_ASSERT(channel_arg->type == GRPC_ARG_STRING);
      service_config_json = gpr_strdup(channel_arg->value.string);
      grpc_service_config *service_config =
          grpc_service_config_create(service_config_json);
      if (service_config != NULL) {
        channel_arg =
            grpc_channel_args_find(chand->resolver_result, GRPC_ARG_SERVER_URI);
        GPR_ASSERT(channel_arg != NULL);
        GPR_ASSERT(channel_arg->type == GRPC_ARG_STRING);
        grpc_uri *uri =
            grpc_uri_parse(exec_ctx, channel_arg->value.string, true);
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
            exec_ctx, service_config, method_parameters_create_from_json,
            method_parameters_free);
        grpc_service_config_destroy(service_config);
      }
    }
    // Before we clean up, save a copy of lb_policy_name, since it might
    // be pointing to data inside chand->resolver_result.
    // The copy will be saved in chand->lb_policy_name below.
    lb_policy_name = gpr_strdup(lb_policy_name);
    grpc_channel_args_destroy(exec_ctx, chand->resolver_result);
    chand->resolver_result = NULL;
  }
  if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
    gpr_log(GPR_DEBUG,
            "chand=%p: resolver result: lb_policy_name=\"%s\"%s, "
            "service_config=\"%s\"",
            chand, lb_policy_name, lb_policy_name_changed ? " (changed)" : "",
            service_config_json);
  }
  // Now swap out fields in chand.  Note that the new values may still
  // be NULL if (e.g.) the resolver failed to return results or the
  // results did not contain the necessary data.
  //
  // First, swap out the data used by cc_get_channel_info().
  gpr_mu_lock(&chand->info_mu);
  if (lb_policy_name != NULL) {
    gpr_free(chand->info_lb_policy_name);
    chand->info_lb_policy_name = lb_policy_name;
  }
  if (service_config_json != NULL) {
    gpr_free(chand->info_service_config_json);
    chand->info_service_config_json = service_config_json;
  }
  gpr_mu_unlock(&chand->info_mu);
  // Swap out the retry throttle data.
  if (chand->retry_throttle_data != NULL) {
    grpc_server_retry_throttle_data_unref(chand->retry_throttle_data);
  }
  chand->retry_throttle_data = retry_throttle_data;
  // Swap out the method params table.
  if (chand->method_params_table != NULL) {
    grpc_slice_hash_table_unref(exec_ctx, chand->method_params_table);
  }
  chand->method_params_table = method_params_table;
  // If we have a new LB policy or are shutting down (in which case
  // new_lb_policy will be NULL), swap out the LB policy, unreffing the
  // old one and removing its fds from chand->interested_parties.
  // Note that we do NOT do this if either (a) we updated the existing
  // LB policy above or (b) we failed to create the new LB policy (in
  // which case we want to continue using the most recent one we had).
  if (new_lb_policy != NULL || error != GRPC_ERROR_NONE ||
      chand->resolver == NULL) {
    if (chand->lb_policy != NULL) {
      if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
        gpr_log(GPR_DEBUG, "chand=%p: unreffing lb_policy=%p", chand,
                chand->lb_policy);
      }
      grpc_pollset_set_del_pollset_set(exec_ctx,
                                       chand->lb_policy->interested_parties,
                                       chand->interested_parties);
      GRPC_LB_POLICY_UNREF(exec_ctx, chand->lb_policy, "channel");
    }
    chand->lb_policy = new_lb_policy;
  }
  // Now that we've swapped out the relevant fields of chand, check for
  // error or shutdown.
  if (error != GRPC_ERROR_NONE || chand->resolver == NULL) {
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG, "chand=%p: shutting down", chand);
    }
    if (chand->resolver != NULL) {
      if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
        gpr_log(GPR_DEBUG, "chand=%p: shutting down resolver", chand);
      }
      grpc_resolver_shutdown_locked(exec_ctx, chand->resolver);
      GRPC_RESOLVER_UNREF(exec_ctx, chand->resolver, "channel");
      chand->resolver = NULL;
    }
    set_channel_connectivity_state_locked(
        exec_ctx, chand, GRPC_CHANNEL_SHUTDOWN,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "Got resolver result after disconnection", &error, 1),
        "resolver_gone");
    GRPC_CHANNEL_STACK_UNREF(exec_ctx, chand->owning_stack, "resolver");
    grpc_closure_list_fail_all(&chand->waiting_for_resolver_result_closures,
                               GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                   "Channel disconnected", &error, 1));
    GRPC_CLOSURE_LIST_SCHED(exec_ctx,
                            &chand->waiting_for_resolver_result_closures);
  } else {  // Not shutting down.
    grpc_connectivity_state state = GRPC_CHANNEL_TRANSIENT_FAILURE;
    grpc_error *state_error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("No load balancing policy");
    if (new_lb_policy != NULL) {
      if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
        gpr_log(GPR_DEBUG, "chand=%p: initializing new LB policy", chand);
      }
      GRPC_ERROR_UNREF(state_error);
      state = grpc_lb_policy_check_connectivity_locked(exec_ctx, new_lb_policy,
                                                       &state_error);
      grpc_pollset_set_add_pollset_set(exec_ctx,
                                       new_lb_policy->interested_parties,
                                       chand->interested_parties);
      GRPC_CLOSURE_LIST_SCHED(exec_ctx,
                              &chand->waiting_for_resolver_result_closures);
      if (chand->exit_idle_when_lb_policy_arrives) {
        grpc_lb_policy_exit_idle_locked(exec_ctx, new_lb_policy);
        chand->exit_idle_when_lb_policy_arrives = false;
      }
      watch_lb_policy_locked(exec_ctx, chand, new_lb_policy, state);
    }
    if (!lb_policy_updated) {
      set_channel_connectivity_state_locked(exec_ctx, chand, state,
                                            GRPC_ERROR_REF(state_error),
                                            "new_lb+resolver");
    }
    grpc_resolver_next_locked(exec_ctx, chand->resolver,
                              &chand->resolver_result,
                              &chand->on_resolver_result_changed);
    GRPC_ERROR_UNREF(state_error);
  }
}

static void start_transport_op_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_error *error_ignored) {
  grpc_transport_op *op = arg;
  grpc_channel_element *elem = op->handler_private.extra_arg;
  channel_data *chand = elem->channel_data;

  if (op->on_connectivity_state_change != NULL) {
    grpc_connectivity_state_notify_on_state_change(
        exec_ctx, &chand->state_tracker, op->connectivity_state,
        op->on_connectivity_state_change);
    op->on_connectivity_state_change = NULL;
    op->connectivity_state = NULL;
  }

  if (op->send_ping != NULL) {
    if (chand->lb_policy == NULL) {
      GRPC_CLOSURE_SCHED(
          exec_ctx, op->send_ping,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Ping with no load balancing"));
    } else {
      grpc_lb_policy_ping_one_locked(exec_ctx, chand->lb_policy, op->send_ping);
      op->bind_pollset = NULL;
    }
    op->send_ping = NULL;
  }

  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    if (chand->resolver != NULL) {
      set_channel_connectivity_state_locked(
          exec_ctx, chand, GRPC_CHANNEL_SHUTDOWN,
          GRPC_ERROR_REF(op->disconnect_with_error), "disconnect");
      grpc_resolver_shutdown_locked(exec_ctx, chand->resolver);
      GRPC_RESOLVER_UNREF(exec_ctx, chand->resolver, "channel");
      chand->resolver = NULL;
      if (!chand->started_resolving) {
        grpc_closure_list_fail_all(&chand->waiting_for_resolver_result_closures,
                                   GRPC_ERROR_REF(op->disconnect_with_error));
        GRPC_CLOSURE_LIST_SCHED(exec_ctx,
                                &chand->waiting_for_resolver_result_closures);
      }
      if (chand->lb_policy != NULL) {
        grpc_pollset_set_del_pollset_set(exec_ctx,
                                         chand->lb_policy->interested_parties,
                                         chand->interested_parties);
        GRPC_LB_POLICY_UNREF(exec_ctx, chand->lb_policy, "channel");
        chand->lb_policy = NULL;
      }
    }
    GRPC_ERROR_UNREF(op->disconnect_with_error);
  }
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, chand->owning_stack, "start_transport_op");

  GRPC_CLOSURE_SCHED(exec_ctx, op->on_consumed, GRPC_ERROR_NONE);
}

static void cc_start_transport_op(grpc_exec_ctx *exec_ctx,
                                  grpc_channel_element *elem,
                                  grpc_transport_op *op) {
  channel_data *chand = elem->channel_data;

  GPR_ASSERT(op->set_accept_stream == false);
  if (op->bind_pollset != NULL) {
    grpc_pollset_set_add_pollset(exec_ctx, chand->interested_parties,
                                 op->bind_pollset);
  }

  op->handler_private.extra_arg = elem;
  GRPC_CHANNEL_STACK_REF(chand->owning_stack, "start_transport_op");
  GRPC_CLOSURE_SCHED(
      exec_ctx,
      GRPC_CLOSURE_INIT(&op->handler_private.closure, start_transport_op_locked,
                        op, grpc_combiner_scheduler(chand->combiner)),
      GRPC_ERROR_NONE);
}

static void cc_get_channel_info(grpc_exec_ctx *exec_ctx,
                                grpc_channel_element *elem,
                                const grpc_channel_info *info) {
  channel_data *chand = elem->channel_data;
  gpr_mu_lock(&chand->info_mu);
  if (info->lb_policy_name != NULL) {
    *info->lb_policy_name = chand->info_lb_policy_name == NULL
                                ? NULL
                                : gpr_strdup(chand->info_lb_policy_name);
  }
  if (info->service_config_json != NULL) {
    *info->service_config_json =
        chand->info_service_config_json == NULL
            ? NULL
            : gpr_strdup(chand->info_service_config_json);
  }
  gpr_mu_unlock(&chand->info_mu);
}

/* Constructor for channel_data */
static grpc_error *cc_init_channel_elem(grpc_exec_ctx *exec_ctx,
                                        grpc_channel_element *elem,
                                        grpc_channel_element_args *args) {
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(args->is_last);
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);
  // Initialize data members.
  chand->combiner = grpc_combiner_create();
  gpr_mu_init(&chand->info_mu);
  gpr_mu_init(&chand->external_connectivity_watcher_list_mu);

  gpr_mu_lock(&chand->external_connectivity_watcher_list_mu);
  chand->external_connectivity_watcher_list_head = NULL;
  gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);

  chand->owning_stack = args->channel_stack;
  GRPC_CLOSURE_INIT(&chand->on_resolver_result_changed,
                    on_resolver_result_changed_locked, chand,
                    grpc_combiner_scheduler(chand->combiner));
  chand->interested_parties = grpc_pollset_set_create();
  grpc_connectivity_state_init(&chand->state_tracker, GRPC_CHANNEL_IDLE,
                               "client_channel");
  // Record max per-RPC retry buffer size.
  const grpc_arg *arg = grpc_channel_args_find(
      args->channel_args, GRPC_ARG_PER_RPC_RETRY_BUFFER_SIZE);
  chand->per_rpc_retry_buffer_size = (size_t)grpc_channel_arg_get_integer(
      arg, (grpc_integer_options){DEFAULT_PER_RPC_RETRY_BUFFER_SIZE, 0,
                                  INT_MAX});
  // Record client channel factory.
  arg = grpc_channel_args_find(args->channel_args,
                               GRPC_ARG_CLIENT_CHANNEL_FACTORY);
  if (arg == NULL) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Missing client channel factory in args for client channel filter");
  }
  if (arg->type != GRPC_ARG_POINTER) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "client channel factory arg must be a pointer");
  }
  grpc_client_channel_factory_ref(arg->value.pointer.p);
  chand->client_channel_factory = arg->value.pointer.p;
  // Get server name to resolve, using proxy mapper if needed.
  arg = grpc_channel_args_find(args->channel_args, GRPC_ARG_SERVER_URI);
  if (arg == NULL) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Missing server uri in args for client channel filter");
  }
  if (arg->type != GRPC_ARG_STRING) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "server uri arg must be a string");
  }
  char *proxy_name = NULL;
  grpc_channel_args *new_args = NULL;
  grpc_proxy_mappers_map_name(exec_ctx, arg->value.string, args->channel_args,
                              &proxy_name, &new_args);
  // Instantiate resolver.
  chand->resolver = grpc_resolver_create(
      exec_ctx, proxy_name != NULL ? proxy_name : arg->value.string,
      new_args != NULL ? new_args : args->channel_args,
      chand->interested_parties, chand->combiner);
  if (proxy_name != NULL) gpr_free(proxy_name);
  if (new_args != NULL) grpc_channel_args_destroy(exec_ctx, new_args);
  if (chand->resolver == NULL) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("resolver creation failed");
  }
  chand->deadline_checking_enabled =
      grpc_deadline_checking_enabled(args->channel_args);
  return GRPC_ERROR_NONE;
}

static void shutdown_resolver_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                     grpc_error *error) {
  grpc_resolver *resolver = arg;
  grpc_resolver_shutdown_locked(exec_ctx, resolver);
  GRPC_RESOLVER_UNREF(exec_ctx, resolver, "channel");
}

/* Destructor for channel_data */
static void cc_destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                    grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  if (chand->resolver != NULL) {
    GRPC_CLOSURE_SCHED(
        exec_ctx, GRPC_CLOSURE_CREATE(shutdown_resolver_locked, chand->resolver,
                                      grpc_combiner_scheduler(chand->combiner)),
        GRPC_ERROR_NONE);
  }
  if (chand->client_channel_factory != NULL) {
    grpc_client_channel_factory_unref(exec_ctx, chand->client_channel_factory);
  }
  if (chand->lb_policy != NULL) {
    grpc_pollset_set_del_pollset_set(exec_ctx,
                                     chand->lb_policy->interested_parties,
                                     chand->interested_parties);
    GRPC_LB_POLICY_UNREF(exec_ctx, chand->lb_policy, "channel");
  }
  gpr_free(chand->info_lb_policy_name);
  gpr_free(chand->info_service_config_json);
  if (chand->retry_throttle_data != NULL) {
    grpc_server_retry_throttle_data_unref(chand->retry_throttle_data);
  }
  if (chand->method_params_table != NULL) {
    grpc_slice_hash_table_unref(exec_ctx, chand->method_params_table);
  }
  grpc_connectivity_state_destroy(exec_ctx, &chand->state_tracker);
  grpc_pollset_set_destroy(exec_ctx, chand->interested_parties);
  GRPC_COMBINER_UNREF(exec_ctx, chand->combiner, "client_channel");
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
#define MAX_PENDING_BATCHES 6

// Retry support:
//
// There are 2 sets of data to maintain:
// - In call_data (in the parent channel), we maintain a list of pending
//   ops and cached data for those ops.
// - In the subchannel call, we maintain state to indicate what ops have
//   already been sent down to that call.
//
// When new ops come down, we first try to send them immediately.  If
// they fail and are retryable, then we do a new pick and start again.
//
// when new ops come down:
// - if retries are enabled, create subchannel_batch_data and use that
//   to send down batch
// - otherwise, send down as-is
//
// In on_complete:
// - if failed and is retryable, start new pick and then retry
// - otherwise, return to surface
//
// synchronization problems:
// - new batch coming down and retry started at the same time, how do we
//   know which ops to include?

// State used for sending a retryable batch down to a subchannel call.
// This provides its own grpc_transport_stream_op_batch and other data
// structures needed to populate the ops in the batch.
// We allocate one struct on the arena for each batch we get from the surface.
typedef struct {
  grpc_call_element *elem;
  grpc_subchannel_call *subchannel_call;
  // The batch to use in the subchannel call.
  // Its payload field points to subchannel_call_retry_state.batch_payload.
  grpc_transport_stream_op_batch batch;
  // For send_initial_metadata.
// FIXME: how do we propagate this back up, given that we may return
// send_initial_metadata on_complete and then later decide to retry?
  gpr_atm peer_string;
  // For send_message.
  grpc_caching_byte_stream send_message;
  // For intercepting recv_initial_metadata.
  grpc_metadata_batch recv_initial_metadata;
  grpc_closure recv_initial_metadata_ready;
  bool trailing_metadata_available;
  // For intercepting recv_message.
  grpc_closure recv_message_ready;
  grpc_byte_stream *recv_message;
  // For intercepting recv_trailing_metadata.
  grpc_metadata_batch recv_trailing_metadata;
  grpc_transport_stream_stats collect_stats;
  // For intercepting on_complete.
  grpc_closure on_complete;
} subchannel_batch_data;

// Retry state associated with a subchannel call.
// Stored in the parent_data of the subchannel call object.
typedef struct {
  // These fields indicate which ops have been sent down to this
  // subchannel call.
  bool send_initial_metadata;
  size_t send_message_count;
  bool send_trailing_metadata;
  bool recv_initial_metadata;
  bool recv_message;
  bool recv_trailing_metadata;
  bool recv_initial_metadata_ready_pending;
  // subchannel_batch_data.batch.payload points to this.
  grpc_transport_stream_op_batch_payload batch_payload;
  bool retry_dispatched;
} subchannel_call_retry_state;

typedef struct {
  grpc_transport_stream_op_batch *batch;
  bool modified_to_cache_send_ops;
  grpc_call_element *elem;
  grpc_closure handle_in_call_combiner;
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
  // and this struct both independently store a pointer to the call
  // stack and the call combiner.  If/when we have time, find a way to avoid
  // this without breaking the grpc_deadline_state abstraction.
  grpc_deadline_state deadline_state;

  grpc_slice path;  // Request path.
  gpr_timespec call_start_time;
  gpr_timespec deadline;
  grpc_call_stack *owning_call;
  gpr_arena *arena;
  grpc_call_combiner *call_combiner;

  grpc_server_retry_throttle_data *retry_throttle_data;
  method_parameters *method_params;

  grpc_subchannel_call *subchannel_call;
  grpc_error *error;

  grpc_lb_policy *lb_policy;  // Holds ref while LB pick is pending.
  grpc_closure lb_pick_closure;
  grpc_closure cancel_closure;

  grpc_connected_subchannel *connected_subchannel;
  grpc_call_context_element subchannel_call_context[GRPC_CONTEXT_COUNT];
  grpc_polling_entity *pollent;
  grpc_linked_mdelem lb_token_mdelem;

  // Batches are added to this list when received from above.
  // They are removed when we are done handling the batch (i.e., when
  // either we have invoked all of the batch's callbacks or we have
  // passed the batch down to the subchannel call and are not
  // intercepting any of its callbacks).
  pending_batch pending_batches[MAX_PENDING_BATCHES];

  // Retry state.
  bool retry_committed;
  int num_retry_attempts;
  size_t bytes_buffered_for_retry;
  gpr_backoff retry_backoff;
  grpc_timer retry_timer;
  grpc_call_context_element *context;

  // Copy of initial metadata.
  // Populated when we receive a send_initial_metadata op.
  grpc_linked_mdelem *send_initial_metadata_storage;
  grpc_metadata_batch send_initial_metadata;
  uint32_t send_initial_metadata_flags;
  // The contents for sent messages.
  // When we get a send_message op, we replace the original byte stream
  // with a grpc_caching_byte_stream that caches the slices to a
  // local buffer for use in retries.  We use initial_send_message as the
  // cache for the first send_message op, so that we don't need to allocate
  // memory for unary RPCs.  All subsequent messages are stored in
  // send_messages, which are dynamically allocated as needed.
  grpc_byte_stream_cache initial_send_message;
  grpc_byte_stream_cache *send_messages;
  size_t num_send_message_ops;
  // Non-NULL if we've received a send_trailing_metadata op.
  grpc_metadata_batch *send_trailing_metadata;
} call_data;

grpc_subchannel_call *grpc_client_channel_get_subchannel_call(
    grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  return calld->subchannel_call;
}

static void start_retriable_subchannel_batch(grpc_exec_ctx *exec_ctx,
                                             grpc_call_element *elem);

static size_t get_batch_index(grpc_transport_stream_op_batch *batch) {
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

static void pending_batches_add(grpc_call_element *elem,
                                grpc_transport_stream_op_batch *batch) {
  call_data *calld = (call_data *)elem->call_data;
  const size_t idx = get_batch_index(batch);
  pending_batch *pending = &calld->pending_batches[idx];
  GPR_ASSERT(pending->batch == NULL);
  pending->batch = batch;
  pending->modified_to_cache_send_ops = false;
  pending->elem = elem;
}

static void fail_pending_batch_in_call_combiner(grpc_exec_ctx *exec_ctx,
                                                void *arg, grpc_error *error) {
  pending_batch *pending = (pending_batch *)arg;
  call_data *calld = (call_data *)pending->elem->call_data;
  // Must clear pending->batch before invoking
  // grpc_transport_stream_op_batch_finish_with_failure(), since that
  // results in yielding the call combiner.
  grpc_transport_stream_op_batch *batch = pending->batch;
  pending->batch = NULL;
  grpc_transport_stream_op_batch_finish_with_failure(
      exec_ctx, batch, GRPC_ERROR_REF(error), calld->call_combiner);
}

static void pending_batches_fail(grpc_exec_ctx *exec_ctx,
                                 grpc_call_element *elem, grpc_error *error) {
  call_data *calld = elem->call_data;
  if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
      if (calld->pending_batches[i].batch != NULL) ++num_batches;
    }
    gpr_log(GPR_DEBUG,
            "chand=%p calld=%p: failing %" PRIdPTR " pending batches: %s",
            elem->channel_data, calld, num_batches, grpc_error_string(error));
  }
  size_t first_batch_idx = GPR_ARRAY_SIZE(calld->pending_batches);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    pending_batch *pending = &calld->pending_batches[i];
    if (pending->batch != NULL) {
      if (first_batch_idx == GPR_ARRAY_SIZE(calld->pending_batches)) {
        first_batch_idx = i;
      } else {
        GRPC_CLOSURE_INIT(&pending->handle_in_call_combiner,
                          fail_pending_batch_in_call_combiner, pending,
                          grpc_schedule_on_exec_ctx);
        GRPC_CALL_COMBINER_START(exec_ctx, calld->call_combiner,
                                 &pending->handle_in_call_combiner,
                                 GRPC_ERROR_REF(error), "pending_batches_fail");
      }
    }
  }
  if (first_batch_idx != GPR_ARRAY_SIZE(calld->pending_batches)) {
    // Manually invoking callback function; does not take ownership of error.
    fail_pending_batch_in_call_combiner(
        exec_ctx, &calld->pending_batches[first_batch_idx], error);
  } else {
    GRPC_CALL_COMBINER_STOP(exec_ctx, calld->call_combiner,
                            "pending_batches_fail");
  }
  GRPC_ERROR_UNREF(error);
}

static void resume_pending_batch_in_call_combiner(grpc_exec_ctx *exec_ctx,
                                               void *arg, grpc_error *ignored) {
  pending_batch *pending = (pending_batch *)arg;
  grpc_call_element *elem = pending->elem;
  call_data *calld = (call_data *)elem->call_data;
  // Must clear pending->batch before invoking
  // grpc_subchannel_call_process_op(), since that results in yielding
  // the call combiner.
  grpc_transport_stream_op_batch *batch = pending->batch;
  pending->batch = NULL;
  grpc_subchannel_call_process_op(exec_ctx, calld->subchannel_call, batch);
}

static void pending_batches_resume(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
      if (calld->pending_batches[i].batch != NULL) ++num_batches;
    }
    gpr_log(GPR_DEBUG, "chand=%p calld=%p: sending %" PRIdPTR
                       " pending batches to subchannel_call=%p",
            chand, calld, num_batches, calld->subchannel_call);
  }
gpr_log(GPR_INFO, "method_params=%p", calld->method_params);
if (calld->method_params) gpr_log(GPR_INFO, "method_params->retry_policy=%p", calld->method_params->retry_policy);
gpr_log(GPR_INFO, "retry_committed=%d", calld->retry_committed);
  if (calld->method_params != NULL &&
      calld->method_params->retry_policy != NULL && !calld->retry_committed) {
gpr_log(GPR_INFO, "RETRIES CONFIGURED");
    start_retriable_subchannel_batch(exec_ctx, elem);
  } else {
    // Retries not enabled; send down batches as-is.
    size_t first_batch_idx = GPR_ARRAY_SIZE(calld->pending_batches);
    for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
      pending_batch *pending = &calld->pending_batches[i];
      if (pending->batch != NULL) {
        if (first_batch_idx == GPR_ARRAY_SIZE(calld->pending_batches)) {
          first_batch_idx = i;
        } else {
          GRPC_CLOSURE_INIT(&pending->handle_in_call_combiner,
                            resume_pending_batch_in_call_combiner, pending,
                            grpc_schedule_on_exec_ctx);
          GRPC_CALL_COMBINER_START(exec_ctx, calld->call_combiner,
                                   &pending->handle_in_call_combiner,
                                   GRPC_ERROR_NONE, "pending_batches_resume");
        }
      }
    }
    GPR_ASSERT(first_batch_idx != GPR_ARRAY_SIZE(calld->pending_batches));
    resume_pending_batch_in_call_combiner(
        exec_ctx, &calld->pending_batches[first_batch_idx], GRPC_ERROR_NONE);
  }
}

static void maybe_clear_pending_batch(pending_batch *pending) {
  grpc_transport_stream_op_batch *batch = pending->batch;
  if (batch->on_complete == NULL &&
      (!batch->recv_initial_metadata ||
       batch->payload->recv_initial_metadata.recv_initial_metadata == NULL) &&
      (!batch->recv_message ||
       batch->payload->recv_message.recv_message_ready == NULL)) {
gpr_log(GPR_INFO, "CLEARING pending_batch");
    pending->batch = NULL;
  }
}

// Cleans up retry state.  Called either when the RPC is committed
// (i.e., we will not attempt any more retries) or when the call is
// destroyed.
static void retry_committed(grpc_exec_ctx *exec_ctx, call_data *calld) {
  if (calld->retry_committed) return;
  calld->retry_committed = true;
  if (calld->send_initial_metadata_storage != NULL) {
    grpc_metadata_batch_destroy(exec_ctx, &calld->send_initial_metadata);
    gpr_free(calld->send_initial_metadata_storage);
  }
  if (calld->num_send_message_ops > 0) {
    grpc_byte_stream_cache_destroy(exec_ctx, &calld->initial_send_message);
  }
  for (int i = 0; i < (int)calld->num_send_message_ops - 2; ++i) {
    grpc_byte_stream_cache_destroy(exec_ctx, &calld->send_messages[i]);
  }
  gpr_free(calld->send_messages);
}

static grpc_byte_stream_cache *get_send_message_cache(call_data *calld,
                                                      size_t index) {
  GPR_ASSERT(index < calld->num_send_message_ops);
  return index == 0
         ? &calld->initial_send_message
         : &calld->send_messages[index - 1];
}

// If retries are configured, checks to see if this exceeds the retry
// buffer limit.  If it doesn't exceed the limit, caches data for send ops
// (if any).
static void retry_checks_for_new_batch(grpc_exec_ctx *exec_ctx,
                                       grpc_call_element *elem,
                                       pending_batch *pending) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  if (pending->modified_to_cache_send_ops) return;
  pending->modified_to_cache_send_ops = true;
  grpc_transport_stream_op_batch *batch = pending->batch;
gpr_log(GPR_INFO, "method_params=%p", calld->method_params);
gpr_log(GPR_INFO, "retry_policy=%p", calld->method_params == NULL ? NULL : calld->method_params->retry_policy);
gpr_log(GPR_INFO, "retry_committed=%d", calld->retry_committed);
  if (calld->method_params != NULL &&
      calld->method_params->retry_policy != NULL && !calld->retry_committed) {
gpr_log(GPR_INFO, "retries configured and not committed");
    // Save context.  Should be the same for all batches on a call.
    calld->context = batch->payload->context;
    // Check if the batch takes us over the retry buffer limit.
    if (batch->send_initial_metadata) {
      calld->bytes_buffered_for_retry += grpc_metadata_batch_size(
          batch->payload->send_initial_metadata.send_initial_metadata);
    }
    if (batch->send_message) {
      calld->bytes_buffered_for_retry +=
          batch->payload->send_message.send_message->length;
    }
    if (calld->bytes_buffered_for_retry > chand->per_rpc_retry_buffer_size) {
gpr_log(GPR_INFO, "size exceeded, committing for retries");
      retry_committed(exec_ctx, calld);
    } else if (!batch->cancel_stream) {  // FIXME: better way to handle cancel_stream?
      // Save a copy of metadata for send_initial_metadata ops.
      if (batch->send_initial_metadata) {
        GPR_ASSERT(calld->send_initial_metadata_storage == NULL);
        grpc_error *error = grpc_metadata_batch_copy(
            exec_ctx,
            batch->payload->send_initial_metadata.send_initial_metadata,
            &calld->send_initial_metadata,
            &calld->send_initial_metadata_storage);
        if (error != GRPC_ERROR_NONE) {
          // If we couldn't copy the metadata, we won't be able to retry,
          // but we can still proceed with the initial RPC.
gpr_log(GPR_INFO, "grpc_metadata_batch_copy() failed, committing");
          retry_committed(exec_ctx, calld);
          GRPC_ERROR_UNREF(error);
          return;
        }
        calld->send_initial_metadata_flags =
            batch->payload->send_initial_metadata.send_initial_metadata_flags;
      }
      // Set up cache for send_message ops.
      if (batch->send_message) {
        if (calld->num_send_message_ops > 0) {
          calld->send_messages = gpr_realloc(
              calld->send_messages,
              sizeof(grpc_byte_stream_cache) * calld->num_send_message_ops);
        }
        ++calld->num_send_message_ops;
        grpc_byte_stream_cache *cache =
            get_send_message_cache(calld, calld->num_send_message_ops - 1);
        grpc_byte_stream_cache_init(cache,
                                    batch->payload->send_message.send_message);
      }
      // Save metadata batch for send_trailing_metadata ops.
      if (batch->send_trailing_metadata) {
        calld->send_trailing_metadata =
            batch->payload->send_trailing_metadata.send_trailing_metadata;
      }
    }
  }
}

static bool is_status_code_in_list(grpc_status_code status,
                                   grpc_status_code* list, size_t list_size) {
  if (list == NULL) return true;
  for (size_t i = 0; i < list_size; ++i) {
    if (status == list[i]) return true;
  }
  return false;
}

static void start_subchannel_pick_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                         grpc_error *ignored);

// Returns true if a retry is attempted.
static bool maybe_retry(grpc_exec_ctx *exec_ctx,
                        subchannel_batch_data *batch_data,
                        grpc_status_code status) {
  grpc_call_element *elem = batch_data->elem;
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  // Get retry policy.
  GPR_ASSERT(calld->method_params != NULL);
  retry_policy_params *retry_policy = calld->method_params->retry_policy;
  GPR_ASSERT(retry_policy != NULL);
  // Check status.
  if (status == GRPC_STATUS_OK) {
    grpc_server_retry_throttle_data_record_success(calld->retry_throttle_data);
  } else {
    // If we've already dispatched a retry from this call, return true.
    // This catches the case where the batch has multiple callbacks
    // (i.e., it includes either recv_message or recv_initial_metadata and
    // at least one other op).
// FIXME: shouldn't this be in subchannel_batch_data, not
// subchannel_call_retry_state?
    subchannel_call_retry_state *retry_state =
        grpc_connected_subchannel_call_get_parent_data(
            batch_data->subchannel_call);
    if (retry_state->retry_dispatched) return true;
    retry_state->retry_dispatched = true;
    // Check whether the status is retryable and whether we're being throttled.
    bool okay_to_retry = false;
    if (is_status_code_in_list(status, retry_policy->retryable_status_codes,
                               retry_policy->num_retryable_status_codes)) {
gpr_log(GPR_INFO, "CODE IN LIST");
      // Note that we should only record failures whose statuses match the
      // configured retryable status codes, since we don't want to count
      // failures due to malformed requests like INVALID_ARGUMENT.
      okay_to_retry = grpc_server_retry_throttle_data_record_failure(
          calld->retry_throttle_data);
    }
gpr_log(GPR_INFO, "okay_to_retry=%d", okay_to_retry);
    // Check whether the call is committed and whether we have retries
    // remaining.
gpr_log(GPR_INFO, "retry_committed=%d", calld->retry_committed);
gpr_log(GPR_INFO, "num_retry_attempts=%d", calld->num_retry_attempts);
gpr_log(GPR_INFO, "max_retry_attempts=%d", retry_policy->max_retry_attempts);
    if (okay_to_retry && !calld->retry_committed &&
        calld->num_retry_attempts < retry_policy->max_retry_attempts) {
gpr_log(GPR_INFO, "RETRYING");
      // Reset subchannel call.
// FIXME
      if (calld->subchannel_call != NULL) {
        GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, calld->subchannel_call,
                                   "client_channel_call_retry");
        calld->subchannel_call = NULL;
      }
      if (calld->error != GRPC_ERROR_NONE) {
        GRPC_ERROR_UNREF(calld->error);
        calld->error = GRPC_ERROR_NONE;
      }
#if 0
      call_or_error coe = get_call_or_error(calld);
      if (coe.subchannel_call != NULL) {
        GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, coe.subchannel_call,
                                   "client_channel_call_retry");
        set_call_or_error(calld, (call_or_error){
            .error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "call failed; retrying")});
      }
#endif

      // Compute backoff delay.
      gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
      gpr_timespec next_attempt_time;
      if (calld->num_retry_attempts == 0) {
        gpr_backoff_init(&calld->retry_backoff,
                         retry_policy->initial_backoff_ms,
                         retry_policy->backoff_multiplier, RETRY_BACKOFF_JITTER,
                         GPR_MIN(retry_policy->initial_backoff_ms,
                                 retry_policy->max_backoff_ms),
                         retry_policy->max_backoff_ms);
        next_attempt_time = gpr_backoff_begin(&calld->retry_backoff, now);
      } else {
        next_attempt_time = gpr_backoff_step(&calld->retry_backoff, now);
      }
// FIXME: is this needed?
      GRPC_CALL_STACK_REF(calld->owning_call, "maybe_retry");
      GRPC_CLOSURE_INIT(&batch_data->batch.handler_private.closure,
                        start_subchannel_pick_locked, elem,
                        grpc_combiner_scheduler(chand->combiner));
      grpc_timer_init(exec_ctx, &calld->retry_timer, next_attempt_time,
                      &batch_data->batch.handler_private.closure, now);
      ++calld->num_retry_attempts;
      return true;
    }
// FIXME: else set calld->retry_committed?
  }
  return false;
}

static void invoke_recv_initial_metadata_callback(grpc_exec_ctx *exec_ctx,
                                                  void *arg,
                                                  grpc_error *error) {
  subchannel_batch_data *batch_data = (subchannel_batch_data *)arg;
  call_data *calld = (call_data *)batch_data->elem->call_data;
  // Find pending batch.
  pending_batch *pending = NULL;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    grpc_transport_stream_op_batch *batch = calld->pending_batches[i].batch;
    if (batch != NULL &&
        batch->recv_initial_metadata &&
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready
            != NULL) {
gpr_log(GPR_INFO, "found recv_initial_metadata batch at index %" PRIdPTR, i);
      pending = &calld->pending_batches[i];
      break;
    }
  }
  GPR_ASSERT(pending != NULL);
  grpc_transport_stream_op_batch *original_batch = pending->batch;
  grpc_metadata_batch_move(
      &batch_data->recv_initial_metadata,
      original_batch->payload->recv_initial_metadata.recv_initial_metadata);
  GRPC_CLOSURE_RUN(
      exec_ctx,
      original_batch->payload->recv_initial_metadata
          .recv_initial_metadata_ready,
      GRPC_ERROR_REF(error));
gpr_log(GPR_INFO, "CLEARING pending_batch->recv_initial_metadata_ready");
  original_batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
      NULL;
  maybe_clear_pending_batch(pending);
}

// Intercepts recv_initial_metadata_ready callback for retries.
// Commits the call and returns the initial metadata up the stack.
static void recv_initial_metadata_ready(grpc_exec_ctx *exec_ctx, void *arg,
                                        grpc_error *error) {
gpr_log(GPR_INFO, "==> recv_initial_metadata_ready(): error=%s", grpc_error_string(error));
  subchannel_batch_data *batch_data = arg;
  call_data *calld = batch_data->elem->call_data;
  // If we got an error, attempt to retry the call.
  if (error != GRPC_ERROR_NONE) {
    grpc_status_code status;
    grpc_error_get_status(error, calld->deadline, &status, NULL, NULL);
    if (maybe_retry(exec_ctx, batch_data, status)) return;
  } else {
    // If we got a Trailers-Only response), do nothing.  We'll probably
    // wind up retrying when recv_trailing_metadata comes back.
// FIXME: do we need to do anything from below before we do this?
gpr_log(GPR_INFO, "trailing_metadata_available=%d", batch_data->trailing_metadata_available);
    if (batch_data->trailing_metadata_available) {
      subchannel_call_retry_state *retry_state =
          grpc_connected_subchannel_call_get_parent_data(
              batch_data->subchannel_call);
      retry_state->recv_initial_metadata_ready_pending = true;
      GRPC_CALL_COMBINER_STOP(exec_ctx, calld->call_combiner,
                              "recv_initial_metadata_ready trailers-only");
      return;
    }
    // No error, so commit the call.
gpr_log(GPR_INFO, "recv_initial_metadata_ready() commit");
    retry_committed(exec_ctx, calld);
  }
  GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, batch_data->subchannel_call,
                             "client_channel_recv_initial_metadata_ready");
  // Manually invoking a callback function; it does not take ownership of error.
  invoke_recv_initial_metadata_callback(exec_ctx, batch_data, error);
  GRPC_ERROR_UNREF(error);
}

// Intercepts recv_message_ready callback for retries.
// Commits the call and returns the message up the stack.
static void recv_message_ready(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error) {
gpr_log(GPR_INFO, "==> recv_message_ready()");
  subchannel_batch_data *batch_data = arg;
  call_data *calld = batch_data->elem->call_data;
  // If we got an error, attempt to retry the call.
  if (error != GRPC_ERROR_NONE) {
    grpc_status_code status;
    grpc_error_get_status(error, calld->deadline, &status, NULL, NULL);
    if (maybe_retry(exec_ctx, batch_data, status)) return;
  } else {
    // No error, so commit the call.
gpr_log(GPR_INFO, "recv_message_ready() commit");
    retry_committed(exec_ctx, calld);
  }
  // Find pending op.
  pending_batch *pending = NULL;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    grpc_transport_stream_op_batch *batch = calld->pending_batches[i].batch;
    if (batch != NULL &&
        batch->recv_message &&
        batch->payload->recv_message.recv_message_ready != NULL) {
gpr_log(GPR_INFO, "found recv_message batch at index %" PRIdPTR, i);
      pending = &calld->pending_batches[i];
      break;
    }
  }
  GPR_ASSERT(pending != NULL);
  grpc_transport_stream_op_batch *original_batch = pending->batch;
  *original_batch->payload->recv_message.recv_message =
      batch_data->recv_message;
  GRPC_CLOSURE_RUN(
      exec_ctx,
      original_batch->payload->recv_message.recv_message_ready,
      GRPC_ERROR_REF(error));
gpr_log(GPR_INFO, "CLEARING pending_batch->recv_message_ready");
  original_batch->payload->recv_message.recv_message_ready = NULL;
  maybe_clear_pending_batch(pending);
  GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, batch_data->subchannel_call,
                             "client_channel_recv_message_ready");
}

// Returns the entry in calld->pending_batches of the batch matching
// subchannel_batch, or NULL if no matching batch was found.
// Note that we do not match against a batch containing a send_message
// op unless this is the last send_message op, because we don't want to
// complete the batch if we're just replaying an already-reported-complete
// send_message op for a retry.
static pending_batch *get_matching_pending_batch(
    call_data *calld, grpc_transport_stream_op_batch *subchannel_batch,
    bool is_last_send_message) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    grpc_transport_stream_op_batch *batch = calld->pending_batches[i].batch;
    if (batch != NULL &&
        batch->on_complete != NULL &&
        subchannel_batch->send_initial_metadata ==
            batch->send_initial_metadata &&
        (!subchannel_batch->send_message ||
         (batch->send_message && is_last_send_message)) &&
        subchannel_batch->send_trailing_metadata ==
            batch->send_trailing_metadata &&
        subchannel_batch->recv_initial_metadata ==
            batch->recv_initial_metadata &&
        subchannel_batch->recv_message == batch->recv_message &&
        subchannel_batch->recv_trailing_metadata ==
            batch->recv_trailing_metadata) {
gpr_log(GPR_INFO, "found batch for on_complete at index %" PRIdPTR, i);
      return &calld->pending_batches[i];
    }
  }
  return NULL;
}

// Callback used to intercept on_complete from subchannel calls.
// Called only when retries are enabled.
static void on_complete(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  subchannel_batch_data *batch_data = arg;
  grpc_call_element *elem = batch_data->elem;
  call_data *calld = elem->call_data;

const char* str = grpc_error_string(error);
gpr_log(GPR_INFO, "==> on_complete(): error=%s", str);
gpr_log(GPR_INFO, "  batch:%s%s%s%s%s%s%s",
        batch_data->batch.send_initial_metadata ? " send_initial_metadata" : "",
        batch_data->batch.send_message ? " send_message" : "",
        batch_data->batch.send_trailing_metadata ? " send_trailing_metadata" : "",
        batch_data->batch.recv_initial_metadata ? " recv_initial_metadata" : "",
        batch_data->batch.recv_message ? " recv_message" : "",
        batch_data->batch.recv_trailing_metadata ? " recv_trailing_metadata" : "",
        batch_data->batch.cancel_stream ? " cancel_stream" : "");

  // Get retry policy.
  GPR_ASSERT(calld->method_params != NULL);
  retry_policy_params *retry_policy = calld->method_params->retry_policy;
  GPR_ASSERT(retry_policy != NULL);
  // If this op included a send_message op, check to see if it was the
  // last one.
  subchannel_call_retry_state *retry_state =
      grpc_connected_subchannel_call_get_parent_data(
          batch_data->subchannel_call);
  const bool have_pending_send_message_ops =
      retry_state->send_message_count < calld->num_send_message_ops;
  // There are several possible cases here:
  // 1. The batch failed (error != GRPC_ERROR_NONE).  In this case, the
  //    call is complete and has failed.
  // 2. The batch succeeded and included the recv_trailing_metadata op,
  //    and the metadata includes a non-OK status, in which case the call
  //    is complete and has failed.
  // 3. The batch succeeded and included the recv_trailing_metadata op,
  //    and the metadata includes status OK, in which case the call is
  //    complete and has succeeded.
  // 4. The batch succeeded but did not include the recv_trailing_metadata
  //    op, in which case the call is not yet complete.
  bool call_finished = false;
  grpc_status_code status = GRPC_STATUS_OK;
  if (error != GRPC_ERROR_NONE) {  // Case 1.
    call_finished = true;
    grpc_error_get_status(error, calld->deadline, &status, NULL, NULL);
  } else if (batch_data->batch.recv_trailing_metadata) {  // Cases 2 and 3.
    call_finished = true;
    grpc_metadata_batch *md_batch =
        batch_data->batch.payload->recv_trailing_metadata
            .recv_trailing_metadata;
    GPR_ASSERT(md_batch->idx.named.grpc_status != NULL);
    status = grpc_get_status_from_metadata(md_batch->idx.named.grpc_status->md);
  }
gpr_log(GPR_INFO, "call_finished=%d, status=%d", call_finished, status);
  // Cases 1, 2, and 3 are handled by maybe_retry().
  if (call_finished) {
    if (maybe_retry(exec_ctx, batch_data, status)) return;
    // If we are not retrying and there is a pending
    // recv_initial_metadata_ready callback, invoke it.
    if (retry_state->recv_initial_metadata_ready_pending) {
      GRPC_CLOSURE_INIT(&batch_data->recv_initial_metadata_ready,
                        invoke_recv_initial_metadata_callback, batch_data,
                        grpc_schedule_on_exec_ctx);
      GRPC_CALL_COMBINER_START(exec_ctx, calld->call_combiner,
                               &batch_data->recv_initial_metadata_ready,
                               GRPC_ERROR_NONE,
                               "resuming recv_initial_metadata_ready");
    }
  }
  // Case 4: call is not yet complete.
  else {
    if (have_pending_send_message_ops) {
gpr_log(GPR_INFO, "starting next batch for pending send_message ops");
      start_retriable_subchannel_batch(exec_ctx, elem);
    }
  }
  // Call succeeded or is not retryable.  Return back up the stack if needed.
  pending_batch *pending = get_matching_pending_batch(
      calld, &batch_data->batch, !have_pending_send_message_ops);
  if (pending != NULL) {
gpr_log(GPR_INFO, "calling original on_complete");
    GRPC_CLOSURE_RUN(exec_ctx, pending->batch->on_complete,
                     GRPC_ERROR_REF(error));
gpr_log(GPR_INFO, "CLEARING pending_batch->on_complete");
    pending->batch->on_complete = NULL;
    maybe_clear_pending_batch(pending);
  }
  GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, batch_data->subchannel_call,
                             "client_channel_on_complete");
}

static void start_retriable_subchannel_batch(grpc_exec_ctx *exec_ctx,
                                             grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  // Figure out what ops we have to send.
  // Note that we don't check for send_message ops here, since those
  // are detected in a different way below.
// FIXME: this isn't exactly right for the send ops -- we may have
// already completed them and sent the completions back to the surface
// (in which case they are no longer pending) but then need to send them
// again for a retry.  consider changing the way state is kept for the
// pending_batches list to avoid this problem.
  size_t send_initial_metadata_idx = INT_MAX;
  size_t send_trailing_metadata_idx = INT_MAX;
  size_t recv_initial_metadata_idx = INT_MAX;
  size_t recv_message_idx = INT_MAX;
  size_t recv_trailing_metadata_idx = INT_MAX;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    grpc_transport_stream_op_batch *batch = calld->pending_batches[i].batch;
    if (batch == NULL) continue;
    if (batch->send_initial_metadata) send_initial_metadata_idx = i;
    if (batch->send_trailing_metadata) send_trailing_metadata_idx = i;
    if (batch->recv_initial_metadata) recv_initial_metadata_idx = i;
    if (batch->recv_message) recv_message_idx = i;
    if (batch->recv_trailing_metadata) recv_trailing_metadata_idx = i;
    // Do retry checks for new batch, if needed.
    retry_checks_for_new_batch(exec_ctx, elem, &calld->pending_batches[i]);
  }
// FIXME: if there are no ops that we haven't yet sent, return without
// doing anything
  // Construct a batch of ops to send on this subchannel call.
  subchannel_call_retry_state *retry_state =
      grpc_connected_subchannel_call_get_parent_data(calld->subchannel_call);
  subchannel_batch_data *batch_data =
      gpr_arena_alloc(calld->arena, sizeof(*batch_data));
  batch_data->elem = elem;
  batch_data->subchannel_call = GRPC_SUBCHANNEL_CALL_REF(
      calld->subchannel_call,
      "client_channel_start_retriable_subchannel_batch");
  batch_data->batch.payload = &retry_state->batch_payload;
  // send_initial_metadata.
  if (send_initial_metadata_idx != INT_MAX &&
      !retry_state->send_initial_metadata) {
    retry_state->send_initial_metadata = true;
    batch_data->batch.send_initial_metadata = true;
    batch_data->batch.payload->send_initial_metadata.send_initial_metadata =
        &calld->send_initial_metadata;
    batch_data->batch.payload->send_initial_metadata
        .send_initial_metadata_flags =
            calld->send_initial_metadata_flags;
    batch_data->batch.payload->send_initial_metadata.peer_string =
        &batch_data->peer_string;
  }
  // send_message.
// FIXME: if we get a new send_message op while there's one already
// pending (e.g., if we told the surface that the first send_message
// succeeded and then we had to retry and had already re-sent the first
// message when we got the next send_message op), then we need to queue
// it without sending it right away
  const bool have_pending_send_message_ops =
      retry_state->send_message_count < calld->num_send_message_ops;
  if (have_pending_send_message_ops) {
    grpc_byte_stream_cache *cache =
        get_send_message_cache(calld, retry_state->send_message_count);
    ++retry_state->send_message_count;
    grpc_caching_byte_stream_init(&batch_data->send_message, cache);
    batch_data->batch.send_message = true;
    batch_data->batch.payload->send_message.send_message =
        (grpc_byte_stream *)&batch_data->send_message;
  }
  // send_trailing_metadata.
// FIXME: don't do this yet if there are pending send_message ops
  if (send_trailing_metadata_idx != INT_MAX &&
      !retry_state->send_trailing_metadata) {
    retry_state->send_trailing_metadata = true;
    batch_data->batch.send_trailing_metadata = true;
    batch_data->batch.payload->send_trailing_metadata.send_trailing_metadata =
        calld->send_trailing_metadata;
  }
  // recv_initial_metadata.
  if (recv_initial_metadata_idx != INT_MAX &&
      !retry_state->recv_initial_metadata) {
    retry_state->recv_initial_metadata = true;
    batch_data->batch.recv_initial_metadata = true;
    grpc_metadata_batch_init(&batch_data->recv_initial_metadata);
    batch_data->batch.payload->recv_initial_metadata.recv_initial_metadata =
        &batch_data->recv_initial_metadata;
    batch_data->batch.payload->recv_initial_metadata.recv_flags =
        calld->pending_batches[recv_initial_metadata_idx].batch->payload
            ->recv_initial_metadata.recv_flags;
    batch_data->batch.payload->recv_initial_metadata
        .trailing_metadata_available =
            &batch_data->trailing_metadata_available;
    GRPC_CLOSURE_INIT(&batch_data->recv_initial_metadata_ready,
                      recv_initial_metadata_ready, batch_data,
                      grpc_schedule_on_exec_ctx);
    batch_data->batch.payload->recv_initial_metadata
        .recv_initial_metadata_ready =
            &batch_data->recv_initial_metadata_ready;
    // Callback holds a ref.
    GRPC_SUBCHANNEL_CALL_REF(batch_data->subchannel_call,
                             "client_channel_recv_initial_metadata_ready");
  }
  // recv_message.
  if (recv_message_idx != INT_MAX && !retry_state->recv_message) {
    retry_state->recv_message = true;
    batch_data->batch.recv_message = true;
    batch_data->batch.payload->recv_message.recv_message =
        &batch_data->recv_message;
    GRPC_CLOSURE_INIT(&batch_data->recv_message_ready, recv_message_ready,
                      batch_data, grpc_schedule_on_exec_ctx);
    batch_data->batch.payload->recv_message.recv_message_ready =
        &batch_data->recv_message_ready;
    // Callback holds a ref.
    GRPC_SUBCHANNEL_CALL_REF(batch_data->subchannel_call,
                             "client_channel_recv_message_ready");
  }
  // recv_trailing_metadata.
  if (recv_trailing_metadata_idx != INT_MAX &&
      !retry_state->recv_trailing_metadata) {
    retry_state->recv_trailing_metadata = true;
    batch_data->batch.recv_trailing_metadata = true;
    grpc_metadata_batch_init(&batch_data->recv_trailing_metadata);
    batch_data->batch.payload->recv_trailing_metadata.recv_trailing_metadata =
        &batch_data->recv_trailing_metadata;
    GPR_ASSERT(calld->pending_batches[recv_trailing_metadata_idx].batch
                   ->collect_stats);
    batch_data->batch.collect_stats = true;
    batch_data->batch.payload->collect_stats.collect_stats =
        &batch_data->collect_stats;
  }
  // Intercept on_complete.
  GRPC_CLOSURE_INIT(&batch_data->on_complete, on_complete, batch_data,
                    grpc_schedule_on_exec_ctx);
  batch_data->batch.on_complete = &batch_data->on_complete;
  grpc_subchannel_call_process_op(exec_ctx, batch_data->subchannel_call,
                                  &batch_data->batch);
}

// Applies service config to the call.  Must be invoked once we know
// that the resolver has returned results to the channel.
static void apply_service_config_to_call_locked(grpc_exec_ctx *exec_ctx,
                                                grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
    gpr_log(GPR_DEBUG, "chand=%p calld=%p: applying service config to call",
            chand, calld);
  }
  if (chand->retry_throttle_data != NULL) {
    calld->retry_throttle_data =
        grpc_server_retry_throttle_data_ref(chand->retry_throttle_data);
  }
  if (chand->method_params_table != NULL) {
    calld->method_params = grpc_method_config_table_get(
        exec_ctx, chand->method_params_table, calld->path);
    if (calld->method_params != NULL) {
      method_parameters_ref(calld->method_params);
      // If the deadline from the service config is shorter than the one
      // from the client API, reset the deadline timer.
      if (chand->deadline_checking_enabled &&
          gpr_time_cmp(calld->method_params->timeout,
                       gpr_time_0(GPR_TIMESPAN)) != 0) {
        const gpr_timespec per_method_deadline =
            gpr_time_add(calld->call_start_time, calld->method_params->timeout);
        if (gpr_time_cmp(per_method_deadline, calld->deadline) < 0) {
          calld->deadline = per_method_deadline;
          grpc_deadline_state_reset(exec_ctx, elem, calld->deadline);
        }
      }
    }
  }
}

static void create_subchannel_call_locked(grpc_exec_ctx *exec_ctx,
                                          grpc_call_element *elem,
                                          grpc_error *error) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  const bool retries_enabled = calld->method_params != NULL &&
                               calld->method_params->retry_policy != NULL &&
                               !calld->retry_committed;
  const size_t parent_data_size =
      retries_enabled ? sizeof(subchannel_call_retry_state) : 0;
  const grpc_connected_subchannel_call_args call_args = {
      .pollent = calld->pollent,
      .path = calld->path,
      .start_time = calld->call_start_time,
      .deadline = calld->deadline,
      .arena = calld->arena,
      .context = calld->subchannel_call_context,
      .call_combiner = calld->call_combiner,
      .parent_data_size = parent_data_size};
  grpc_error *new_error = grpc_connected_subchannel_create_call(
      exec_ctx, calld->connected_subchannel, &call_args,
      &calld->subchannel_call);
  if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
    gpr_log(GPR_DEBUG, "chand=%p calld=%p: create subchannel_call=%p: error=%s",
            chand, calld, calld->subchannel_call, grpc_error_string(new_error));
  }
  if (new_error != GRPC_ERROR_NONE) {
    new_error = grpc_error_add_child(new_error, error);
    pending_batches_fail(exec_ctx, elem, new_error);
  } else {
    pending_batches_resume(exec_ctx, elem);
  }
  GRPC_ERROR_UNREF(error);
}

static void subchannel_ready_locked(grpc_exec_ctx *exec_ctx,
                                    grpc_call_element *elem,
                                    grpc_error *error) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_polling_entity_del_from_pollset_set(exec_ctx, calld->pollent,
                                           chand->interested_parties);
  if (calld->connected_subchannel == NULL) {
    // Failed to create subchannel.
    calld->error = error == GRPC_ERROR_NONE
                       ? GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                             "Call dropped by load balancing policy")
                       : GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Failed to create subchannel", &error, 1);
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG,
              "chand=%p calld=%p: failed to create subchannel: error=%s", chand,
              calld, grpc_error_string(calld->error));
    }
    pending_batches_fail(exec_ctx, elem, GRPC_ERROR_REF(calld->error));
  } else if (calld->error != GRPC_ERROR_NONE) {
    /* already cancelled before subchannel became ready */
    grpc_error *child_errors[] = {error, calld->error};
    grpc_error *cancellation_error =
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "Cancelled before creating subchannel", child_errors,
            GPR_ARRAY_SIZE(child_errors));
    /* if due to deadline, attach the deadline exceeded status to the error */
    if (gpr_time_cmp(calld->deadline, gpr_now(GPR_CLOCK_MONOTONIC)) < 0) {
      cancellation_error =
          grpc_error_set_int(cancellation_error, GRPC_ERROR_INT_GRPC_STATUS,
                             GRPC_STATUS_DEADLINE_EXCEEDED);
    }
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG,
              "chand=%p calld=%p: cancelled before subchannel became ready: %s",
              chand, calld, grpc_error_string(cancellation_error));
    }
    pending_batches_fail(exec_ctx, elem, cancellation_error);
  } else {
    /* Create call on subchannel. */
    create_subchannel_call_locked(exec_ctx, elem, GRPC_ERROR_REF(error));
  }
  GRPC_CALL_STACK_UNREF(exec_ctx, calld->owning_call, "pick_subchannel");
  GRPC_ERROR_UNREF(error);
}

/** Return true if subchannel is available immediately (in which case
    subchannel_ready_locked() should not be called), or false otherwise (in
    which case subchannel_ready_locked() should be called when the subchannel
    is available). */
static bool pick_subchannel_locked(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *elem);

typedef struct {
  grpc_call_element *elem;
  bool cancelled;
  grpc_closure closure;
} pick_after_resolver_result_args;

static void pick_after_resolver_result_cancel_locked(grpc_exec_ctx *exec_ctx,
                                                     void *arg,
                                                     grpc_error *error) {
  grpc_call_element *elem = arg;
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  // If we don't yet have a resolver result, then a closure for
  // pick_after_resolver_result_done_locked() will have been added to
  // chand->waiting_for_resolver_result_closures, and it may not be invoked
  // until after this call has been destroyed.  We mark the operation as
  // cancelled, so that when pick_after_resolver_result_done_locked()
  // is called, it will be a no-op.  We also immediately invoke
  // subchannel_ready_locked() to propagate the error back to the caller.
  for (grpc_closure *closure = chand->waiting_for_resolver_result_closures.head;
       closure != NULL; closure = closure->next_data.next) {
    pick_after_resolver_result_args *args = closure->cb_arg;
    if (!args->cancelled && args->elem == elem) {
      if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
        gpr_log(GPR_DEBUG,
                "chand=%p calld=%p: "
                "cancelling pick waiting for resolver result",
                chand, calld);
      }
      args->cancelled = true;
      subchannel_ready_locked(exec_ctx, elem,
                              GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                  "Pick cancelled", &error, 1));
    }
  }
}

static void pick_after_resolver_result_done_locked(grpc_exec_ctx *exec_ctx,
                                                   void *arg,
                                                   grpc_error *error) {
  pick_after_resolver_result_args *args = arg;
  if (args->cancelled) {
    /* cancelled, do nothing */
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG, "call cancelled before resolver result");
    }
  } else {
    grpc_call_element *elem = args->elem;
    channel_data *chand = elem->channel_data;
    call_data *calld = elem->call_data;
    grpc_call_combiner_set_notify_on_cancel(exec_ctx, calld->call_combiner,
                                            NULL);
    if (error != GRPC_ERROR_NONE) {
      if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
        gpr_log(GPR_DEBUG, "chand=%p calld=%p: resolver failed to return data",
                chand, calld);
      }
      subchannel_ready_locked(exec_ctx, elem, GRPC_ERROR_REF(error));
    } else {
      if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
        gpr_log(GPR_DEBUG, "chand=%p calld=%p: resolver returned, doing pick",
                chand, calld);
      }
      if (pick_subchannel_locked(exec_ctx, elem)) {
        subchannel_ready_locked(exec_ctx, elem, GRPC_ERROR_NONE);
      }
    }
  }
  gpr_free(args);
}

static void pick_after_resolver_result_start_locked(grpc_exec_ctx *exec_ctx,
                                                    grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
    gpr_log(GPR_DEBUG,
            "chand=%p calld=%p: deferring pick pending resolver result", chand,
            calld);
  }
  pick_after_resolver_result_args *args =
      (pick_after_resolver_result_args *)gpr_zalloc(sizeof(*args));
  args->elem = elem;
  GRPC_CLOSURE_INIT(&args->closure, pick_after_resolver_result_done_locked,
                    args, grpc_combiner_scheduler(chand->combiner));
  grpc_closure_list_append(&chand->waiting_for_resolver_result_closures,
                           &args->closure, GRPC_ERROR_NONE);
  grpc_call_combiner_set_notify_on_cancel(
      exec_ctx, calld->call_combiner,
      GRPC_CLOSURE_INIT(&calld->cancel_closure,
                        pick_after_resolver_result_cancel_locked, elem,
                        grpc_combiner_scheduler(chand->combiner)));
}

static void pick_callback_cancel_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                        grpc_error *error) {
  grpc_call_element *elem = arg;
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  if (calld->lb_policy != NULL) {
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: cancelling pick from LB policy %p",
              chand, calld, calld->lb_policy);
    }
    grpc_lb_policy_cancel_pick_locked(exec_ctx, calld->lb_policy,
                                      &calld->connected_subchannel,
                                      GRPC_ERROR_REF(error));
  }
}

// Callback invoked by grpc_lb_policy_pick_locked() for async picks.
// Unrefs the LB policy after invoking subchannel_ready_locked().
static void pick_callback_done_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_error *error) {
  grpc_call_element *elem = arg;
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
    gpr_log(GPR_DEBUG, "chand=%p calld=%p: pick completed asynchronously",
            chand, calld);
  }
  grpc_call_combiner_set_notify_on_cancel(exec_ctx, calld->call_combiner, NULL);
  GPR_ASSERT(calld->lb_policy != NULL);
  GRPC_LB_POLICY_UNREF(exec_ctx, calld->lb_policy, "pick_subchannel");
  calld->lb_policy = NULL;
  subchannel_ready_locked(exec_ctx, elem, GRPC_ERROR_REF(error));
}

// Takes a ref to chand->lb_policy and calls grpc_lb_policy_pick_locked().
// If the pick was completed synchronously, unrefs the LB policy and
// returns true.
static bool pick_callback_start_locked(grpc_exec_ctx *exec_ctx,
                                       grpc_call_element *elem,
                                       const grpc_lb_policy_pick_args *inputs) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
    gpr_log(GPR_DEBUG, "chand=%p calld=%p: starting pick on lb_policy=%p",
            chand, calld, chand->lb_policy);
  }
  // Keep a ref to the LB policy in calld while the pick is pending.
  GRPC_LB_POLICY_REF(chand->lb_policy, "pick_subchannel");
  calld->lb_policy = chand->lb_policy;
  GRPC_CLOSURE_INIT(&calld->lb_pick_closure, pick_callback_done_locked, elem,
                    grpc_combiner_scheduler(chand->combiner));
  const bool pick_done = grpc_lb_policy_pick_locked(
      exec_ctx, chand->lb_policy, inputs, &calld->connected_subchannel,
      calld->subchannel_call_context, NULL, &calld->lb_pick_closure);
  if (pick_done) {
    /* synchronous grpc_lb_policy_pick call. Unref the LB policy. */
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: pick completed synchronously",
              chand, calld);
    }
    GRPC_LB_POLICY_UNREF(exec_ctx, calld->lb_policy, "pick_subchannel");
    calld->lb_policy = NULL;
  } else {
    grpc_call_combiner_set_notify_on_cancel(
        exec_ctx, calld->call_combiner,
        GRPC_CLOSURE_INIT(&calld->cancel_closure, pick_callback_cancel_locked,
                          elem, grpc_combiner_scheduler(chand->combiner)));
  }
  return pick_done;
}

static bool pick_subchannel_locked(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *elem) {
  GPR_TIMER_BEGIN("pick_subchannel", 0);
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  bool pick_done = false;
  if (chand->lb_policy != NULL) {
    apply_service_config_to_call_locked(exec_ctx, elem);
    // If the application explicitly set wait_for_ready, use that.
    // Otherwise, if the service config specified a value for this
    // method, use that.
    //
    // The send_initial_metadata batch will be the first one in the
    // list, as set by get_batch_index() above.
    GPR_ASSERT(calld->pending_batches[0].batch != NULL);
    grpc_transport_stream_op_batch_payload *send_initial_metadata_payload =
        calld->pending_batches[0].batch->payload;
    uint32_t initial_metadata_flags =
        send_initial_metadata_payload->send_initial_metadata
            .send_initial_metadata_flags;
    const bool wait_for_ready_set_from_api =
        initial_metadata_flags &
        GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
    const bool wait_for_ready_set_from_service_config =
        calld->method_params != NULL &&
        calld->method_params->wait_for_ready != WAIT_FOR_READY_UNSET;
    if (!wait_for_ready_set_from_api &&
        wait_for_ready_set_from_service_config) {
      if (calld->method_params->wait_for_ready == WAIT_FOR_READY_TRUE) {
        initial_metadata_flags |= GRPC_INITIAL_METADATA_WAIT_FOR_READY;
      } else {
        initial_metadata_flags &= ~GRPC_INITIAL_METADATA_WAIT_FOR_READY;
      }
    }
    const grpc_lb_policy_pick_args inputs = {
        send_initial_metadata_payload->send_initial_metadata
            .send_initial_metadata,
        initial_metadata_flags, &calld->lb_token_mdelem};
    pick_done = pick_callback_start_locked(exec_ctx, elem, &inputs);
  } else if (chand->resolver != NULL) {
    if (!chand->started_resolving) {
      start_resolving_locked(exec_ctx, chand);
    }
    pick_after_resolver_result_start_locked(exec_ctx, elem);
  } else {
    subchannel_ready_locked(
        exec_ctx, elem, GRPC_ERROR_CREATE_FROM_STATIC_STRING("Disconnected"));
  }
  GPR_TIMER_END("pick_subchannel", 0);
  return pick_done;
}

static void start_subchannel_pick_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                         grpc_error *ignored) {
  grpc_call_element *elem = (grpc_call_element *)arg;
  call_data *calld = (call_data *)elem->call_data;
  channel_data *chand = (channel_data *)elem->channel_data;
  GRPC_CALL_STACK_REF(calld->owning_call, "pick_subchannel");
  /* If a subchannel is not available immediately, the polling entity from
     call_data should be provided to channel_data's interested_parties, so
     that IO of the lb_policy and resolver could be done under it. */
  if (pick_subchannel_locked(exec_ctx, elem)) {
    // Pick was returned synchronously.
    GRPC_CALL_STACK_UNREF(exec_ctx, calld->owning_call, "pick_subchannel");
    if (calld->connected_subchannel == NULL) {
      if (calld->error != GRPC_ERROR_NONE) GRPC_ERROR_UNREF(calld->error);
      calld->error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Call dropped by load balancing policy");
      pending_batches_fail(exec_ctx, elem, GRPC_ERROR_REF(calld->error));
    } else {
      // Create subchannel call.
      create_subchannel_call_locked(exec_ctx, elem, GRPC_ERROR_NONE);
    }
  } else {
    grpc_polling_entity_add_to_pollset_set(exec_ctx, calld->pollent,
                                           chand->interested_parties);
  }
}

static void start_transport_stream_op_batch_locked(grpc_exec_ctx *exec_ctx,
                                                   void *arg,
                                                   grpc_error *error_ignored) {
  GPR_TIMER_BEGIN("start_transport_stream_op_batch_locked", 0);
  grpc_transport_stream_op_batch *batch = arg;
  grpc_call_element *elem = batch->handler_private.extra_arg;
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  // If this is a cancellation, cancel the pending pick (if any) and
  // fail any pending batches.
  if (batch->cancel_stream) {

// FIXME: if retrying, need to stop all retries
// can only happen in one of the following cases:
// - we allowed an error to propagate up, in which case we've given up
//   and no longer need to retry
// - a filter above us in the parent channel stack generated an error,
//   in which case it's fine to give up on retries (might want to audit
//   this)
// - the application canceled from the API, in which case we definitely
//   want to give up on retries

    // Stash a copy of cancel_error in our call data, so that we can use
    // it for subsequent operations.  This ensures that if the call is
    // cancelled before any batches are passed down (e.g., if the deadline
    // is in the past when the call starts), we can return the right
    // error to the caller when the first batch does get passed down.
    if (calld->error != GRPC_ERROR_NONE) GRPC_ERROR_UNREF(calld->error);
    calld->error = GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: recording cancel_error=%s", chand,
              calld, grpc_error_string(calld->error));
    }
    if (calld->lb_policy != NULL) {
      pick_callback_cancel_locked(exec_ctx, elem, calld->error);
    } else {
      pick_after_resolver_result_cancel_locked(exec_ctx, elem, calld->error);
    }
    pending_batches_fail(exec_ctx, elem, GRPC_ERROR_REF(calld->error));
  } else if (batch->send_initial_metadata) {
    // For send_initial_metadata, try to pick a subchannel.
    GPR_ASSERT(calld->connected_subchannel == NULL);
    start_subchannel_pick_locked(exec_ctx, elem, GRPC_ERROR_NONE);
  }
  GPR_TIMER_END("start_transport_stream_op_batch_locked", 0);
}

// The logic here is fairly complicated, due to (a) the fact that we
// need to handle the case where we receive a send_message op before the
// send_initial_metadata op, and (b) the need for efficiency, especially in
// the streaming case.
//
// We check to see if we've already gotten a subchannel pick.  If so, we
// proceed on the fast path.  If not, we acquire the channel combiner and
// do the pick there.
static void cc_start_transport_stream_op_batch(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_transport_stream_op_batch *batch) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  if (chand->deadline_checking_enabled) {
    grpc_deadline_state_client_start_transport_stream_op_batch(exec_ctx, elem,
                                                               batch);
  }
  // Add the batch to the pending list.
  pending_batches_add(elem, batch);
  // Check if we've already gotten a subchannel call.
  GPR_TIMER_BEGIN("cc_start_transport_stream_op_batch", 0);
  if (calld->error != GRPC_ERROR_NONE) {
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: failing batch with error: %s",
              chand, calld, grpc_error_string(calld->error));
    }
    pending_batches_fail(exec_ctx, elem, GRPC_ERROR_REF(calld->error));
    goto done;
  }
  if (calld->subchannel_call != NULL) {
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG,
              "chand=%p calld=%p: sending batch to subchannel_call=%p", chand,
              calld, calld->subchannel_call);
    }
    pending_batches_resume(exec_ctx, elem);
    goto done;
  }
  // We do not yet have a subchannel call.
  // For batches containing send_initial_metadata or cancel_stream ops, enter
  // the channel combiner to either start or cancel a pick, respectively.
  if (batch->cancel_stream || batch->send_initial_metadata) {
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG, "chand=%p calld=%p: entering combiner", chand, calld);
    }
    batch->handler_private.extra_arg = elem;
    GRPC_CLOSURE_SCHED(
        exec_ctx,
        GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                          start_transport_stream_op_batch_locked, batch,
                          grpc_combiner_scheduler(chand->combiner)),
        GRPC_ERROR_NONE);
  } else {
    // For all other batches, release the call combiner.
    if (GRPC_TRACER_ON(grpc_client_channel_trace)) {
      gpr_log(GPR_DEBUG,
              "chand=%p calld=%p: saved batch, yeilding call combiner", chand,
              calld);
    }
    GRPC_CALL_COMBINER_STOP(exec_ctx, calld->call_combiner,
                            "batch does not include send_initial_metadata");
  }
done:
  GPR_TIMER_END("cc_start_transport_stream_op_batch", 0);
}

/* Constructor for call_data */
static grpc_error *cc_init_call_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_call_element *elem,
                                     const grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  // Initialize data members.
  calld->path = grpc_slice_ref_internal(args->path);
  calld->call_start_time = args->start_time;
  calld->deadline = gpr_convert_clock_type(args->deadline, GPR_CLOCK_MONOTONIC);
  calld->owning_call = args->call_stack;
  calld->arena = args->arena;
  calld->call_combiner = args->call_combiner;
  if (chand->deadline_checking_enabled) {
    grpc_deadline_state_init(exec_ctx, elem, args->call_stack,
                             args->call_combiner, calld->deadline);
  }
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void cc_destroy_call_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_call_element *elem,
                                 const grpc_call_final_info *final_info,
                                 grpc_closure *then_schedule_closure) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  if (chand->deadline_checking_enabled) {
    grpc_deadline_state_destroy(exec_ctx, elem);
  }
  grpc_slice_unref_internal(exec_ctx, calld->path);
  if (calld->method_params != NULL) {
    if (calld->method_params->retry_policy != NULL) {
      retry_committed(exec_ctx, calld);
    }
    method_parameters_unref(calld->method_params);
  }
  GRPC_ERROR_UNREF(calld->error);
  if (calld->subchannel_call != NULL) {
    grpc_subchannel_call_set_cleanup_closure(calld->subchannel_call,
                                             then_schedule_closure);
    then_schedule_closure = NULL;
    GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, calld->subchannel_call,
                               "client_channel_destroy_call");
  }
  GPR_ASSERT(calld->lb_policy == NULL);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    GPR_ASSERT(calld->pending_batches[i].batch == NULL);
  }
  if (calld->connected_subchannel != NULL) {
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, calld->connected_subchannel,
                                    "picked");
  }
  for (size_t i = 0; i < GRPC_CONTEXT_COUNT; ++i) {
    if (calld->subchannel_call_context[i].value != NULL) {
      calld->subchannel_call_context[i].destroy(
          calld->subchannel_call_context[i].value);
    }
  }
  GRPC_CLOSURE_SCHED(exec_ctx, then_schedule_closure, GRPC_ERROR_NONE);
}

static void cc_set_pollset_or_pollset_set(grpc_exec_ctx *exec_ctx,
                                          grpc_call_element *elem,
                                          grpc_polling_entity *pollent) {
  call_data *calld = elem->call_data;
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

static void try_to_connect_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                  grpc_error *error_ignored) {
  channel_data *chand = arg;
  if (chand->lb_policy != NULL) {
    grpc_lb_policy_exit_idle_locked(exec_ctx, chand->lb_policy);
  } else {
    chand->exit_idle_when_lb_policy_arrives = true;
    if (!chand->started_resolving && chand->resolver != NULL) {
      start_resolving_locked(exec_ctx, chand);
    }
  }
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, chand->owning_stack, "try_to_connect");
}

grpc_connectivity_state grpc_client_channel_check_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem, int try_to_connect) {
  channel_data *chand = elem->channel_data;
  grpc_connectivity_state out =
      grpc_connectivity_state_check(&chand->state_tracker);
  if (out == GRPC_CHANNEL_IDLE && try_to_connect) {
    GRPC_CHANNEL_STACK_REF(chand->owning_stack, "try_to_connect");
    GRPC_CLOSURE_SCHED(
        exec_ctx, GRPC_CLOSURE_CREATE(try_to_connect_locked, chand,
                                      grpc_combiner_scheduler(chand->combiner)),
        GRPC_ERROR_NONE);
  }
  return out;
}

typedef struct external_connectivity_watcher {
  channel_data *chand;
  grpc_polling_entity pollent;
  grpc_closure *on_complete;
  grpc_closure *watcher_timer_init;
  grpc_connectivity_state *state;
  grpc_closure my_closure;
  struct external_connectivity_watcher *next;
} external_connectivity_watcher;

static external_connectivity_watcher *lookup_external_connectivity_watcher(
    channel_data *chand, grpc_closure *on_complete) {
  gpr_mu_lock(&chand->external_connectivity_watcher_list_mu);
  external_connectivity_watcher *w =
      chand->external_connectivity_watcher_list_head;
  while (w != NULL && w->on_complete != on_complete) {
    w = w->next;
  }
  gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);
  return w;
}

static void external_connectivity_watcher_list_append(
    channel_data *chand, external_connectivity_watcher *w) {
  GPR_ASSERT(!lookup_external_connectivity_watcher(chand, w->on_complete));

  gpr_mu_lock(&w->chand->external_connectivity_watcher_list_mu);
  GPR_ASSERT(!w->next);
  w->next = chand->external_connectivity_watcher_list_head;
  chand->external_connectivity_watcher_list_head = w;
  gpr_mu_unlock(&w->chand->external_connectivity_watcher_list_mu);
}

static void external_connectivity_watcher_list_remove(
    channel_data *chand, external_connectivity_watcher *too_remove) {
  GPR_ASSERT(
      lookup_external_connectivity_watcher(chand, too_remove->on_complete));
  gpr_mu_lock(&chand->external_connectivity_watcher_list_mu);
  if (too_remove == chand->external_connectivity_watcher_list_head) {
    chand->external_connectivity_watcher_list_head = too_remove->next;
    gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);
    return;
  }
  external_connectivity_watcher *w =
      chand->external_connectivity_watcher_list_head;
  while (w != NULL) {
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
    grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  int count = 0;

  gpr_mu_lock(&chand->external_connectivity_watcher_list_mu);
  external_connectivity_watcher *w =
      chand->external_connectivity_watcher_list_head;
  while (w != NULL) {
    count++;
    w = w->next;
  }
  gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);

  return count;
}

static void on_external_watch_complete(grpc_exec_ctx *exec_ctx, void *arg,
                                       grpc_error *error) {
  external_connectivity_watcher *w = arg;
  grpc_closure *follow_up = w->on_complete;
  grpc_polling_entity_del_from_pollset_set(exec_ctx, &w->pollent,
                                           w->chand->interested_parties);
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, w->chand->owning_stack,
                           "external_connectivity_watcher");
  external_connectivity_watcher_list_remove(w->chand, w);
  gpr_free(w);
  GRPC_CLOSURE_RUN(exec_ctx, follow_up, GRPC_ERROR_REF(error));
}

static void watch_connectivity_state_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                            grpc_error *error_ignored) {
  external_connectivity_watcher *w = arg;
  external_connectivity_watcher *found = NULL;
  if (w->state != NULL) {
    external_connectivity_watcher_list_append(w->chand, w);
    GRPC_CLOSURE_RUN(exec_ctx, w->watcher_timer_init, GRPC_ERROR_NONE);
    GRPC_CLOSURE_INIT(&w->my_closure, on_external_watch_complete, w,
                      grpc_schedule_on_exec_ctx);
    grpc_connectivity_state_notify_on_state_change(
        exec_ctx, &w->chand->state_tracker, w->state, &w->my_closure);
  } else {
    GPR_ASSERT(w->watcher_timer_init == NULL);
    found = lookup_external_connectivity_watcher(w->chand, w->on_complete);
    if (found) {
      GPR_ASSERT(found->on_complete == w->on_complete);
      grpc_connectivity_state_notify_on_state_change(
          exec_ctx, &found->chand->state_tracker, NULL, &found->my_closure);
    }
    grpc_polling_entity_del_from_pollset_set(exec_ctx, &w->pollent,
                                             w->chand->interested_parties);
    GRPC_CHANNEL_STACK_UNREF(exec_ctx, w->chand->owning_stack,
                             "external_connectivity_watcher");
    gpr_free(w);
  }
}

void grpc_client_channel_watch_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
    grpc_polling_entity pollent, grpc_connectivity_state *state,
    grpc_closure *closure, grpc_closure *watcher_timer_init) {
  channel_data *chand = elem->channel_data;
  external_connectivity_watcher *w = gpr_zalloc(sizeof(*w));
  w->chand = chand;
  w->pollent = pollent;
  w->on_complete = closure;
  w->state = state;
  w->watcher_timer_init = watcher_timer_init;
  grpc_polling_entity_add_to_pollset_set(exec_ctx, &w->pollent,
                                         chand->interested_parties);
  GRPC_CHANNEL_STACK_REF(w->chand->owning_stack,
                         "external_connectivity_watcher");
  GRPC_CLOSURE_SCHED(
      exec_ctx,
      GRPC_CLOSURE_INIT(&w->my_closure, watch_connectivity_state_locked, w,
                        grpc_combiner_scheduler(chand->combiner)),
      GRPC_ERROR_NONE);
}
