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
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/deadline_filter.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/service_config.h"
#include "src/core/lib/transport/static_metadata.h"

/* Client channel implementation */

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
  gpr_timespec timeout;
  wait_for_ready_value wait_for_ready;
} method_parameters;

static method_parameters *method_parameters_ref(
    method_parameters *method_params) {
  gpr_ref(&method_params->refs);
  return method_params;
}

static void method_parameters_unref(method_parameters *method_params) {
  if (gpr_unref(&method_params->refs)) {
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

static void *method_parameters_create_from_json(const grpc_json *json) {
  wait_for_ready_value wait_for_ready = WAIT_FOR_READY_UNSET;
  gpr_timespec timeout = {0, 0, GPR_TIMESPAN};
  for (grpc_json *field = json->child; field != NULL; field = field->next) {
    if (field->key == NULL) continue;
    if (strcmp(field->key, "waitForReady") == 0) {
      if (wait_for_ready != WAIT_FOR_READY_UNSET) return NULL;  // Duplicate.
      if (!parse_wait_for_ready(field, &wait_for_ready)) return NULL;
    } else if (strcmp(field->key, "timeout") == 0) {
      if (timeout.tv_sec > 0 || timeout.tv_nsec > 0) return NULL;  // Duplicate.
      if (!parse_timeout(field, &timeout)) return NULL;
    }
  }
  method_parameters *value = gpr_malloc(sizeof(method_parameters));
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
  grpc_resolver *resolver;
  /** have we started resolving this channel */
  bool started_resolving;
  /** client channel factory */
  grpc_client_channel_factory *client_channel_factory;

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
  if ((state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
       state == GRPC_CHANNEL_SHUTDOWN) &&
      chand->lb_policy != NULL) {
    /* cancel picks with wait_for_ready=false */
    grpc_lb_policy_cancel_picks_locked(
        exec_ctx, chand->lb_policy,
        /* mask= */ GRPC_INITIAL_METADATA_WAIT_FOR_READY,
        /* check= */ 0, GRPC_ERROR_REF(error));
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
  gpr_mu_init(&chand->external_connectivity_watcher_list_mu);

  gpr_mu_lock(&chand->external_connectivity_watcher_list_mu);
  chand->external_connectivity_watcher_list_head = NULL;
  gpr_mu_unlock(&chand->external_connectivity_watcher_list_mu);

  chand->owning_stack = args->channel_stack;
  grpc_closure_init(&chand->on_resolver_result_changed,
                    on_resolver_result_changed_locked, chand,
                    grpc_combiner_scheduler(chand->combiner, false));
  chand->interested_parties = grpc_pollset_set_create();
  grpc_connectivity_state_init(&chand->state_tracker, GRPC_CHANNEL_IDLE,
                               "client_channel");
  // Record client channel factory.
  const grpc_arg *arg = grpc_channel_args_find(args->channel_args,
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
  gpr_mu_destroy(&chand->external_connectivity_watcher_list_mu);
}

/*************************************************************************
 * PER-CALL FUNCTIONS
 */

#define GET_CALL(call_data) \
  ((grpc_subchannel_call *)(gpr_atm_acq_load(&(call_data)->subchannel_call)))

#define CANCELLED_CALL ((grpc_subchannel_call *)1)

typedef enum {
  /* zero so that it can be default-initialized */
  GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING = 0,
  GRPC_SUBCHANNEL_CALL_HOLDER_PICKING_SUBCHANNEL
} subchannel_creation_phase;

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

  grpc_transport_stream_op_batch **waiting_ops;
  size_t waiting_ops_count;
  size_t waiting_ops_capacity;

  grpc_closure next_step;

  grpc_call_stack *owning_call;

  grpc_linked_mdelem lb_token_mdelem;

  grpc_closure on_complete;
  grpc_closure *original_on_complete;
} call_data;

grpc_subchannel_call *grpc_client_channel_get_subchannel_call(
    grpc_call_element *call_elem) {
  grpc_subchannel_call *scc = GET_CALL((call_data *)call_elem->call_data);
  return scc == CANCELLED_CALL ? NULL : scc;
}

static void add_waiting_locked(call_data *calld,
                               grpc_transport_stream_op_batch *op) {
  GPR_TIMER_BEGIN("add_waiting_locked", 0);
  if (calld->waiting_ops_count == calld->waiting_ops_capacity) {
    calld->waiting_ops_capacity = GPR_MAX(3, 2 * calld->waiting_ops_capacity);
    calld->waiting_ops =
        gpr_realloc(calld->waiting_ops,
                    calld->waiting_ops_capacity * sizeof(*calld->waiting_ops));
  }
  calld->waiting_ops[calld->waiting_ops_count++] = op;
  GPR_TIMER_END("add_waiting_locked", 0);
}

static void fail_locked(grpc_exec_ctx *exec_ctx, call_data *calld,
                        grpc_error *error) {
  size_t i;
  for (i = 0; i < calld->waiting_ops_count; i++) {
    grpc_transport_stream_op_batch_finish_with_failure(
        exec_ctx, calld->waiting_ops[i], GRPC_ERROR_REF(error));
  }
  calld->waiting_ops_count = 0;
  GRPC_ERROR_UNREF(error);
}

static void retry_waiting_locked(grpc_exec_ctx *exec_ctx, call_data *calld) {
  if (calld->waiting_ops_count == 0) {
    return;
  }

  grpc_subchannel_call *call = GET_CALL(calld);
  grpc_transport_stream_op_batch **ops = calld->waiting_ops;
  size_t nops = calld->waiting_ops_count;
  if (call == CANCELLED_CALL) {
    fail_locked(exec_ctx, calld, GRPC_ERROR_CANCELLED);
    return;
  }
  calld->waiting_ops = NULL;
  calld->waiting_ops_count = 0;
  calld->waiting_ops_capacity = 0;
  for (size_t i = 0; i < nops; i++) {
    grpc_subchannel_call_process_op(exec_ctx, call, ops[i]);
  }
  gpr_free(ops);
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
  gpr_timespec per_method_deadline;
  if (set_call_method_params_from_service_config_locked(exec_ctx, elem,
                                                        &per_method_deadline)) {
    // If the deadline from the service config is shorter than the one
    // from the client API, reset the deadline timer.
    if (gpr_time_cmp(per_method_deadline, calld->deadline) < 0) {
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
             GRPC_SUBCHANNEL_CALL_HOLDER_PICKING_SUBCHANNEL);
  grpc_polling_entity_del_from_pollset_set(exec_ctx, calld->pollent,
                                           chand->interested_parties);
  calld->creation_phase = GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING;
  if (calld->connected_subchannel == NULL) {
    gpr_atm_no_barrier_store(&calld->subchannel_call, 1);
    fail_locked(exec_ctx, calld,
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
    fail_locked(exec_ctx, calld, cancellation_error);
  } else {
    /* Create call on subchannel. */
    grpc_subchannel_call *subchannel_call = NULL;
    const grpc_connected_subchannel_call_args call_args = {
        .pollent = calld->pollent,
        .path = calld->path,
        .start_time = calld->call_start_time,
        .deadline = calld->deadline,
        .arena = calld->arena};
    grpc_error *new_error = grpc_connected_subchannel_create_call(
        exec_ctx, calld->connected_subchannel, &call_args, &subchannel_call);
    gpr_atm_rel_store(&calld->subchannel_call,
                      (gpr_atm)(uintptr_t)subchannel_call);
    if (new_error != GRPC_ERROR_NONE) {
      new_error = grpc_error_add_child(new_error, error);
      fail_locked(exec_ctx, calld, new_error);
    } else {
      retry_waiting_locked(exec_ctx, calld);
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

  if (initial_metadata == NULL) {
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
  GPR_ASSERT(error == GRPC_ERROR_NONE);
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
    const bool result = grpc_lb_policy_pick_locked(
        exec_ctx, lb_policy, &inputs, connected_subchannel, NULL, on_ready);
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "pick_subchannel");
    GPR_TIMER_END("pick_subchannel", 0);
    return result;
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
    grpc_subchannel_call_process_op(exec_ctx, call, op);
    /* early out */
    return;
  }
  /* if this is a cancellation, then we can raise our cancelled flag */
  if (op->cancel_stream) {
    if (!gpr_atm_rel_cas(&calld->subchannel_call, 0,
                         (gpr_atm)(uintptr_t)CANCELLED_CALL)) {
      /* recurse to retry */
      start_transport_stream_op_batch_locked_inner(exec_ctx, op, elem);
      /* early out */
      return;
    } else {
      /* Stash a copy of cancel_error in our call data, so that we can use
         it for subsequent operations.  This ensures that if the call is
         cancelled before any ops are passed down (e.g., if the deadline
         is in the past when the call starts), we can return the right
         error to the caller when the first op does get passed down. */
      calld->cancel_error =
          GRPC_ERROR_REF(op->payload->cancel_stream.cancel_error);
      switch (calld->creation_phase) {
        case GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING:
          fail_locked(exec_ctx, calld,
                      GRPC_ERROR_REF(op->payload->cancel_stream.cancel_error));
          break;
        case GRPC_SUBCHANNEL_CALL_HOLDER_PICKING_SUBCHANNEL:
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
  if (calld->creation_phase == GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING &&
      calld->connected_subchannel == NULL && op->send_initial_metadata) {
    calld->creation_phase = GRPC_SUBCHANNEL_CALL_HOLDER_PICKING_SUBCHANNEL;
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
      calld->creation_phase = GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING;
      GRPC_CALL_STACK_UNREF(exec_ctx, calld->owning_call, "pick_subchannel");
    } else {
      grpc_polling_entity_add_to_pollset_set(exec_ctx, calld->pollent,
                                             chand->interested_parties);
    }
  }
  /* if we've got a subchannel, then let's ask it to create a call */
  if (calld->creation_phase == GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING &&
      calld->connected_subchannel != NULL) {
    grpc_subchannel_call *subchannel_call = NULL;
    const grpc_connected_subchannel_call_args call_args = {
        .pollent = calld->pollent,
        .path = calld->path,
        .start_time = calld->call_start_time,
        .deadline = calld->deadline,
        .arena = calld->arena};
    grpc_error *error = grpc_connected_subchannel_create_call(
        exec_ctx, calld->connected_subchannel, &call_args, &subchannel_call);
    gpr_atm_rel_store(&calld->subchannel_call,
                      (gpr_atm)(uintptr_t)subchannel_call);
    if (error != GRPC_ERROR_NONE) {
      fail_locked(exec_ctx, calld, GRPC_ERROR_REF(error));
      grpc_transport_stream_op_batch_finish_with_failure(exec_ctx, op, error);
    } else {
      retry_waiting_locked(exec_ctx, calld);
      /* recurse to retry */
      start_transport_stream_op_batch_locked_inner(exec_ctx, op, elem);
    }
    /* early out */
    return;
  }
  /* nothing to be done but wait */
  add_waiting_locked(calld, op);
}

static void on_complete(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  grpc_call_element *elem = arg;
  call_data *calld = elem->call_data;
  if (calld->retry_throttle_data != NULL) {
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
  grpc_closure_run(exec_ctx, calld->original_on_complete,
                   GRPC_ERROR_REF(error));
}

static void start_transport_stream_op_batch_locked(grpc_exec_ctx *exec_ctx,
                                                   void *arg,
                                                   grpc_error *error_ignored) {
  GPR_TIMER_BEGIN("start_transport_stream_op_batch_locked", 0);

  grpc_transport_stream_op_batch *op = arg;
  grpc_call_element *elem = op->handler_private.extra_arg;
  call_data *calld = elem->call_data;

  if (op->recv_trailing_metadata) {
    GPR_ASSERT(op->on_complete != NULL);
    calld->original_on_complete = op->on_complete;
    grpc_closure_init(&calld->on_complete, on_complete, elem,
                      grpc_schedule_on_exec_ctx);
    op->on_complete = &calld->on_complete;
  }

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
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  grpc_deadline_state_client_start_transport_stream_op_batch(exec_ctx, elem,
                                                             op);
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
    grpc_subchannel_call_process_op(exec_ctx, call, op);
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
  // Initialize data members.
  grpc_deadline_state_init(exec_ctx, elem, args->call_stack);
  calld->path = grpc_slice_ref_internal(args->path);
  calld->call_start_time = args->start_time;
  calld->deadline = gpr_convert_clock_type(args->deadline, GPR_CLOCK_MONOTONIC);
  calld->owning_call = args->call_stack;
  calld->arena = args->arena;
  grpc_deadline_state_start(exec_ctx, elem, calld->deadline);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void cc_destroy_call_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_call_element *elem,
                                 const grpc_call_final_info *final_info,
                                 grpc_closure *then_schedule_closure) {
  call_data *calld = elem->call_data;
  grpc_deadline_state_destroy(exec_ctx, elem);
  grpc_slice_unref_internal(exec_ctx, calld->path);
  if (calld->method_params != NULL) {
    method_parameters_unref(calld->method_params);
  }
  GRPC_ERROR_UNREF(calld->cancel_error);
  grpc_subchannel_call *call = GET_CALL(calld);
  if (call != NULL && call != CANCELLED_CALL) {
    grpc_subchannel_call_set_cleanup_closure(call, then_schedule_closure);
    then_schedule_closure = NULL;
    GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, call, "client_channel_destroy_call");
  }
  GPR_ASSERT(calld->creation_phase == GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING);
  GPR_ASSERT(calld->waiting_ops_count == 0);
  if (calld->connected_subchannel != NULL) {
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, calld->connected_subchannel,
                                    "picked");
  }
  gpr_free(calld->waiting_ops);
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

typedef struct external_connectivity_watcher {
  channel_data *chand;
  grpc_pollset *pollset;
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
  grpc_pollset_set_del_pollset(exec_ctx, w->chand->interested_parties,
                               w->pollset);
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, w->chand->owning_stack,
                           "external_connectivity_watcher");
  external_connectivity_watcher_list_remove(w->chand, w);
  gpr_free(w);
  grpc_closure_run(exec_ctx, follow_up, GRPC_ERROR_REF(error));
}

static void watch_connectivity_state_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                            grpc_error *error_ignored) {
  external_connectivity_watcher *w = arg;
  external_connectivity_watcher *found = NULL;
  if (w->state != NULL) {
    external_connectivity_watcher_list_append(w->chand, w);
    grpc_closure_run(exec_ctx, w->watcher_timer_init, GRPC_ERROR_NONE);
    grpc_closure_init(&w->my_closure, on_external_watch_complete, w,
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
    grpc_pollset_set_del_pollset(exec_ctx, w->chand->interested_parties,
                                 w->pollset);
    GRPC_CHANNEL_STACK_UNREF(exec_ctx, w->chand->owning_stack,
                             "external_connectivity_watcher");
    gpr_free(w);
  }
}

void grpc_client_channel_watch_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem, grpc_pollset *pollset,
    grpc_connectivity_state *state, grpc_closure *on_complete,
    grpc_closure *watcher_timer_init) {
  channel_data *chand = elem->channel_data;
  external_connectivity_watcher *w = gpr_zalloc(sizeof(*w));
  w->chand = chand;
  w->pollset = pollset;
  w->on_complete = on_complete;
  w->state = state;
  w->watcher_timer_init = watcher_timer_init;

  grpc_pollset_set_add_pollset(exec_ctx, chand->interested_parties, pollset);
  GRPC_CHANNEL_STACK_REF(w->chand->owning_stack,
                         "external_connectivity_watcher");
  grpc_closure_sched(
      exec_ctx,
      grpc_closure_init(&w->my_closure, watch_connectivity_state_locked, w,
                        grpc_combiner_scheduler(chand->combiner, true)),
      GRPC_ERROR_NONE);
}
