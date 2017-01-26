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

#include "src/core/ext/client_channel/client_channel.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

#include "src/core/ext/client_channel/http_connect_handshaker.h"
#include "src/core/ext/client_channel/http_proxy.h"
#include "src/core/ext/client_channel/lb_policy_registry.h"
#include "src/core/ext/client_channel/resolver_registry.h"
#include "src/core/ext/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/deadline_filter.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/profiling/timers.h"
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
  WAIT_FOR_READY_UNSET,
  WAIT_FOR_READY_FALSE,
  WAIT_FOR_READY_TRUE
} wait_for_ready_value;

typedef struct method_parameters {
  gpr_timespec timeout;
  wait_for_ready_value wait_for_ready;
} method_parameters;

static void *method_parameters_copy(void *value) {
  void *new_value = gpr_malloc(sizeof(method_parameters));
  memcpy(new_value, value, sizeof(method_parameters));
  return new_value;
}

static void method_parameters_free(grpc_exec_ctx *exec_ctx, void *p) {
  gpr_free(p);
}

static const grpc_mdstr_hash_table_vtable method_parameters_vtable = {
    method_parameters_free, method_parameters_copy};

static void *method_parameters_create_from_json(const grpc_json *json) {
  wait_for_ready_value wait_for_ready = WAIT_FOR_READY_UNSET;
  gpr_timespec timeout = {0, 0, GPR_TIMESPAN};
  for (grpc_json *field = json->child; field != NULL; field = field->next) {
    if (field->key == NULL) continue;
    if (strcmp(field->key, "waitForReady") == 0) {
      if (wait_for_ready != WAIT_FOR_READY_UNSET) return NULL;  // Duplicate.
      if (field->type != GRPC_JSON_TRUE && field->type != GRPC_JSON_FALSE) {
        return NULL;
      }
      wait_for_ready = field->type == GRPC_JSON_TRUE ? WAIT_FOR_READY_TRUE
                                                     : WAIT_FOR_READY_FALSE;
    } else if (strcmp(field->key, "timeout") == 0) {
      if (timeout.tv_sec > 0 || timeout.tv_nsec > 0) return NULL;  // Duplicate.
      if (field->type != GRPC_JSON_STRING) return NULL;
      size_t len = strlen(field->value);
      if (field->value[len - 1] != 's') return NULL;
      char *buf = gpr_strdup(field->value);
      buf[len - 1] = '\0';  // Remove trailing 's'.
      char *decimal_point = strchr(buf, '.');
      if (decimal_point != NULL) {
        *decimal_point = '\0';
        timeout.tv_nsec = gpr_parse_nonnegative_int(decimal_point + 1);
        if (timeout.tv_nsec == -1) {
          gpr_free(buf);
          return NULL;
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
            return NULL;
        }
        timeout.tv_nsec *= multiplier;
      }
      timeout.tv_sec = gpr_parse_nonnegative_int(buf);
      if (timeout.tv_sec == -1) return NULL;
      gpr_free(buf);
    }
  }
  method_parameters *value = gpr_malloc(sizeof(method_parameters));
  value->timeout = timeout;
  value->wait_for_ready = wait_for_ready;
  return value;
}

/*************************************************************************
 * CHANNEL-WIDE FUNCTIONS
 */

typedef struct client_channel_channel_data {
  /** server name */
  char *server_name;
  /** HTTP CONNECT proxy to use, if any */
  char *proxy_name;
  /** resolver for this channel */
  grpc_resolver *resolver;
  /** have we started resolving this channel */
  bool started_resolving;
  /** client channel factory */
  grpc_client_channel_factory *client_channel_factory;

  /** mutex protecting all variables below in this data structure */
  gpr_mu mu;
  /** currently active load balancer */
  char *lb_policy_name;
  grpc_lb_policy *lb_policy;
  /** service config in JSON form */
  char *service_config_json;
  /** maps method names to method_parameters structs */
  grpc_mdstr_hash_table *method_params_table;
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

static void watch_lb_policy(grpc_exec_ctx *exec_ctx, channel_data *chand,
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
    grpc_lb_policy_cancel_picks(
        exec_ctx, chand->lb_policy,
        /* mask= */ GRPC_INITIAL_METADATA_WAIT_FOR_READY,
        /* check= */ 0, GRPC_ERROR_REF(error));
  }
  grpc_connectivity_state_set(exec_ctx, &chand->state_tracker, state, error,
                              reason);
}

static void on_lb_policy_state_changed_locked(grpc_exec_ctx *exec_ctx,
                                              lb_policy_connectivity_watcher *w,
                                              grpc_error *error) {
  grpc_connectivity_state publish_state = w->state;
  /* check if the notification is for a stale policy */
  if (w->lb_policy != w->chand->lb_policy) return;

  if (publish_state == GRPC_CHANNEL_SHUTDOWN && w->chand->resolver != NULL) {
    publish_state = GRPC_CHANNEL_TRANSIENT_FAILURE;
    grpc_resolver_channel_saw_error(exec_ctx, w->chand->resolver);
    GRPC_LB_POLICY_UNREF(exec_ctx, w->chand->lb_policy, "channel");
    w->chand->lb_policy = NULL;
  }
  set_channel_connectivity_state_locked(exec_ctx, w->chand, publish_state,
                                        GRPC_ERROR_REF(error), "lb_changed");
  if (w->state != GRPC_CHANNEL_SHUTDOWN) {
    watch_lb_policy(exec_ctx, w->chand, w->lb_policy, w->state);
  }
}

static void on_lb_policy_state_changed(grpc_exec_ctx *exec_ctx, void *arg,
                                       grpc_error *error) {
  lb_policy_connectivity_watcher *w = arg;

  gpr_mu_lock(&w->chand->mu);
  on_lb_policy_state_changed_locked(exec_ctx, w, error);
  gpr_mu_unlock(&w->chand->mu);

  GRPC_CHANNEL_STACK_UNREF(exec_ctx, w->chand->owning_stack, "watch_lb_policy");
  gpr_free(w);
}

static void watch_lb_policy(grpc_exec_ctx *exec_ctx, channel_data *chand,
                            grpc_lb_policy *lb_policy,
                            grpc_connectivity_state current_state) {
  lb_policy_connectivity_watcher *w = gpr_malloc(sizeof(*w));
  GRPC_CHANNEL_STACK_REF(chand->owning_stack, "watch_lb_policy");

  w->chand = chand;
  grpc_closure_init(&w->on_changed, on_lb_policy_state_changed, w,
                    grpc_schedule_on_exec_ctx);
  w->state = current_state;
  w->lb_policy = lb_policy;
  grpc_lb_policy_notify_on_state_change(exec_ctx, lb_policy, &w->state,
                                        &w->on_changed);
}

static void on_resolver_result_changed(grpc_exec_ctx *exec_ctx, void *arg,
                                       grpc_error *error) {
  channel_data *chand = arg;
  char *lb_policy_name = NULL;
  grpc_lb_policy *lb_policy = NULL;
  grpc_lb_policy *old_lb_policy;
  grpc_mdstr_hash_table *method_params_table = NULL;
  grpc_connectivity_state state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  bool exit_idle = false;
  grpc_error *state_error = GRPC_ERROR_CREATE("No load balancing policy");
  char *service_config_json = NULL;

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
    if (channel_arg != NULL) {
      GPR_ASSERT(channel_arg->type == GRPC_ARG_POINTER);
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
    // If using a proxy, add channel arg for server in HTTP CONNECT request.
    if (chand->proxy_name != NULL) {
      grpc_arg new_arg;
      new_arg.key = GRPC_ARG_HTTP_CONNECT_SERVER;
      new_arg.type = GRPC_ARG_STRING;
      new_arg.value.string = chand->server_name;
      grpc_channel_args *tmp_args = chand->resolver_result;
      chand->resolver_result =
          grpc_channel_args_copy_and_add(chand->resolver_result, &new_arg, 1);
      grpc_channel_args_destroy(exec_ctx, tmp_args);
    }
    // Instantiate LB policy.
    grpc_lb_policy_args lb_policy_args;
    lb_policy_args.args = chand->resolver_result;
    lb_policy_args.client_channel_factory = chand->client_channel_factory;
    lb_policy =
        grpc_lb_policy_create(exec_ctx, lb_policy_name, &lb_policy_args);
    if (lb_policy != NULL) {
      GRPC_LB_POLICY_REF(lb_policy, "config_change");
      GRPC_ERROR_UNREF(state_error);
      state =
          grpc_lb_policy_check_connectivity(exec_ctx, lb_policy, &state_error);
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

  gpr_mu_lock(&chand->mu);
  if (lb_policy_name != NULL) {
    gpr_free(chand->lb_policy_name);
    chand->lb_policy_name = lb_policy_name;
  }
  old_lb_policy = chand->lb_policy;
  chand->lb_policy = lb_policy;
  if (service_config_json != NULL) {
    gpr_free(chand->service_config_json);
    chand->service_config_json = service_config_json;
  }
  if (chand->method_params_table != NULL) {
    grpc_mdstr_hash_table_unref(exec_ctx, chand->method_params_table);
  }
  chand->method_params_table = method_params_table;
  if (lb_policy != NULL) {
    grpc_closure_list_sched(exec_ctx, &chand->waiting_for_config_closures);
  } else if (chand->resolver == NULL /* disconnected */) {
    grpc_closure_list_fail_all(
        &chand->waiting_for_config_closures,
        GRPC_ERROR_CREATE_REFERENCING("Channel disconnected", &error, 1));
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
      watch_lb_policy(exec_ctx, chand, lb_policy, state);
    }
    GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
    grpc_resolver_next(exec_ctx, chand->resolver, &chand->resolver_result,
                       &chand->on_resolver_result_changed);
    gpr_mu_unlock(&chand->mu);
  } else {
    if (chand->resolver != NULL) {
      grpc_resolver_shutdown(exec_ctx, chand->resolver);
      GRPC_RESOLVER_UNREF(exec_ctx, chand->resolver, "channel");
      chand->resolver = NULL;
    }
    grpc_error *refs[] = {error, state_error};
    set_channel_connectivity_state_locked(
        exec_ctx, chand, GRPC_CHANNEL_SHUTDOWN,
        GRPC_ERROR_CREATE_REFERENCING("Got config after disconnection", refs,
                                      GPR_ARRAY_SIZE(refs)),
        "resolver_gone");
    gpr_mu_unlock(&chand->mu);
  }

  if (exit_idle) {
    grpc_lb_policy_exit_idle(exec_ctx, lb_policy);
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

static void cc_start_transport_op(grpc_exec_ctx *exec_ctx,
                                  grpc_channel_element *elem,
                                  grpc_transport_op *op) {
  channel_data *chand = elem->channel_data;

  grpc_closure_sched(exec_ctx, op->on_consumed, GRPC_ERROR_NONE);

  GPR_ASSERT(op->set_accept_stream == false);
  if (op->bind_pollset != NULL) {
    grpc_pollset_set_add_pollset(exec_ctx, chand->interested_parties,
                                 op->bind_pollset);
  }

  gpr_mu_lock(&chand->mu);
  if (op->on_connectivity_state_change != NULL) {
    grpc_connectivity_state_notify_on_state_change(
        exec_ctx, &chand->state_tracker, op->connectivity_state,
        op->on_connectivity_state_change);
    op->on_connectivity_state_change = NULL;
    op->connectivity_state = NULL;
  }

  if (op->send_ping != NULL) {
    if (chand->lb_policy == NULL) {
      grpc_closure_sched(exec_ctx, op->send_ping,
                         GRPC_ERROR_CREATE("Ping with no load balancing"));
    } else {
      grpc_lb_policy_ping_one(exec_ctx, chand->lb_policy, op->send_ping);
      op->bind_pollset = NULL;
    }
    op->send_ping = NULL;
  }

  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    if (chand->resolver != NULL) {
      set_channel_connectivity_state_locked(
          exec_ctx, chand, GRPC_CHANNEL_SHUTDOWN,
          GRPC_ERROR_REF(op->disconnect_with_error), "disconnect");
      grpc_resolver_shutdown(exec_ctx, chand->resolver);
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
  gpr_mu_unlock(&chand->mu);
}

static void cc_get_channel_info(grpc_exec_ctx *exec_ctx,
                                grpc_channel_element *elem,
                                const grpc_channel_info *info) {
  channel_data *chand = elem->channel_data;
  gpr_mu_lock(&chand->mu);
  if (info->lb_policy_name != NULL) {
    *info->lb_policy_name = chand->lb_policy_name == NULL
                                ? NULL
                                : gpr_strdup(chand->lb_policy_name);
  }
  if (info->service_config_json != NULL) {
    *info->service_config_json = chand->service_config_json == NULL
                                     ? NULL
                                     : gpr_strdup(chand->service_config_json);
  }
  gpr_mu_unlock(&chand->mu);
}

/* Constructor for channel_data */
static grpc_error *cc_init_channel_elem(grpc_exec_ctx *exec_ctx,
                                        grpc_channel_element *elem,
                                        grpc_channel_element_args *args) {
  channel_data *chand = elem->channel_data;
  memset(chand, 0, sizeof(*chand));
  GPR_ASSERT(args->is_last);
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);
  // Initialize data members.
  gpr_mu_init(&chand->mu);
  chand->owning_stack = args->channel_stack;
  grpc_closure_init(&chand->on_resolver_result_changed,
                    on_resolver_result_changed, chand,
                    grpc_schedule_on_exec_ctx);
  chand->interested_parties = grpc_pollset_set_create();
  grpc_connectivity_state_init(&chand->state_tracker, GRPC_CHANNEL_IDLE,
                               "client_channel");
  // Record client channel factory.
  const grpc_arg *arg = grpc_channel_args_find(args->channel_args,
                                               GRPC_ARG_CLIENT_CHANNEL_FACTORY);
  GPR_ASSERT(arg != NULL);
  GPR_ASSERT(arg->type == GRPC_ARG_POINTER);
  grpc_client_channel_factory_ref(arg->value.pointer.p);
  chand->client_channel_factory = arg->value.pointer.p;
  // Instantiate resolver.
  arg = grpc_channel_args_find(args->channel_args, GRPC_ARG_SERVER_URI);
  GPR_ASSERT(arg != NULL);
  GPR_ASSERT(arg->type == GRPC_ARG_STRING);
  chand->server_name = gpr_strdup(arg->value.string);
  chand->proxy_name = grpc_get_http_proxy_server();
  char *name_to_resolve =
      chand->proxy_name == NULL ? chand->server_name : chand->proxy_name;
  chand->resolver = grpc_resolver_create(
      exec_ctx, name_to_resolve, args->channel_args, chand->interested_parties);
  if (chand->resolver == NULL) {
    return GRPC_ERROR_CREATE("resolver creation failed");
  }
  return GRPC_ERROR_NONE;
}

/* Destructor for channel_data */
static void cc_destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                    grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  gpr_free(chand->server_name);
  gpr_free(chand->proxy_name);
  if (chand->resolver != NULL) {
    grpc_resolver_shutdown(exec_ctx, chand->resolver);
    GRPC_RESOLVER_UNREF(exec_ctx, chand->resolver, "channel");
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
  gpr_free(chand->lb_policy_name);
  gpr_free(chand->service_config_json);
  if (chand->method_params_table != NULL) {
    grpc_mdstr_hash_table_unref(exec_ctx, chand->method_params_table);
  }
  grpc_connectivity_state_destroy(exec_ctx, &chand->state_tracker);
  grpc_pollset_set_destroy(chand->interested_parties);
  gpr_mu_destroy(&chand->mu);
}

/*************************************************************************
 * PER-CALL FUNCTIONS
 */

#define GET_CALL(call_data) \
  ((grpc_subchannel_call *)(gpr_atm_acq_load(&(call_data)->subchannel_call)))

#define CANCELLED_CALL ((grpc_subchannel_call *)1)

typedef enum {
  GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING,
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

  grpc_mdstr *path;  // Request path.
  gpr_timespec call_start_time;
  gpr_timespec deadline;
  wait_for_ready_value wait_for_ready_from_service_config;
  grpc_closure read_service_config;

  grpc_error *cancel_error;

  /** either 0 for no call, 1 for cancelled, or a pointer to a
      grpc_subchannel_call */
  gpr_atm subchannel_call;

  gpr_mu mu;

  subchannel_creation_phase creation_phase;
  grpc_connected_subchannel *connected_subchannel;
  grpc_polling_entity *pollent;

  grpc_transport_stream_op **waiting_ops;
  size_t waiting_ops_count;
  size_t waiting_ops_capacity;

  grpc_closure next_step;

  grpc_call_stack *owning_call;

  grpc_linked_mdelem lb_token_mdelem;
} call_data;

static void add_waiting_locked(call_data *calld, grpc_transport_stream_op *op) {
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
    grpc_transport_stream_op_finish_with_failure(
        exec_ctx, calld->waiting_ops[i], GRPC_ERROR_REF(error));
  }
  calld->waiting_ops_count = 0;
  GRPC_ERROR_UNREF(error);
}

typedef struct {
  grpc_transport_stream_op **ops;
  size_t nops;
  grpc_subchannel_call *call;
} retry_ops_args;

static void retry_ops(grpc_exec_ctx *exec_ctx, void *args, grpc_error *error) {
  retry_ops_args *a = args;
  size_t i;
  for (i = 0; i < a->nops; i++) {
    grpc_subchannel_call_process_op(exec_ctx, a->call, a->ops[i]);
  }
  GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, a->call, "retry_ops");
  gpr_free(a->ops);
  gpr_free(a);
}

static void retry_waiting_locked(grpc_exec_ctx *exec_ctx, call_data *calld) {
  if (calld->waiting_ops_count == 0) {
    return;
  }

  retry_ops_args *a = gpr_malloc(sizeof(*a));
  a->ops = calld->waiting_ops;
  a->nops = calld->waiting_ops_count;
  a->call = GET_CALL(calld);
  if (a->call == CANCELLED_CALL) {
    gpr_free(a);
    fail_locked(exec_ctx, calld, GRPC_ERROR_CANCELLED);
    return;
  }
  calld->waiting_ops = NULL;
  calld->waiting_ops_count = 0;
  calld->waiting_ops_capacity = 0;
  GRPC_SUBCHANNEL_CALL_REF(a->call, "retry_ops");
  grpc_closure_sched(
      exec_ctx, grpc_closure_create(retry_ops, a, grpc_schedule_on_exec_ctx),
      GRPC_ERROR_NONE);
}

static void subchannel_ready(grpc_exec_ctx *exec_ctx, void *arg,
                             grpc_error *error) {
  grpc_call_element *elem = arg;
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  gpr_mu_lock(&calld->mu);
  GPR_ASSERT(calld->creation_phase ==
             GRPC_SUBCHANNEL_CALL_HOLDER_PICKING_SUBCHANNEL);
  grpc_polling_entity_del_from_pollset_set(exec_ctx, calld->pollent,
                                           chand->interested_parties);
  calld->creation_phase = GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING;
  if (calld->connected_subchannel == NULL) {
    gpr_atm_no_barrier_store(&calld->subchannel_call, 1);
    fail_locked(exec_ctx, calld, GRPC_ERROR_CREATE_REFERENCING(
                                     "Failed to create subchannel", &error, 1));
  } else if (GET_CALL(calld) == CANCELLED_CALL) {
    /* already cancelled before subchannel became ready */
    grpc_error *cancellation_error = GRPC_ERROR_CREATE_REFERENCING(
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
    grpc_error *new_error = grpc_connected_subchannel_create_call(
        exec_ctx, calld->connected_subchannel, calld->pollent, calld->path,
        calld->call_start_time, calld->deadline, &subchannel_call);
    if (new_error != GRPC_ERROR_NONE) {
      new_error = grpc_error_add_child(new_error, error);
      subchannel_call = CANCELLED_CALL;
      fail_locked(exec_ctx, calld, new_error);
    }
    gpr_atm_rel_store(&calld->subchannel_call,
                      (gpr_atm)(uintptr_t)subchannel_call);
    retry_waiting_locked(exec_ctx, calld);
  }
  gpr_mu_unlock(&calld->mu);
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
static bool pick_subchannel(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                            grpc_metadata_batch *initial_metadata,
                            uint32_t initial_metadata_flags,
                            grpc_connected_subchannel **connected_subchannel,
                            grpc_closure *on_ready, grpc_error *error);

static void continue_picking(grpc_exec_ctx *exec_ctx, void *arg,
                             grpc_error *error) {
  continue_picking_args *cpa = arg;
  if (cpa->connected_subchannel == NULL) {
    /* cancelled, do nothing */
  } else if (error != GRPC_ERROR_NONE) {
    grpc_closure_sched(exec_ctx, cpa->on_ready, GRPC_ERROR_REF(error));
  } else {
    call_data *calld = cpa->elem->call_data;
    gpr_mu_lock(&calld->mu);
    if (pick_subchannel(exec_ctx, cpa->elem, cpa->initial_metadata,
                        cpa->initial_metadata_flags, cpa->connected_subchannel,
                        cpa->on_ready, GRPC_ERROR_NONE)) {
      grpc_closure_sched(exec_ctx, cpa->on_ready, GRPC_ERROR_NONE);
    }
    gpr_mu_unlock(&calld->mu);
  }
  gpr_free(cpa);
}

static bool pick_subchannel(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                            grpc_metadata_batch *initial_metadata,
                            uint32_t initial_metadata_flags,
                            grpc_connected_subchannel **connected_subchannel,
                            grpc_closure *on_ready, grpc_error *error) {
  GPR_TIMER_BEGIN("pick_subchannel", 0);

  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  continue_picking_args *cpa;
  grpc_closure *closure;

  GPR_ASSERT(connected_subchannel);

  gpr_mu_lock(&chand->mu);
  if (initial_metadata == NULL) {
    if (chand->lb_policy != NULL) {
      grpc_lb_policy_cancel_pick(exec_ctx, chand->lb_policy,
                                 connected_subchannel, GRPC_ERROR_REF(error));
    }
    for (closure = chand->waiting_for_config_closures.head; closure != NULL;
         closure = closure->next_data.next) {
      cpa = closure->cb_arg;
      if (cpa->connected_subchannel == connected_subchannel) {
        cpa->connected_subchannel = NULL;
        grpc_closure_sched(
            exec_ctx, cpa->on_ready,
            GRPC_ERROR_CREATE_REFERENCING("Pick cancelled", &error, 1));
      }
    }
    gpr_mu_unlock(&chand->mu);
    GPR_TIMER_END("pick_subchannel", 0);
    GRPC_ERROR_UNREF(error);
    return true;
  }
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  if (chand->lb_policy != NULL) {
    grpc_lb_policy *lb_policy = chand->lb_policy;
    GRPC_LB_POLICY_REF(lb_policy, "pick_subchannel");
    gpr_mu_unlock(&chand->mu);
    // If the application explicitly set wait_for_ready, use that.
    // Otherwise, if the service config specified a value for this
    // method, use that.
    const bool wait_for_ready_set_from_api =
        initial_metadata_flags &
        GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
    const bool wait_for_ready_set_from_service_config =
        calld->wait_for_ready_from_service_config != WAIT_FOR_READY_UNSET;
    if (!wait_for_ready_set_from_api &&
        wait_for_ready_set_from_service_config) {
      if (calld->wait_for_ready_from_service_config == WAIT_FOR_READY_TRUE) {
        initial_metadata_flags |= GRPC_INITIAL_METADATA_WAIT_FOR_READY;
      } else {
        initial_metadata_flags &= ~GRPC_INITIAL_METADATA_WAIT_FOR_READY;
      }
    }
    const grpc_lb_policy_pick_args inputs = {
        initial_metadata, initial_metadata_flags, &calld->lb_token_mdelem,
        gpr_inf_future(GPR_CLOCK_MONOTONIC)};
    const bool result = grpc_lb_policy_pick(
        exec_ctx, lb_policy, &inputs, connected_subchannel, NULL, on_ready);
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "pick_subchannel");
    GPR_TIMER_END("pick_subchannel", 0);
    return result;
  }
  if (chand->resolver != NULL && !chand->started_resolving) {
    chand->started_resolving = true;
    GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
    grpc_resolver_next(exec_ctx, chand->resolver, &chand->resolver_result,
                       &chand->on_resolver_result_changed);
  }
  if (chand->resolver != NULL) {
    cpa = gpr_malloc(sizeof(*cpa));
    cpa->initial_metadata = initial_metadata;
    cpa->initial_metadata_flags = initial_metadata_flags;
    cpa->connected_subchannel = connected_subchannel;
    cpa->on_ready = on_ready;
    cpa->elem = elem;
    grpc_closure_init(&cpa->closure, continue_picking, cpa,
                      grpc_schedule_on_exec_ctx);
    grpc_closure_list_append(&chand->waiting_for_config_closures, &cpa->closure,
                             GRPC_ERROR_NONE);
  } else {
    grpc_closure_sched(exec_ctx, on_ready, GRPC_ERROR_CREATE("Disconnected"));
  }
  gpr_mu_unlock(&chand->mu);

  GPR_TIMER_END("pick_subchannel", 0);
  return false;
}

// The logic here is fairly complicated, due to (a) the fact that we
// need to handle the case where we receive the send op before the
// initial metadata op, and (b) the need for efficiency, especially in
// the streaming case.
// TODO(ctiller): Explain this more thoroughly.
static void cc_start_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                         grpc_call_element *elem,
                                         grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  grpc_deadline_state_client_start_transport_stream_op(exec_ctx, elem, op);
  /* try to (atomically) get the call */
  grpc_subchannel_call *call = GET_CALL(calld);
  GPR_TIMER_BEGIN("cc_start_transport_stream_op", 0);
  if (call == CANCELLED_CALL) {
    grpc_transport_stream_op_finish_with_failure(
        exec_ctx, op, GRPC_ERROR_REF(calld->cancel_error));
    GPR_TIMER_END("cc_start_transport_stream_op", 0);
    return;
  }
  if (call != NULL) {
    grpc_subchannel_call_process_op(exec_ctx, call, op);
    GPR_TIMER_END("cc_start_transport_stream_op", 0);
    return;
  }
  /* we failed; lock and figure out what to do */
  gpr_mu_lock(&calld->mu);
retry:
  /* need to recheck that another thread hasn't set the call */
  call = GET_CALL(calld);
  if (call == CANCELLED_CALL) {
    gpr_mu_unlock(&calld->mu);
    grpc_transport_stream_op_finish_with_failure(
        exec_ctx, op, GRPC_ERROR_REF(calld->cancel_error));
    GPR_TIMER_END("cc_start_transport_stream_op", 0);
    return;
  }
  if (call != NULL) {
    gpr_mu_unlock(&calld->mu);
    grpc_subchannel_call_process_op(exec_ctx, call, op);
    GPR_TIMER_END("cc_start_transport_stream_op", 0);
    return;
  }
  /* if this is a cancellation, then we can raise our cancelled flag */
  if (op->cancel_error != GRPC_ERROR_NONE) {
    if (!gpr_atm_rel_cas(&calld->subchannel_call, 0,
                         (gpr_atm)(uintptr_t)CANCELLED_CALL)) {
      goto retry;
    } else {
      // Stash a copy of cancel_error in our call data, so that we can use
      // it for subsequent operations.  This ensures that if the call is
      // cancelled before any ops are passed down (e.g., if the deadline
      // is in the past when the call starts), we can return the right
      // error to the caller when the first op does get passed down.
      calld->cancel_error = GRPC_ERROR_REF(op->cancel_error);
      switch (calld->creation_phase) {
        case GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING:
          fail_locked(exec_ctx, calld, GRPC_ERROR_REF(op->cancel_error));
          break;
        case GRPC_SUBCHANNEL_CALL_HOLDER_PICKING_SUBCHANNEL:
          pick_subchannel(exec_ctx, elem, NULL, 0, &calld->connected_subchannel,
                          NULL, GRPC_ERROR_REF(op->cancel_error));
          break;
      }
      gpr_mu_unlock(&calld->mu);
      grpc_transport_stream_op_finish_with_failure(
          exec_ctx, op, GRPC_ERROR_REF(op->cancel_error));
      GPR_TIMER_END("cc_start_transport_stream_op", 0);
      return;
    }
  }
  /* if we don't have a subchannel, try to get one */
  if (calld->creation_phase == GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING &&
      calld->connected_subchannel == NULL &&
      op->send_initial_metadata != NULL) {
    calld->creation_phase = GRPC_SUBCHANNEL_CALL_HOLDER_PICKING_SUBCHANNEL;
    grpc_closure_init(&calld->next_step, subchannel_ready, elem,
                      grpc_schedule_on_exec_ctx);
    GRPC_CALL_STACK_REF(calld->owning_call, "pick_subchannel");
    /* If a subchannel is not available immediately, the polling entity from
       call_data should be provided to channel_data's interested_parties, so
       that IO of the lb_policy and resolver could be done under it. */
    if (pick_subchannel(exec_ctx, elem, op->send_initial_metadata,
                        op->send_initial_metadata_flags,
                        &calld->connected_subchannel, &calld->next_step,
                        GRPC_ERROR_NONE)) {
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
    grpc_error *error = grpc_connected_subchannel_create_call(
        exec_ctx, calld->connected_subchannel, calld->pollent, calld->path,
        calld->call_start_time, calld->deadline, &subchannel_call);
    if (error != GRPC_ERROR_NONE) {
      subchannel_call = CANCELLED_CALL;
      fail_locked(exec_ctx, calld, GRPC_ERROR_REF(error));
      grpc_transport_stream_op_finish_with_failure(exec_ctx, op, error);
    }
    gpr_atm_rel_store(&calld->subchannel_call,
                      (gpr_atm)(uintptr_t)subchannel_call);
    retry_waiting_locked(exec_ctx, calld);
    goto retry;
  }
  /* nothing to be done but wait */
  add_waiting_locked(calld, op);
  gpr_mu_unlock(&calld->mu);
  GPR_TIMER_END("cc_start_transport_stream_op", 0);
}

// Gets data from the service config.  Invoked when the resolver returns
// its initial result.
static void read_service_config(grpc_exec_ctx *exec_ctx, void *arg,
                                grpc_error *error) {
  grpc_call_element *elem = arg;
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  // If this is an error, there's no point in looking at the service config.
  if (error == GRPC_ERROR_NONE) {
    // Get the method config table from channel data.
    gpr_mu_lock(&chand->mu);
    grpc_mdstr_hash_table *method_params_table = NULL;
    if (chand->method_params_table != NULL) {
      method_params_table =
          grpc_mdstr_hash_table_ref(chand->method_params_table);
    }
    gpr_mu_unlock(&chand->mu);
    // If the method config table was present, use it.
    if (method_params_table != NULL) {
      const method_parameters *method_params = grpc_method_config_table_get(
          exec_ctx, method_params_table, calld->path);
      if (method_params != NULL) {
        const bool have_method_timeout =
            gpr_time_cmp(method_params->timeout, gpr_time_0(GPR_TIMESPAN)) != 0;
        if (have_method_timeout ||
            method_params->wait_for_ready != WAIT_FOR_READY_UNSET) {
          gpr_mu_lock(&calld->mu);
          if (have_method_timeout) {
            const gpr_timespec per_method_deadline =
                gpr_time_add(calld->call_start_time, method_params->timeout);
            if (gpr_time_cmp(per_method_deadline, calld->deadline) < 0) {
              calld->deadline = per_method_deadline;
              // Reset deadline timer.
              grpc_deadline_state_reset(exec_ctx, elem, calld->deadline);
            }
          }
          if (method_params->wait_for_ready != WAIT_FOR_READY_UNSET) {
            calld->wait_for_ready_from_service_config =
                method_params->wait_for_ready;
          }
          gpr_mu_unlock(&calld->mu);
        }
      }
      grpc_mdstr_hash_table_unref(exec_ctx, method_params_table);
    }
  }
  GRPC_CALL_STACK_UNREF(exec_ctx, calld->owning_call, "read_service_config");
}

/* Constructor for call_data */
static grpc_error *cc_init_call_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_call_element *elem,
                                     grpc_call_element_args *args) {
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  // Initialize data members.
  grpc_deadline_state_init(exec_ctx, elem, args->call_stack);
  calld->path = GRPC_MDSTR_REF(args->path);
  calld->call_start_time = args->start_time;
  calld->deadline = gpr_convert_clock_type(args->deadline, GPR_CLOCK_MONOTONIC);
  calld->wait_for_ready_from_service_config = WAIT_FOR_READY_UNSET;
  calld->cancel_error = GRPC_ERROR_NONE;
  gpr_atm_rel_store(&calld->subchannel_call, 0);
  gpr_mu_init(&calld->mu);
  calld->connected_subchannel = NULL;
  calld->waiting_ops = NULL;
  calld->waiting_ops_count = 0;
  calld->waiting_ops_capacity = 0;
  calld->creation_phase = GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING;
  calld->owning_call = args->call_stack;
  calld->pollent = NULL;
  // If the resolver has already returned results, then we can access
  // the service config parameters immediately.  Otherwise, we need to
  // defer that work until the resolver returns an initial result.
  // TODO(roth): This code is almost but not quite identical to the code
  // in read_service_config() above.  It would be nice to find a way to
  // combine them, to avoid having to maintain it twice.
  gpr_mu_lock(&chand->mu);
  if (chand->lb_policy != NULL) {
    // We already have a resolver result, so check for service config.
    if (chand->method_params_table != NULL) {
      grpc_mdstr_hash_table *method_params_table =
          grpc_mdstr_hash_table_ref(chand->method_params_table);
      gpr_mu_unlock(&chand->mu);
      method_parameters *method_params = grpc_method_config_table_get(
          exec_ctx, method_params_table, args->path);
      if (method_params != NULL) {
        if (gpr_time_cmp(method_params->timeout,
                         gpr_time_0(GPR_CLOCK_MONOTONIC)) != 0) {
          gpr_timespec per_method_deadline =
              gpr_time_add(calld->call_start_time, method_params->timeout);
          calld->deadline = gpr_time_min(calld->deadline, per_method_deadline);
        }
        if (method_params->wait_for_ready != WAIT_FOR_READY_UNSET) {
          calld->wait_for_ready_from_service_config =
              method_params->wait_for_ready;
        }
      }
      grpc_mdstr_hash_table_unref(exec_ctx, method_params_table);
    } else {
      gpr_mu_unlock(&chand->mu);
    }
  } else {
    // We don't yet have a resolver result, so register a callback to
    // get the service config data once the resolver returns.
    // Take a reference to the call stack to be owned by the callback.
    GRPC_CALL_STACK_REF(calld->owning_call, "read_service_config");
    grpc_closure_init(&calld->read_service_config, read_service_config, elem,
                      grpc_schedule_on_exec_ctx);
    grpc_closure_list_append(&chand->waiting_for_config_closures,
                             &calld->read_service_config, GRPC_ERROR_NONE);
    gpr_mu_unlock(&chand->mu);
  }
  // Start the deadline timer with the current deadline value.  If we
  // do not yet have service config data, then the timer may be reset
  // later.
  grpc_deadline_state_start(exec_ctx, elem, calld->deadline);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void cc_destroy_call_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_call_element *elem,
                                 const grpc_call_final_info *final_info,
                                 void *and_free_memory) {
  call_data *calld = elem->call_data;
  grpc_deadline_state_destroy(exec_ctx, elem);
  GRPC_MDSTR_UNREF(exec_ctx, calld->path);
  GRPC_ERROR_UNREF(calld->cancel_error);
  grpc_subchannel_call *call = GET_CALL(calld);
  if (call != NULL && call != CANCELLED_CALL) {
    GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, call, "client_channel_destroy_call");
  }
  GPR_ASSERT(calld->creation_phase == GRPC_SUBCHANNEL_CALL_HOLDER_NOT_CREATING);
  gpr_mu_destroy(&calld->mu);
  GPR_ASSERT(calld->waiting_ops_count == 0);
  if (calld->connected_subchannel != NULL) {
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, calld->connected_subchannel,
                                    "picked");
  }
  gpr_free(calld->waiting_ops);
  gpr_free(and_free_memory);
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
    cc_start_transport_stream_op,
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

grpc_connectivity_state grpc_client_channel_check_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem, int try_to_connect) {
  channel_data *chand = elem->channel_data;
  grpc_connectivity_state out;
  gpr_mu_lock(&chand->mu);
  out = grpc_connectivity_state_check(&chand->state_tracker, NULL);
  if (out == GRPC_CHANNEL_IDLE && try_to_connect) {
    if (chand->lb_policy != NULL) {
      grpc_lb_policy_exit_idle(exec_ctx, chand->lb_policy);
    } else {
      chand->exit_idle_when_lb_policy_arrives = true;
      if (!chand->started_resolving && chand->resolver != NULL) {
        GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
        chand->started_resolving = true;
        grpc_resolver_next(exec_ctx, chand->resolver, &chand->resolver_result,
                           &chand->on_resolver_result_changed);
      }
    }
  }
  gpr_mu_unlock(&chand->mu);
  return out;
}

typedef struct {
  channel_data *chand;
  grpc_pollset *pollset;
  grpc_closure *on_complete;
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
  follow_up->cb(exec_ctx, follow_up->cb_arg, error);
}

void grpc_client_channel_watch_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem, grpc_pollset *pollset,
    grpc_connectivity_state *state, grpc_closure *on_complete) {
  channel_data *chand = elem->channel_data;
  external_connectivity_watcher *w = gpr_malloc(sizeof(*w));
  w->chand = chand;
  w->pollset = pollset;
  w->on_complete = on_complete;
  grpc_pollset_set_add_pollset(exec_ctx, chand->interested_parties, pollset);
  grpc_closure_init(&w->my_closure, on_external_watch_complete, w,
                    grpc_schedule_on_exec_ctx);
  GRPC_CHANNEL_STACK_REF(w->chand->owning_stack,
                         "external_connectivity_watcher");
  gpr_mu_lock(&chand->mu);
  grpc_connectivity_state_notify_on_state_change(
      exec_ctx, &chand->state_tracker, state, &w->my_closure);
  gpr_mu_unlock(&chand->mu);
}
