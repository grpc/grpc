/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

static void *method_parameters_copy(void *value) {
  return method_parameters_ref(value);
}

static void method_parameters_free(grpc_exec_ctx *exec_ctx, void *value) {
  method_parameters_unref(value);
}

static const grpc_slice_hash_table_vtable method_parameters_vtable = {
    method_parameters_free, method_parameters_copy};

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
  /** a list of closures that are all waiting for config to come in */
  grpc_closure_list waiting_for_config_closures;
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
  grpc_connectivity_state_set(exec_ctx, &chand->state_tracker, state, error,
                              reason);
}

static void on_lb_policy_state_changed_locked(grpc_exec_ctx *exec_ctx,
                                              void *arg, grpc_error *error) {
  lb_policy_connectivity_watcher *w = arg;
  grpc_connectivity_state publish_state = w->state;
  /* check if the notification is for the latest policy */
  if (w->lb_policy == w->chand->lb_policy) {
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
  grpc_closure_init(&w->on_changed, on_lb_policy_state_changed_locked, w,
                    grpc_combiner_scheduler(chand->combiner, false));
  w->state = current_state;
  w->lb_policy = lb_policy;
  grpc_lb_policy_notify_on_state_change_locked(exec_ctx, lb_policy, &w->state,
                                               &w->on_changed);
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

// Wrap a closure associated with \a lb_policy. The associated callback (\a
// wrapped_on_pick_closure_cb) is responsible for unref'ing \a lb_policy after
// scheduling \a wrapped_closure.
typedef struct wrapped_on_pick_closure_arg {
  /* the closure instance using this struct as argument */
  grpc_closure wrapper_closure;

  /* the original closure. Usually a on_complete/notify cb for pick() and ping()
   * calls against the internal RR instance, respectively. */
  grpc_closure *wrapped_closure;

  /* The policy instance related to the closure */
  grpc_lb_policy *lb_policy;
} wrapped_on_pick_closure_arg;

// Invoke \a arg->wrapped_closure, unref \a arg->lb_policy and free \a arg.
static void wrapped_on_pick_closure_cb(grpc_exec_ctx *exec_ctx, void *arg,
                                       grpc_error *error) {
  wrapped_on_pick_closure_arg *wc_arg = arg;
  GPR_ASSERT(wc_arg != NULL);
  GPR_ASSERT(wc_arg->wrapped_closure != NULL);
  GPR_ASSERT(wc_arg->lb_policy != NULL);
  grpc_closure_run(exec_ctx, wc_arg->wrapped_closure, GRPC_ERROR_REF(error));
  GRPC_LB_POLICY_UNREF(exec_ctx, wc_arg->lb_policy, "pick_subchannel_wrapping");
  gpr_free(wc_arg);
}

static void on_resolver_result_changed_locked(grpc_exec_ctx *exec_ctx,
                                              void *arg, grpc_error *error) {
  channel_data *chand = arg;
  char *lb_policy_name = NULL;
  grpc_lb_policy *lb_policy = NULL;
  grpc_lb_policy *old_lb_policy;
  grpc_slice_hash_table *method_params_table = NULL;
  grpc_connectivity_state state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  bool exit_idle = false;
  grpc_error *state_error =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("No load balancing policy");
  char *service_config_json = NULL;
  service_config_parsing_state parsing_state;
  memset(&parsing_state, 0, sizeof(parsing_state));

  if (chand->resolver_result != NULL) {
    // Find LB policy name.
    const grpc_arg *channel_arg =
        grpc_channel_args_find(chand->resolver_result, GRPC_ARG_LB_POLICY_NAME);
    if (channel_arg != NULL) {
      GPR_ASSERT(channel_arg->type == GRPC_ARG_STRING);
      lb_policy_name = channel_arg->value.string;
    }
    // Special case: If all of the addresses are balancer addresses,
    // assume that we should use the grpclb policy, regardless of what the
    // resolver actually specified.
    channel_arg =
        grpc_channel_args_find(chand->resolver_result, GRPC_ARG_LB_ADDRESSES);
    if (channel_arg != NULL && channel_arg->type == GRPC_ARG_POINTER) {
      grpc_lb_addresses *addresses = channel_arg->value.pointer.p;
      bool found_backend_address = false;
      for (size_t i = 0; i < addresses->num_addresses; ++i) {
        if (!addresses->addresses[i].is_balancer) {
          found_backend_address = true;
          break;
        }
      }
      if (!found_backend_address) {
        if (lb_policy_name != NULL && strcmp(lb_policy_name, "grpclb") != 0) {
          gpr_log(GPR_INFO,
                  "resolver requested LB policy %s but provided only balancer "
                  "addresses, no backend addresses -- forcing use of grpclb LB "
                  "policy",
                  lb_policy_name);
        }
        lb_policy_name = "grpclb";
      }
    }
    // Use pick_first if nothing was specified and we didn't select grpclb
    // above.
    if (lb_policy_name == NULL) lb_policy_name = "pick_first";
    // Instantiate LB policy.
    grpc_lb_policy_args lb_policy_args;
    lb_policy_args.args = chand->resolver_result;
    lb_policy_args.client_channel_factory = chand->client_channel_factory;
    lb_policy_args.combiner = chand->combiner;
    lb_policy =
        grpc_lb_policy_create(exec_ctx, lb_policy_name, &lb_policy_args);
    if (lb_policy != NULL) {
      GRPC_LB_POLICY_REF(lb_policy, "config_change");
      GRPC_ERROR_UNREF(state_error);
      state = grpc_lb_policy_check_connectivity_locked(exec_ctx, lb_policy,
                                                       &state_error);
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
        parsing_state.server_name =
            uri->path[0] == '/' ? uri->path + 1 : uri->path;
        grpc_service_config_parse_global_params(
            service_config, parse_retry_throttle_params, &parsing_state);
        parsing_state.server_name = NULL;
        grpc_uri_destroy(uri);
        method_params_table = grpc_service_config_create_method_config_table(
            exec_ctx, service_config, method_parameters_create_from_json,
            &method_parameters_vtable);
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

  if (lb_policy != NULL) {
    grpc_pollset_set_add_pollset_set(exec_ctx, lb_policy->interested_parties,
                                     chand->interested_parties);
  }

  gpr_mu_lock(&chand->info_mu);
  if (lb_policy_name != NULL) {
    gpr_free(chand->info_lb_policy_name);
    chand->info_lb_policy_name = lb_policy_name;
  }
  old_lb_policy = chand->lb_policy;
  chand->lb_policy = lb_policy;
  if (service_config_json != NULL) {
    gpr_free(chand->info_service_config_json);
    chand->info_service_config_json = service_config_json;
  }
  gpr_mu_unlock(&chand->info_mu);

  if (chand->retry_throttle_data != NULL) {
    grpc_server_retry_throttle_data_unref(chand->retry_throttle_data);
  }
  chand->retry_throttle_data = parsing_state.retry_throttle_data;
  if (chand->method_params_table != NULL) {
    grpc_slice_hash_table_unref(exec_ctx, chand->method_params_table);
  }
  chand->method_params_table = method_params_table;
  if (lb_policy != NULL) {
    grpc_closure_list_sched(exec_ctx, &chand->waiting_for_config_closures);
  } else if (chand->resolver == NULL /* disconnected */) {
    grpc_closure_list_fail_all(&chand->waiting_for_config_closures,
                               GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                   "Channel disconnected", &error, 1));
    grpc_closure_list_sched(exec_ctx, &chand->waiting_for_config_closures);
  }
  if (lb_policy != NULL && chand->exit_idle_when_lb_policy_arrives) {
    GRPC_LB_POLICY_REF(lb_policy, "exit_idle");
    exit_idle = true;
    chand->exit_idle_when_lb_policy_arrives = false;
  }

  if (error == GRPC_ERROR_NONE && chand->resolver) {
    set_channel_connectivity_state_locked(
        exec_ctx, chand, state, GRPC_ERROR_REF(state_error), "new_lb+resolver");
    if (lb_policy != NULL) {
      watch_lb_policy_locked(exec_ctx, chand, lb_policy, state);
    }
    GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
    grpc_resolver_next_locked(exec_ctx, chand->resolver,
                              &chand->resolver_result,
                              &chand->on_resolver_result_changed);
  } else {
    if (chand->resolver != NULL) {
      grpc_resolver_shutdown_locked(exec_ctx, chand->resolver);
      GRPC_RESOLVER_UNREF(exec_ctx, chand->resolver, "channel");
      chand->resolver = NULL;
    }
    grpc_error *refs[] = {error, state_error};
    set_channel_connectivity_state_locked(
        exec_ctx, chand, GRPC_CHANNEL_SHUTDOWN,
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "Got config after disconnection", refs, GPR_ARRAY_SIZE(refs)),
        "resolver_gone");
  }

  if (exit_idle) {
    grpc_lb_policy_exit_idle_locked(exec_ctx, lb_policy);
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "exit_idle");
  }

  if (old_lb_policy != NULL) {
    grpc_pollset_set_del_pollset_set(
        exec_ctx, old_lb_policy->interested_parties, chand->interested_parties);
    GRPC_LB_POLICY_UNREF(exec_ctx, old_lb_policy, "channel");
  }

  if (lb_policy != NULL) {
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "config_change");
  }

  GRPC_CHANNEL_STACK_UNREF(exec_ctx, chand->owning_stack, "resolver");
  GRPC_ERROR_UNREF(state_error);
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
      grpc_closure_sched(
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
        grpc_closure_list_fail_all(&chand->waiting_for_config_closures,
                                   GRPC_ERROR_REF(op->disconnect_with_error));
        grpc_closure_list_sched(exec_ctx, &chand->waiting_for_config_closures);
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

  grpc_closure_sched(exec_ctx, op->on_consumed, GRPC_ERROR_NONE);
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
  grpc_closure_sched(
      exec_ctx,
      grpc_closure_init(&op->handler_private.closure, start_transport_op_locked,
                        op, grpc_combiner_scheduler(chand->combiner, false)),
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
  chand->combiner = grpc_combiner_create(NULL);
  gpr_mu_init(&chand->info_mu);
  chand->owning_stack = args->channel_stack;
  grpc_closure_init(&chand->on_resolver_result_changed,
                    on_resolver_result_changed_locked, chand,
                    grpc_combiner_scheduler(chand->combiner, false));
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
    grpc_closure_sched(
        exec_ctx,
        grpc_closure_create(shutdown_resolver_locked, chand->resolver,
                            grpc_combiner_scheduler(chand->combiner, false)),
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
}

/*************************************************************************
 * PER-CALL FUNCTIONS
 */

#define GET_CALL(call_data) \
  ((grpc_subchannel_call *)(gpr_atm_acq_load(&(call_data)->subchannel_call)))

#define CANCELLED_CALL ((grpc_subchannel_call *)1)

typedef enum {
  /* zero so that it can be default-initialized */
  GRPC_CLIENT_CHANNEL_NOT_PICKING = 0,
  GRPC_CLIENT_CHANNEL_PICKING_SUBCHANNEL
} subchannel_creation_phase;

// The maximum number of concurrent batches possible.
// Based upon the maximum number of individually queueable ops in the batch
// API:
// - send_initial_metadata
// - send_message
// - send_trailing_metadata
// - recv_initial_metadata
// - recv_message
// - recv_trailing_metadata
#define MAX_CONCURRENT_BATCHES 6

// State used for sending a retryable batch down to a subchannel call.
typedef struct {
  grpc_call_element *elem;
  grpc_subchannel_call *subchannel_call;
  // The batch to use in the subchannel call.
  // Its payload field points to subchannel_call_retry_state.batch_payload.
  grpc_transport_stream_op_batch batch;
  // For send_message.
  grpc_multi_attempt_byte_stream send_message;
  // For intercepting recv_initial_metadata.
  grpc_metadata_batch recv_initial_metadata;
  grpc_closure recv_initial_metadata_ready;
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
typedef struct {
  // These fields indicate which ops have been sent down to this
  // subchannel call.
  gpr_atm send_initial_metadata;  // bool
  gpr_atm send_message_count;  // size_t
  gpr_atm send_trailing_metadata;  // bool
  gpr_atm recv_initial_metadata;  // bool
  gpr_atm recv_message;  // bool
  gpr_atm recv_trailing_metadata;  // bool
  // subchannel_batch_data.batch.payload points to this.
  grpc_transport_stream_op_batch_payload batch_payload;
  gpr_atm retry_dispatched;  // bool
} subchannel_call_retry_state;

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
  // stack and each has its own mutex.  If/when we have time, find a way
  // to avoid this without breaking the grpc_deadline_state abstraction.
  grpc_deadline_state deadline_state;

  grpc_slice path;  // Request path.
  gpr_timespec call_start_time;
  gpr_timespec deadline;
  grpc_server_retry_throttle_data *retry_throttle_data;
  method_parameters *method_params;

  grpc_error *cancel_error;

  /** either 0 for no call, 1 for cancelled, or a pointer to a
      grpc_subchannel_call */
  gpr_atm subchannel_call;
  gpr_arena *arena;

  subchannel_creation_phase creation_phase;
  grpc_connected_subchannel *connected_subchannel;
  grpc_polling_entity *pollent;

  grpc_closure_list waiting_list;

  grpc_closure next_step;

  grpc_call_stack *owning_call;

  grpc_linked_mdelem lb_token_mdelem;

  // Retry state.
  bool retry_committed;
  int num_retry_attempts;
  size_t bytes_buffered_for_retry;
  grpc_call_context_element *context;
  // Batches received from above that still have a on_complete,
  // recv_initial_metadata_ready, or recv_message_ready callback pending.
// FIXME: should this be an array of gpr_atm's?
  grpc_transport_stream_op_batch* pending_batches[MAX_CONCURRENT_BATCHES];
  // Copy of initial metadata.
  // Populated when we receive a send_initial_metadata op.
  grpc_linked_mdelem *send_initial_metadata_storage;
  grpc_metadata_batch send_initial_metadata;
  uint32_t send_initial_metadata_flags;
  // The contents for sent messages.
  // When we get a send_message op, we replace the original byte stream
  // with a grpc_multi_attempt_byte_stream that caches the slices to a
  // local buffer for use in retries.  We use initial_send_message as the
  // cache for the first send_message op, so that we don't need to allocate
  // memory for unary RPCs.  All subsequent messages are stored in
  // send_messages, which are dynamically allocated as needed.
  grpc_multi_attempt_byte_stream_cache initial_send_message;
  grpc_multi_attempt_byte_stream_cache *send_messages;
// FIXME: does this need to be a gpr_atm?
  size_t num_send_message_ops;
  // Non-NULL if we've received a send_trailing_metadata op.
  grpc_metadata_batch *send_trailing_metadata;
} call_data;

grpc_subchannel_call *grpc_client_channel_get_subchannel_call(
    grpc_call_element *call_elem) {
  grpc_subchannel_call *scc = GET_CALL((call_data *)call_elem->call_data);
  return scc == CANCELLED_CALL ? NULL : scc;
}

static void start_subchannel_batch(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *elem,
                                   grpc_subchannel_call *subchannel_call,
                                   grpc_transport_stream_op_batch *batch);

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
    grpc_multi_attempt_byte_stream_cache_destroy(exec_ctx,
                                                 &calld->initial_send_message);
  }
  for (int i = 0; i < (int)calld->num_send_message_ops - 2; ++i) {
    grpc_multi_attempt_byte_stream_cache_destroy(exec_ctx,
                                                 &calld->send_messages[i]);
  }
  gpr_free(calld->send_messages);
}

static size_t get_batch_index(grpc_transport_stream_op_batch *batch) {
  if (batch->send_initial_metadata) return 0;
  if (batch->send_message) return 1;
  if (batch->send_trailing_metadata) return 2;
  if (batch->recv_initial_metadata) return 3;
  if (batch->recv_message) return 4;
  if (batch->recv_trailing_metadata) return 5;
  GPR_UNREACHABLE_CODE(return (size_t)-1);
}

static grpc_multi_attempt_byte_stream_cache *get_send_message_cache(
    call_data *calld, size_t index) {
  GPR_ASSERT(index < calld->num_send_message_ops);
  return index == 0
         ? &calld->initial_send_message
         : &calld->send_messages[index - 1];
}

// If retries are configured, checks to see if this exceeds the retry
// buffer limit.  If it doesn't exceed the limit, adds the batch to
// calld->pending_batches and caches data for send ops (if any).
//
// FIXME: this is called in two places, in cc_start_transport_stream_op_batch()
// and in process_waiting_batch().  the second one is needed because the
// service config may not yet have been available when we first got the
// batch from the surface.  but need to make sure we don't do it twice!
static void retry_checks_for_new_batch(grpc_exec_ctx *exec_ctx,
                                       grpc_call_element *elem,
                                       grpc_transport_stream_op_batch *batch) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
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
    } else if (!batch->cancel_stream) {
      const size_t idx = get_batch_index(batch);
gpr_log(GPR_INFO, "STORING batch in pending_batches[%zu]", idx);
      GPR_ASSERT(calld->pending_batches[idx] == NULL);
      calld->pending_batches[idx] = batch;
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
              sizeof(grpc_multi_attempt_byte_stream_cache) *
                  calld->num_send_message_ops);
        }
        ++calld->num_send_message_ops;
        grpc_multi_attempt_byte_stream_cache *cache =
            get_send_message_cache(calld, calld->num_send_message_ops - 1);
        grpc_multi_attempt_byte_stream_cache_init(
            cache, batch->payload->send_message.send_message);
      }
      // Save metadata batch for send_trailing_metadata ops.
      if (batch->send_trailing_metadata) {
        calld->send_trailing_metadata =
            batch->payload->send_trailing_metadata.send_trailing_metadata;
      }
    }
  }
}

static void process_waiting_batch(grpc_exec_ctx *exec_ctx, void *arg,
                                  grpc_error *error) {
  grpc_transport_stream_op_batch *batch = arg;
  grpc_call_element *elem = batch->handler_private.extra_arg;
  call_data *calld = elem->call_data;
  if (error != GRPC_ERROR_NONE) {
    grpc_transport_stream_op_batch_finish_with_failure(
        exec_ctx, batch, GRPC_ERROR_REF(error));
    return;
  }
  grpc_subchannel_call *call = GET_CALL(calld);
  if (call == CANCELLED_CALL) {
    grpc_transport_stream_op_batch_finish_with_failure(
        exec_ctx, batch, GRPC_ERROR_REF(error));
    return;
  }
  retry_checks_for_new_batch(exec_ctx, elem, batch);
  start_subchannel_batch(exec_ctx, elem, call, batch);
}

static void add_waiting_batch_locked(grpc_call_element* elem,
                                     grpc_transport_stream_op_batch *batch) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  GPR_TIMER_BEGIN("add_waiting_batch_locked", 0);
  batch->handler_private.extra_arg = elem;
  grpc_closure_init(&batch->handler_private.closure, process_waiting_batch,
                    batch, grpc_combiner_scheduler(chand->combiner, true));
  grpc_closure_list_append(&calld->waiting_list,
                           &batch->handler_private.closure, GRPC_ERROR_NONE);
  GPR_TIMER_END("add_waiting_batch_locked", 0);
}

static void fail_waiting_locked(grpc_exec_ctx *exec_ctx, call_data *calld,
                                grpc_error *error) {
  grpc_closure_list_fail_all(&calld->waiting_list, error);
  grpc_closure_list_sched(exec_ctx, &calld->waiting_list);
}

static void release_waiting_locked(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  grpc_closure_list_sched(exec_ctx, &calld->waiting_list);
}

static void maybe_clear_pending_batch(call_data *calld, size_t batch_index) {
  grpc_transport_stream_op_batch *original_batch =
      calld->pending_batches[batch_index];
  if (original_batch->on_complete == NULL &&
      (!original_batch->recv_initial_metadata ||
       original_batch->payload->recv_initial_metadata.recv_initial_metadata
           == NULL) &&
      (!original_batch->recv_message ||
       original_batch->payload->recv_message.recv_message_ready == NULL)) {
gpr_log(GPR_INFO, "CLEARING pending_batches[%zu]", batch_index);
    calld->pending_batches[batch_index] = NULL;
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

static void start_transport_stream_op_batch_locked(grpc_exec_ctx *exec_ctx,
                                                   void *arg,
                                                   grpc_error *error_ignored);

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
    subchannel_call_retry_state *retry_state =
        grpc_connected_subchannel_call_get_parent_data(
            batch_data->subchannel_call);
    if (!gpr_atm_full_cas(&retry_state->retry_dispatched, (gpr_atm)0,
                          (gpr_atm)1)) {
      return true;
    }
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
// FIXME: compute backoff delay.  if the backoff delay is less than the
// deadline, then start a timer to start the next retry attempt
      grpc_subchannel_call *call = GET_CALL(calld);
      if (call != NULL && call != CANCELLED_CALL) {
        GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, call,
                                   "client_channel_call_retry");
        gpr_atm_rel_store(&calld->subchannel_call, (gpr_atm)NULL);
      }
// FIXME: this isn't exactly the right callback to use here, since
// start_transport_stream_op_batch_locked() assumes that it has a batch
// to send down and makes its decisions based on that.  right now, we're
// punting by sending it the batch that just failed, but that's not really
// the right thing to do, because we might receive new ops from the surface
// while we're doing the pick, and we'd want to include those in the retry
// attempt once the pick comes back.  need to either write a new version of
// start_transport_stream_op_batch_locked() to handle this case or
// generalize the existing code so that it knows about not having a
// specific batch available in the retry case.
      GRPC_CALL_STACK_REF(calld->owning_call, "maybe_retry");
      batch_data->batch.handler_private.extra_arg = elem;
      grpc_closure_sched(
          exec_ctx,
          grpc_closure_init(&batch_data->batch.handler_private.closure,
                            start_transport_stream_op_batch_locked,
                            &batch_data->batch,
                            grpc_combiner_scheduler(chand->combiner, false)),
          GRPC_ERROR_NONE);
      ++calld->num_retry_attempts;
      return true;
    }
// FIXME: else set calld->retry_committed?
  }
  return false;
}

// Intercepts recv_initial_metadata_ready callback for retries.
// Commits the call and returns the initial metadata up the stack.
static void recv_initial_metadata_ready(grpc_exec_ctx *exec_ctx, void *arg,
                                        grpc_error *error) {
gpr_log(GPR_INFO, "==> recv_initial_metadata_ready()");
  subchannel_batch_data *batch_data = arg;
  call_data *calld = batch_data->elem->call_data;
  // If we got an error, attempt to retry the call.
  if (error != GRPC_ERROR_NONE) {
    grpc_status_code status;
    grpc_error_get_status(error, calld->deadline, &status, NULL, NULL);
    if (maybe_retry(exec_ctx, batch_data, status)) return;
  } else {
    // No error, so commit the call.
gpr_log(GPR_INFO, "recv_initial_metadata_ready() commit");
    retry_committed(exec_ctx, calld);
  }
  // Find pending batch.
  size_t batch_index = 0;
  for (; batch_index < GPR_ARRAY_SIZE(calld->pending_batches); ++batch_index) {
    if (calld->pending_batches[batch_index] != NULL &&
        calld->pending_batches[batch_index]->recv_initial_metadata &&
        calld->pending_batches[batch_index]->payload->recv_initial_metadata
            .recv_initial_metadata_ready != NULL) {
      break;
    }
  }
  GPR_ASSERT(batch_index < GPR_ARRAY_SIZE(calld->pending_batches));
  grpc_transport_stream_op_batch *original_batch =
      calld->pending_batches[batch_index];
  grpc_metadata_batch_move(
      &batch_data->recv_initial_metadata,
      original_batch->payload->recv_initial_metadata.recv_initial_metadata);
  grpc_closure_run(
      exec_ctx,
      original_batch->payload->recv_initial_metadata
          .recv_initial_metadata_ready,
      GRPC_ERROR_REF(error));
gpr_log(GPR_INFO, "CLEARING pending_batches[%zu]->recv_initial_metadata_ready", batch_index);
  original_batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
      NULL;
  maybe_clear_pending_batch(calld, batch_index);
  GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, batch_data->subchannel_call,
                             "client_channel_recv_initial_metadata_ready");
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
  size_t batch_index = 0;
  for (; batch_index < GPR_ARRAY_SIZE(calld->pending_batches); ++batch_index) {
    if (calld->pending_batches[batch_index] != NULL &&
        calld->pending_batches[batch_index]->recv_message &&
        calld->pending_batches[batch_index]->payload->recv_message
            .recv_message_ready != NULL) {
      break;
    }
  }
  GPR_ASSERT(batch_index < GPR_ARRAY_SIZE(calld->pending_batches));
  grpc_transport_stream_op_batch *original_batch =
      calld->pending_batches[batch_index];
  *original_batch->payload->recv_message.recv_message =
      batch_data->recv_message;
  grpc_closure_run(
      exec_ctx,
      original_batch->payload->recv_message.recv_message_ready,
      GRPC_ERROR_REF(error));
gpr_log(GPR_INFO, "CLEARING pending_batches[%zu]->recv_message_ready", batch_index);
  original_batch->payload->recv_message.recv_message_ready = NULL;
  maybe_clear_pending_batch(calld, batch_index);
  GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, batch_data->subchannel_call,
                             "client_channel_recv_message_ready");
}

// Returns the index into calld->pending_batches of the batch matching
// subchannel_batch, or -1 if no matching batch was found.
// Note that we do not match against a batch containing a send_message
// op unless this is the last send_message op, because we don't want to
// complete the batch if we're just replaying an already-reported-complete
// send_message op for a retry.
static size_t get_matching_pending_batch_index(
    call_data *calld, grpc_transport_stream_op_batch *subchannel_batch,
    bool is_last_send_message) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(calld->pending_batches); ++i) {
    if (calld->pending_batches[i] != NULL &&
        calld->pending_batches[i]->on_complete != NULL &&
        subchannel_batch->send_initial_metadata ==
            calld->pending_batches[i]->send_initial_metadata &&
        (!subchannel_batch->send_message ||
         (calld->pending_batches[i]->send_message && is_last_send_message)) &&
        subchannel_batch->send_trailing_metadata ==
            calld->pending_batches[i]->send_trailing_metadata &&
        subchannel_batch->recv_initial_metadata ==
            calld->pending_batches[i]->recv_initial_metadata &&
        subchannel_batch->recv_message ==
            calld->pending_batches[i]->recv_message &&
        subchannel_batch->recv_trailing_metadata ==
            calld->pending_batches[i]->recv_trailing_metadata) {
      return i;
    }
  }
  return (size_t)-1;
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
  const size_t send_message_count =
      (size_t)gpr_atm_acq_load(&retry_state->send_message_count);
  const bool have_pending_send_message_ops =
      send_message_count < calld->num_send_message_ops;
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
  }
  // Case 4: call is not yet complete.
  else {
    if (have_pending_send_message_ops) {
gpr_log(GPR_INFO, "starting next batch for pending send_message ops");
      start_subchannel_batch(exec_ctx, elem, batch_data->subchannel_call,
                             NULL /* batch */);
    }
  }
  // Call succeeded or is not retryable.  Return back up the stack if needed.
  size_t idx = get_matching_pending_batch_index(calld, &batch_data->batch,
                                                !have_pending_send_message_ops);
  if (idx != (size_t)-1) {
gpr_log(GPR_INFO, "calling original on_complete");
    grpc_closure_run(exec_ctx, calld->pending_batches[idx]->on_complete,
                     GRPC_ERROR_REF(error));
gpr_log(GPR_INFO, "CLEARING pending_batches[%zu]->on_complete", idx);
    calld->pending_batches[idx]->on_complete = NULL;
    maybe_clear_pending_batch(calld, idx);
  }
  GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, batch_data->subchannel_call,
                             "client_channel_on_complete");
}

// batch will be NULL if this is a retry.
// FIXME: need to make sure that this function is thread-safe w.r.t. new
// ops coming down and recording data in retry_checks_for_new_batch().
static void start_subchannel_batch(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *elem,
                                   grpc_subchannel_call *subchannel_call,
                                   grpc_transport_stream_op_batch *batch) {
  call_data *calld = elem->call_data;
gpr_log(GPR_INFO, "method_params=%p", calld->method_params);
if (calld->method_params) gpr_log(GPR_INFO, "method_params->retry_policy=%p", calld->method_params->retry_policy);
gpr_log(GPR_INFO, "retry_committed=%d", calld->retry_committed);
  if (calld->method_params != NULL &&
      calld->method_params->retry_policy != NULL && !calld->retry_committed) {
gpr_log(GPR_INFO, "RETRIES CONFIGURED");
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
      grpc_transport_stream_op_batch *pending_batch = calld->pending_batches[i];
      if (pending_batch == NULL) continue;
      if (pending_batch->send_initial_metadata) send_initial_metadata_idx = i;
      if (pending_batch->send_trailing_metadata) send_trailing_metadata_idx = i;
      if (pending_batch->recv_initial_metadata) recv_initial_metadata_idx = i;
      if (pending_batch->recv_message) recv_message_idx = i;
      if (pending_batch->recv_trailing_metadata) recv_trailing_metadata_idx = i;
    }
// FIXME: if there are no ops that we haven't yet sent, return without
// doing anything
    // Construct a batch of ops to send on this subchannel call.
    subchannel_call_retry_state *retry_state =
        grpc_connected_subchannel_call_get_parent_data(subchannel_call);
    subchannel_batch_data *batch_data =
        gpr_arena_alloc(calld->arena, sizeof(*batch_data));
    batch_data->elem = elem;
    batch_data->subchannel_call = GRPC_SUBCHANNEL_CALL_REF(
        subchannel_call, "client_channel_start_subchannel_batch");
    batch_data->batch.payload = &retry_state->batch_payload;
    // send_initial_metadata.
    if (send_initial_metadata_idx != INT_MAX &&
        gpr_atm_full_cas(&retry_state->send_initial_metadata, (gpr_atm)0,
                         (gpr_atm)1)) {
      batch_data->batch.send_initial_metadata = true;
      batch_data->batch.payload->send_initial_metadata.send_initial_metadata =
          &calld->send_initial_metadata;
      batch_data->batch.payload->send_initial_metadata
          .send_initial_metadata_flags =
              calld->send_initial_metadata_flags;
    }
    // send_message.
// FIXME: if we get a new send_message op while there's one already
// pending (e.g., if we told the surface that the first send_message
// succeeded and then we had to retry and had already re-sent the first
// message when we got the next send_message op), then we need to queue
// it without sending it right away
    const size_t send_message_count =
        (size_t)gpr_atm_acq_load(&retry_state->send_message_count);
    const bool have_pending_send_message_ops =
        send_message_count < calld->num_send_message_ops;
    if (have_pending_send_message_ops &&
        gpr_atm_full_cas(&retry_state->send_trailing_metadata,
                         (gpr_atm)send_message_count,
                         (gpr_atm)send_message_count + 1)) {
      grpc_multi_attempt_byte_stream_cache *cache =
          get_send_message_cache(calld, send_message_count);
      grpc_multi_attempt_byte_stream_init(&batch_data->send_message, cache);
      batch_data->batch.send_message = true;
      batch_data->batch.payload->send_message.send_message =
          (grpc_byte_stream *)&batch_data->send_message;
    }
    // send_trailing_metadata.
// FIXME: don't do this yet if there are pending send_message ops
    if (send_trailing_metadata_idx != INT_MAX &&
        gpr_atm_full_cas(&retry_state->send_trailing_metadata, (gpr_atm)0,
                         (gpr_atm)1)) {
      batch_data->batch.send_trailing_metadata = true;
      batch_data->batch.payload->send_trailing_metadata.send_trailing_metadata =
          calld->send_trailing_metadata;
    }
    // recv_initial_metadata.
    if (recv_initial_metadata_idx != INT_MAX &&
        gpr_atm_full_cas(&retry_state->recv_initial_metadata, (gpr_atm)0,
                         (gpr_atm)1)) {
      batch_data->batch.recv_initial_metadata = true;
      grpc_metadata_batch_init(&batch_data->recv_initial_metadata);
      batch_data->batch.payload->recv_initial_metadata.recv_initial_metadata =
          &batch_data->recv_initial_metadata;
      batch_data->batch.payload->recv_initial_metadata.recv_flags =
          calld->pending_batches[recv_initial_metadata_idx]->payload
              ->recv_initial_metadata.recv_flags;
      grpc_closure_init(&batch_data->recv_initial_metadata_ready,
                        recv_initial_metadata_ready, batch_data,
                        grpc_schedule_on_exec_ctx);
      batch_data->batch.payload->recv_initial_metadata
          .recv_initial_metadata_ready =
              &batch_data->recv_initial_metadata_ready;
      // Callback holds a ref.
      GRPC_SUBCHANNEL_CALL_REF(subchannel_call,
                               "client_channel_recv_initial_metadata_ready");
    }
    // recv_message.
    if (recv_message_idx != INT_MAX &&
        gpr_atm_full_cas(&retry_state->recv_message, (gpr_atm)0, (gpr_atm)1)) {
      batch_data->batch.recv_message = true;
      batch_data->batch.payload->recv_message.recv_message =
          &batch_data->recv_message;
      grpc_closure_init(&batch_data->recv_message_ready, recv_message_ready,
                        batch_data, grpc_schedule_on_exec_ctx);
      batch_data->batch.payload->recv_message.recv_message_ready =
          &batch_data->recv_message_ready;
      // Callback holds a ref.
      GRPC_SUBCHANNEL_CALL_REF(subchannel_call,
                               "client_channel_recv_message_ready");
    }
    // read_trailing_metadata.
    if (recv_trailing_metadata_idx != INT_MAX &&
        gpr_atm_full_cas(&retry_state->recv_trailing_metadata, (gpr_atm)0,
                         (gpr_atm)1)) {
      batch_data->batch.recv_trailing_metadata = true;
      grpc_metadata_batch_init(&batch_data->recv_trailing_metadata);
      batch_data->batch.payload->recv_trailing_metadata.recv_trailing_metadata =
          &batch_data->recv_trailing_metadata;
      GPR_ASSERT(
          calld->pending_batches[recv_trailing_metadata_idx]->collect_stats);
      batch_data->batch.collect_stats = true;
      batch_data->batch.payload->collect_stats.collect_stats =
          &batch_data->collect_stats;
    }
    // Intercept on_complete.
    grpc_closure_init(&batch_data->on_complete, on_complete, batch_data,
                      grpc_schedule_on_exec_ctx);
    batch_data->batch.on_complete = &batch_data->on_complete;
    batch = &batch_data->batch;
  }
  grpc_subchannel_call_process_op(exec_ctx, subchannel_call, batch);
}

// Sets calld->method_params and calld->retry_throttle_data.
// If the method params specify a timeout, populates
// *per_method_deadline and returns true.
static bool set_call_method_params_from_service_config_locked(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    gpr_timespec *per_method_deadline) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  if (chand->retry_throttle_data != NULL) {
    calld->retry_throttle_data =
        grpc_server_retry_throttle_data_ref(chand->retry_throttle_data);
  }
  if (chand->method_params_table != NULL) {
    calld->method_params = grpc_method_config_table_get(
        exec_ctx, chand->method_params_table, calld->path);
    if (calld->method_params != NULL) {
      method_parameters_ref(calld->method_params);
      if (gpr_time_cmp(calld->method_params->timeout,
                       gpr_time_0(GPR_TIMESPAN)) != 0) {
        *per_method_deadline =
            gpr_time_add(calld->call_start_time, calld->method_params->timeout);
        return true;
      }
    }
  }
  return false;
}

static void apply_final_configuration_locked(grpc_exec_ctx *exec_ctx,
                                             grpc_call_element *elem) {
  /* apply service-config level configuration to the call (now that we're
   * certain it exists) */
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  gpr_timespec per_method_deadline;
  if (set_call_method_params_from_service_config_locked(exec_ctx, elem,
                                                        &per_method_deadline)) {
    // If the deadline from the service config is shorter than the one
    // from the client API, reset the deadline timer.
    if (chand->deadline_checking_enabled &&
        gpr_time_cmp(per_method_deadline, calld->deadline) < 0) {
      calld->deadline = per_method_deadline;
      grpc_deadline_state_reset(exec_ctx, elem, calld->deadline);
    }
  }
}

static void subchannel_ready_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                    grpc_error *error) {
  grpc_call_element *elem = arg;
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(calld->creation_phase ==
             GRPC_CLIENT_CHANNEL_PICKING_SUBCHANNEL);
  grpc_polling_entity_del_from_pollset_set(exec_ctx, calld->pollent,
                                           chand->interested_parties);
  calld->creation_phase = GRPC_CLIENT_CHANNEL_NOT_PICKING;
  if (calld->connected_subchannel == NULL) {
    gpr_atm_no_barrier_store(&calld->subchannel_call, (gpr_atm)CANCELLED_CALL);
    fail_waiting_locked(exec_ctx, calld,
                        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                            "Failed to create subchannel", &error, 1));
  } else if (GET_CALL(calld) == CANCELLED_CALL) {
    /* already cancelled before subchannel became ready */
    grpc_error *cancellation_error =
        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
            "Cancelled before creating subchannel", &error, 1);
    /* if due to deadline, attach the deadline exceeded status to the error */
    if (gpr_time_cmp(calld->deadline, gpr_now(GPR_CLOCK_MONOTONIC)) < 0) {
      cancellation_error =
          grpc_error_set_int(cancellation_error, GRPC_ERROR_INT_GRPC_STATUS,
                             GRPC_STATUS_DEADLINE_EXCEEDED);
    }
    fail_waiting_locked(exec_ctx, calld, cancellation_error);
  } else {
    /* Create call on subchannel. */
    grpc_subchannel_call *subchannel_call = NULL;
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
        .parent_data_size = parent_data_size,
        };
    grpc_error *new_error = grpc_connected_subchannel_create_call(
        exec_ctx, calld->connected_subchannel, &call_args, &subchannel_call);
    gpr_atm_rel_store(&calld->subchannel_call,
                      (gpr_atm)(uintptr_t)subchannel_call);
    if (new_error != GRPC_ERROR_NONE) {
      new_error = grpc_error_add_child(new_error, error);
      fail_waiting_locked(exec_ctx, calld, new_error);
    } else {
      release_waiting_locked(exec_ctx, elem);
    }
  }
  GRPC_CALL_STACK_UNREF(exec_ctx, calld->owning_call, "pick_subchannel");
}

static char *cc_get_peer(grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  grpc_subchannel_call *subchannel_call = GET_CALL(calld);
  if (subchannel_call == NULL || subchannel_call == CANCELLED_CALL) {
    return NULL;
  } else {
    return grpc_subchannel_call_get_peer(exec_ctx, subchannel_call);
  }
}

typedef struct {
  grpc_metadata_batch *initial_metadata;
  uint32_t initial_metadata_flags;
  grpc_connected_subchannel **connected_subchannel;
  grpc_closure *on_ready;
  grpc_call_element *elem;
  grpc_closure closure;
} continue_picking_args;

/** Return true if subchannel is available immediately (in which case on_ready
    should not be called), or false otherwise (in which case on_ready should be
    called when the subchannel is available). */
static bool pick_subchannel_locked(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_metadata_batch *initial_metadata, uint32_t initial_metadata_flags,
    grpc_connected_subchannel **connected_subchannel, grpc_closure *on_ready,
    grpc_error *error);

static void continue_picking_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                    grpc_error *error) {
  continue_picking_args *cpa = arg;
  if (cpa->connected_subchannel == NULL) {
    /* cancelled, do nothing */
  } else if (error != GRPC_ERROR_NONE) {
    grpc_closure_sched(exec_ctx, cpa->on_ready, GRPC_ERROR_REF(error));
  } else {
    if (pick_subchannel_locked(exec_ctx, cpa->elem, cpa->initial_metadata,
                               cpa->initial_metadata_flags,
                               cpa->connected_subchannel, cpa->on_ready,
                               GRPC_ERROR_NONE)) {
      grpc_closure_sched(exec_ctx, cpa->on_ready, GRPC_ERROR_NONE);
    }
  }
  gpr_free(cpa);
}

static bool pick_subchannel_locked(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_metadata_batch *initial_metadata, uint32_t initial_metadata_flags,
    grpc_connected_subchannel **connected_subchannel, grpc_closure *on_ready,
    grpc_error *error) {
  GPR_TIMER_BEGIN("pick_subchannel", 0);

  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  continue_picking_args *cpa;
  grpc_closure *closure;

  GPR_ASSERT(connected_subchannel);

  if (error != GRPC_ERROR_NONE) {
    if (chand->lb_policy != NULL) {
      grpc_lb_policy_cancel_pick_locked(exec_ctx, chand->lb_policy,
                                        connected_subchannel,
                                        GRPC_ERROR_REF(error));
    }
    for (closure = chand->waiting_for_config_closures.head; closure != NULL;
         closure = closure->next_data.next) {
      cpa = closure->cb_arg;
      if (cpa->connected_subchannel == connected_subchannel) {
        cpa->connected_subchannel = NULL;
        grpc_closure_sched(exec_ctx, cpa->on_ready,
                           GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                               "Pick cancelled", &error, 1));
      }
    }
    GPR_TIMER_END("pick_subchannel", 0);
    GRPC_ERROR_UNREF(error);
    return true;
  }
  if (chand->lb_policy != NULL) {
    apply_final_configuration_locked(exec_ctx, elem);
    grpc_lb_policy *lb_policy = chand->lb_policy;
    GRPC_LB_POLICY_REF(lb_policy, "pick_subchannel");
    // If the application explicitly set wait_for_ready, use that.
    // Otherwise, if the service config specified a value for this
    // method, use that.
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
        initial_metadata, initial_metadata_flags, &calld->lb_token_mdelem,
        gpr_inf_future(GPR_CLOCK_MONOTONIC)};

    // Wrap the user-provided callback in order to hold a strong reference to
    // the LB policy for the duration of the pick.
    wrapped_on_pick_closure_arg *w_on_pick_arg =
        gpr_zalloc(sizeof(*w_on_pick_arg));
    grpc_closure_init(&w_on_pick_arg->wrapper_closure,
                      wrapped_on_pick_closure_cb, w_on_pick_arg,
                      grpc_schedule_on_exec_ctx);
    w_on_pick_arg->wrapped_closure = on_ready;
    GRPC_LB_POLICY_REF(lb_policy, "pick_subchannel_wrapping");
    w_on_pick_arg->lb_policy = lb_policy;
    const bool pick_done = grpc_lb_policy_pick_locked(
        exec_ctx, lb_policy, &inputs, connected_subchannel, NULL,
        &w_on_pick_arg->wrapper_closure);
    if (pick_done) {
      /* synchronous grpc_lb_policy_pick call. Unref the LB policy. */
      GRPC_LB_POLICY_UNREF(exec_ctx, w_on_pick_arg->lb_policy,
                           "pick_subchannel_wrapping");
      gpr_free(w_on_pick_arg);
    }
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "pick_subchannel");
    GPR_TIMER_END("pick_subchannel", 0);
    return pick_done;
  }
  if (chand->resolver != NULL && !chand->started_resolving) {
    chand->started_resolving = true;
    GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
    grpc_resolver_next_locked(exec_ctx, chand->resolver,
                              &chand->resolver_result,
                              &chand->on_resolver_result_changed);
  }
  if (chand->resolver != NULL) {
    cpa = gpr_malloc(sizeof(*cpa));
    cpa->initial_metadata = initial_metadata;
    cpa->initial_metadata_flags = initial_metadata_flags;
    cpa->connected_subchannel = connected_subchannel;
    cpa->on_ready = on_ready;
    cpa->elem = elem;
    grpc_closure_init(&cpa->closure, continue_picking_locked, cpa,
                      grpc_combiner_scheduler(chand->combiner, true));
    grpc_closure_list_append(&chand->waiting_for_config_closures, &cpa->closure,
                             GRPC_ERROR_NONE);
  } else {
    grpc_closure_sched(exec_ctx, on_ready,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Disconnected"));
  }

  GPR_TIMER_END("pick_subchannel", 0);
  return false;
}

static void start_transport_stream_op_batch_locked_inner(
    grpc_exec_ctx *exec_ctx, grpc_transport_stream_op_batch *op,
    grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  grpc_subchannel_call *call;
  /* need to recheck that another thread hasn't set the call */
  call = GET_CALL(calld);
  if (call == CANCELLED_CALL) {
    grpc_transport_stream_op_batch_finish_with_failure(
        exec_ctx, op, GRPC_ERROR_REF(calld->cancel_error));
    /* early out */
    return;
  }
  if (call != NULL) {
    start_subchannel_batch(exec_ctx, elem, call, op);
    /* early out */
    return;
  }
  /* if this is a cancellation, then we can raise our cancelled flag */
  if (op->cancel_stream) {
    if (!gpr_atm_rel_cas(&calld->subchannel_call, 0, (gpr_atm)CANCELLED_CALL)) {
      /* recurse to retry */
      start_transport_stream_op_batch_locked_inner(exec_ctx, op, elem);
      /* early out */
      return;
    } else {
// FIXME: if retrying, need to stop all retries
// can only happen in one of the following cases:
// - we allowed an error to propagate up, in which case we've given up
//   and no longer need to retry
// - a filter above us in the parent channel stack generated an error,
//   in which case it's fine to give up on retries (might want to audit
//   this)
// - the application canceled from the API, in which case we definitely
//   want to give up on retries
      /* Stash a copy of cancel_error in our call data, so that we can use
         it for subsequent operations.  This ensures that if the call is
         cancelled before any ops are passed down (e.g., if the deadline
         is in the past when the call starts), we can return the right
         error to the caller when the first op does get passed down. */
      calld->cancel_error =
          GRPC_ERROR_REF(op->payload->cancel_stream.cancel_error);
      switch (calld->creation_phase) {
        case GRPC_CLIENT_CHANNEL_NOT_PICKING:
          fail_waiting_locked(exec_ctx, calld,
                              GRPC_ERROR_REF(
                                  op->payload->cancel_stream.cancel_error));
          break;
        case GRPC_CLIENT_CHANNEL_PICKING_SUBCHANNEL:
          pick_subchannel_locked(
              exec_ctx, elem, NULL, 0, &calld->connected_subchannel, NULL,
              GRPC_ERROR_REF(op->payload->cancel_stream.cancel_error));
          break;
      }
      grpc_transport_stream_op_batch_finish_with_failure(
          exec_ctx, op,
          GRPC_ERROR_REF(op->payload->cancel_stream.cancel_error));
      /* early out */
      return;
    }
  }
  /* if we don't have a subchannel, try to get one */
  if (calld->creation_phase == GRPC_CLIENT_CHANNEL_NOT_PICKING &&
      calld->connected_subchannel == NULL && op->send_initial_metadata) {
    calld->creation_phase = GRPC_CLIENT_CHANNEL_PICKING_SUBCHANNEL;
    grpc_closure_init(&calld->next_step, subchannel_ready_locked, elem,
                      grpc_combiner_scheduler(chand->combiner, true));
    GRPC_CALL_STACK_REF(calld->owning_call, "pick_subchannel");
    /* If a subchannel is not available immediately, the polling entity from
       call_data should be provided to channel_data's interested_parties, so
       that IO of the lb_policy and resolver could be done under it. */
    if (pick_subchannel_locked(
            exec_ctx, elem,
            op->payload->send_initial_metadata.send_initial_metadata,
            op->payload->send_initial_metadata.send_initial_metadata_flags,
            &calld->connected_subchannel, &calld->next_step, GRPC_ERROR_NONE)) {
      calld->creation_phase = GRPC_CLIENT_CHANNEL_NOT_PICKING;
      GRPC_CALL_STACK_UNREF(exec_ctx, calld->owning_call, "pick_subchannel");
    } else {
      grpc_polling_entity_add_to_pollset_set(exec_ctx, calld->pollent,
                                             chand->interested_parties);
    }
  }
  /* if we've got a subchannel, then let's ask it to create a call */
  if (calld->creation_phase == GRPC_CLIENT_CHANNEL_NOT_PICKING &&
      calld->connected_subchannel != NULL) {
    grpc_subchannel_call *subchannel_call = NULL;
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
        .parent_data_size = parent_data_size,
        };
    grpc_error *error = grpc_connected_subchannel_create_call(
        exec_ctx, calld->connected_subchannel, &call_args, &subchannel_call);
    gpr_atm_rel_store(&calld->subchannel_call,
                      (gpr_atm)(uintptr_t)subchannel_call);
    if (error != GRPC_ERROR_NONE) {
      fail_waiting_locked(exec_ctx, calld, GRPC_ERROR_REF(error));
      grpc_transport_stream_op_batch_finish_with_failure(exec_ctx, op, error);
    } else {
      release_waiting_locked(exec_ctx, elem);
      /* recurse to retry */
      start_transport_stream_op_batch_locked_inner(exec_ctx, op, elem);
    }
    /* early out */
    return;
  }
  /* nothing to be done but wait */
  add_waiting_batch_locked(elem, op);
}

static void start_transport_stream_op_batch_locked(grpc_exec_ctx *exec_ctx,
                                                   void *arg,
                                                   grpc_error *error_ignored) {
  GPR_TIMER_BEGIN("start_transport_stream_op_batch_locked", 0);

  grpc_transport_stream_op_batch *op = arg;
  grpc_call_element *elem = op->handler_private.extra_arg;
  call_data *calld = elem->call_data;

  start_transport_stream_op_batch_locked_inner(exec_ctx, op, elem);

  GRPC_CALL_STACK_UNREF(exec_ctx, calld->owning_call,
                        "start_transport_stream_op_batch");
  GPR_TIMER_END("start_transport_stream_op_batch_locked", 0);
}

/* The logic here is fairly complicated, due to (a) the fact that we
   need to handle the case where we receive the send op before the
   initial metadata op, and (b) the need for efficiency, especially in
   the streaming case.

   We use double-checked locking to initially see if initialization has been
   performed. If it has not, we acquire the combiner and perform initialization.
   If it has, we proceed on the fast path. */
static void cc_start_transport_stream_op_batch(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_transport_stream_op_batch *op) {
gpr_log(GPR_INFO, "received batch:%s%s%s%s%s%s%s",
        op->send_initial_metadata ? " send_initial_metadata" : "",
        op->send_message ? " send_message" : "",
        op->send_trailing_metadata ? " send_trailing_metadata" : "",
        op->recv_initial_metadata ? " recv_initial_metadata" : "",
        op->recv_message ? " recv_message" : "",
        op->recv_trailing_metadata ? " recv_trailing_metadata" : "",
        op->cancel_stream ? " cancel_stream" : "");

  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  if (chand->deadline_checking_enabled) {
    grpc_deadline_state_client_start_transport_stream_op_batch(exec_ctx, elem,
                                                               op);
  }
  retry_checks_for_new_batch(exec_ctx, elem, op);
  /* try to (atomically) get the call */
  grpc_subchannel_call *call = GET_CALL(calld);
  GPR_TIMER_BEGIN("cc_start_transport_stream_op_batch", 0);
  if (call == CANCELLED_CALL) {
    grpc_transport_stream_op_batch_finish_with_failure(
        exec_ctx, op, GRPC_ERROR_REF(calld->cancel_error));
    GPR_TIMER_END("cc_start_transport_stream_op_batch", 0);
    /* early out */
    return;
  }
  if (call != NULL) {
    start_subchannel_batch(exec_ctx, elem, call, op);
    GPR_TIMER_END("cc_start_transport_stream_op_batch", 0);
    /* early out */
    return;
  }
  /* we failed; lock and figure out what to do */
  GRPC_CALL_STACK_REF(calld->owning_call, "start_transport_stream_op_batch");
  op->handler_private.extra_arg = elem;
  grpc_closure_sched(
      exec_ctx,
      grpc_closure_init(&op->handler_private.closure,
                        start_transport_stream_op_batch_locked, op,
                        grpc_combiner_scheduler(chand->combiner, false)),
      GRPC_ERROR_NONE);
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
  grpc_closure_list_init(&calld->waiting_list);
  if (chand->deadline_checking_enabled) {
    grpc_deadline_state_init(exec_ctx, elem, args->call_stack, calld->deadline);
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
  GRPC_ERROR_UNREF(calld->cancel_error);
  grpc_subchannel_call *call = GET_CALL(calld);
  if (call != NULL && call != CANCELLED_CALL) {
    grpc_subchannel_call_set_cleanup_closure(call, then_schedule_closure);
    then_schedule_closure = NULL;
    GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, call, "client_channel_destroy_call");
  }
  GPR_ASSERT(calld->creation_phase == GRPC_CLIENT_CHANNEL_NOT_PICKING);
  if (calld->connected_subchannel != NULL) {
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, calld->connected_subchannel,
                                    "picked");
  }
  grpc_closure_sched(exec_ctx, then_schedule_closure, GRPC_ERROR_NONE);
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
    cc_get_peer,
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
      GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
      chand->started_resolving = true;
      grpc_resolver_next_locked(exec_ctx, chand->resolver,
                                &chand->resolver_result,
                                &chand->on_resolver_result_changed);
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
    grpc_closure_sched(
        exec_ctx,
        grpc_closure_create(try_to_connect_locked, chand,
                            grpc_combiner_scheduler(chand->combiner, false)),
        GRPC_ERROR_NONE);
  }
  return out;
}

typedef struct {
  channel_data *chand;
  grpc_pollset *pollset;
  grpc_closure *on_complete;
  grpc_connectivity_state *state;
  grpc_closure my_closure;
} external_connectivity_watcher;

static void on_external_watch_complete(grpc_exec_ctx *exec_ctx, void *arg,
                                       grpc_error *error) {
  external_connectivity_watcher *w = arg;
  grpc_closure *follow_up = w->on_complete;
  grpc_pollset_set_del_pollset(exec_ctx, w->chand->interested_parties,
                               w->pollset);
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, w->chand->owning_stack,
                           "external_connectivity_watcher");
  gpr_free(w);
  grpc_closure_run(exec_ctx, follow_up, GRPC_ERROR_REF(error));
}

static void watch_connectivity_state_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                            grpc_error *error_ignored) {
  external_connectivity_watcher *w = arg;
  grpc_closure_init(&w->my_closure, on_external_watch_complete, w,
                    grpc_schedule_on_exec_ctx);
  grpc_connectivity_state_notify_on_state_change(
      exec_ctx, &w->chand->state_tracker, w->state, &w->my_closure);
}

void grpc_client_channel_watch_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem, grpc_pollset *pollset,
    grpc_connectivity_state *state, grpc_closure *on_complete) {
  channel_data *chand = elem->channel_data;
  external_connectivity_watcher *w = gpr_malloc(sizeof(*w));
  w->chand = chand;
  w->pollset = pollset;
  w->on_complete = on_complete;
  w->state = state;
  grpc_pollset_set_add_pollset(exec_ctx, chand->interested_parties, pollset);
  GRPC_CHANNEL_STACK_REF(w->chand->owning_stack,
                         "external_connectivity_watcher");
  grpc_closure_sched(
      exec_ctx,
      grpc_closure_init(&w->my_closure, watch_connectivity_state_locked, w,
                        grpc_combiner_scheduler(chand->combiner, true)),
      GRPC_ERROR_NONE);
}
