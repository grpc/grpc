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

#include "src/core/ext/filters/client_channel/subchannel.h"

#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <cstring>

#include <grpc/support/alloc.h>
#include <grpc/support/avl.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/subchannel_index.h"
#include "src/core/ext/filters/client_channel/uri_parser.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr++/debug_location.h"
#include "src/core/lib/gpr++/manual_constructor.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/connectivity_state.h"

#define INTERNAL_REF_BITS 16
#define STRONG_REF_MASK (~(gpr_atm)((1 << INTERNAL_REF_BITS) - 1))

#define GRPC_SUBCHANNEL_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_SUBCHANNEL_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_SUBCHANNEL_RECONNECT_MIN_TIMEOUT_SECONDS 20
#define GRPC_SUBCHANNEL_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_SUBCHANNEL_RECONNECT_JITTER 0.2

#define GET_CONNECTED_SUBCHANNEL(subchannel, barrier)     \
  ((grpc_connected_subchannel*)(gpr_atm_##barrier##_load( \
      &(subchannel)->connected_subchannel)))

namespace {
struct state_watcher {
  grpc_closure closure;
  grpc_subchannel* subchannel;
  grpc_connectivity_state connectivity_state;
};
}  // namespace

typedef struct external_state_watcher {
  grpc_subchannel* subchannel;
  grpc_pollset_set* pollset_set;
  grpc_closure* notify;
  grpc_closure closure;
  struct external_state_watcher* next;
  struct external_state_watcher* prev;
} external_state_watcher;

struct grpc_subchannel {
  grpc_connector* connector;

  /** refcount
      - lower INTERNAL_REF_BITS bits are for internal references:
        these do not keep the subchannel open.
      - upper remaining bits are for public references: these do
        keep the subchannel open */
  gpr_atm ref_pair;

  /** non-transport related channel filters */
  const grpc_channel_filter** filters;
  size_t num_filters;
  /** channel arguments */
  grpc_channel_args* args;

  grpc_subchannel_key* key;

  /** set during connection */
  grpc_connect_out_args connecting_result;

  /** callback for connection finishing */
  grpc_closure connected;

  /** callback for our alarm */
  grpc_closure on_alarm;

  /** pollset_set tracking who's interested in a connection
      being setup */
  grpc_pollset_set* pollset_set;

  /** active connection, or null; of type grpc_connected_subchannel */
  gpr_atm connected_subchannel;

  /** mutex protecting remaining elements */
  gpr_mu mu;

  /** have we seen a disconnection? */
  bool disconnected;
  /** are we connecting */
  bool connecting;
  /** connectivity state tracking */
  grpc_connectivity_state_tracker state_tracker;

  external_state_watcher root_external_state_watcher;

  /** backoff state */
  grpc_core::ManualConstructor<grpc_core::BackOff> backoff;
  grpc_millis next_attempt_deadline;
  grpc_millis min_connect_timeout_ms;

  /** do we have an active alarm? */
  bool have_alarm;
  /** have we started the backoff loop */
  bool backoff_begun;
  /** our alarm */
  grpc_timer alarm;
};

struct grpc_subchannel_call {
  grpc_connected_subchannel* connection;
  grpc_closure* schedule_closure_after_destroy;
};

#define SUBCHANNEL_CALL_TO_CALL_STACK(call) ((grpc_call_stack*)((call) + 1))
#define CHANNEL_STACK_FROM_CONNECTION(con) ((grpc_channel_stack*)(con))
#define CALLSTACK_TO_SUBCHANNEL_CALL(callstack) \
  (((grpc_subchannel_call*)(callstack)) - 1)

static void subchannel_connected(void* subchannel, grpc_error* error);

#ifndef NDEBUG
#define REF_REASON reason
#define REF_MUTATE_EXTRA_ARGS \
  GRPC_SUBCHANNEL_REF_EXTRA_ARGS, const char* purpose
#define REF_MUTATE_PURPOSE(x) , file, line, reason, x
#else
#define REF_REASON ""
#define REF_MUTATE_EXTRA_ARGS
#define REF_MUTATE_PURPOSE(x)
#endif

/*
 * connection implementation
 */

static void connection_destroy(void* arg, grpc_error* error) {
  grpc_connected_subchannel* c = (grpc_connected_subchannel*)arg;
  grpc_channel_stack_destroy(CHANNEL_STACK_FROM_CONNECTION(c));
  gpr_free(c);
}

grpc_connected_subchannel* grpc_connected_subchannel_ref(
    grpc_connected_subchannel* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  GRPC_CHANNEL_STACK_REF(CHANNEL_STACK_FROM_CONNECTION(c), REF_REASON);
  return c;
}

void grpc_connected_subchannel_unref(
    grpc_connected_subchannel* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  GRPC_CHANNEL_STACK_UNREF(CHANNEL_STACK_FROM_CONNECTION(c), REF_REASON);
}

/*
 * grpc_subchannel implementation
 */

static void subchannel_destroy(void* arg, grpc_error* error) {
  grpc_subchannel* c = (grpc_subchannel*)arg;
  gpr_free((void*)c->filters);
  grpc_channel_args_destroy(c->args);
  grpc_connectivity_state_destroy(&c->state_tracker);
  grpc_connector_unref(c->connector);
  grpc_pollset_set_destroy(c->pollset_set);
  grpc_subchannel_key_destroy(c->key);
  gpr_mu_destroy(&c->mu);
  gpr_free(c);
}

static gpr_atm ref_mutate(grpc_subchannel* c, gpr_atm delta,
                          int barrier REF_MUTATE_EXTRA_ARGS) {
  gpr_atm old_val = barrier ? gpr_atm_full_fetch_add(&c->ref_pair, delta)
                            : gpr_atm_no_barrier_fetch_add(&c->ref_pair, delta);
#ifndef NDEBUG
  if (grpc_trace_stream_refcount.enabled()) {
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "SUBCHANNEL: %p %12s 0x%" PRIxPTR " -> 0x%" PRIxPTR " [%s]", c,
            purpose, old_val, old_val + delta, reason);
  }
#endif
  return old_val;
}

grpc_subchannel* grpc_subchannel_ref(
    grpc_subchannel* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = ref_mutate(c, (1 << INTERNAL_REF_BITS),
                        0 REF_MUTATE_PURPOSE("STRONG_REF"));
  GPR_ASSERT((old_refs & STRONG_REF_MASK) != 0);
  return c;
}

grpc_subchannel* grpc_subchannel_weak_ref(
    grpc_subchannel* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = ref_mutate(c, 1, 0 REF_MUTATE_PURPOSE("WEAK_REF"));
  GPR_ASSERT(old_refs != 0);
  return c;
}

grpc_subchannel* grpc_subchannel_ref_from_weak_ref(
    grpc_subchannel* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  if (!c) return nullptr;
  for (;;) {
    gpr_atm old_refs = gpr_atm_acq_load(&c->ref_pair);
    if (old_refs >= (1 << INTERNAL_REF_BITS)) {
      gpr_atm new_refs = old_refs + (1 << INTERNAL_REF_BITS);
      if (gpr_atm_rel_cas(&c->ref_pair, old_refs, new_refs)) {
        return c;
      }
    } else {
      return nullptr;
    }
  }
}

static void disconnect(grpc_subchannel* c) {
  grpc_connected_subchannel* con;
  grpc_subchannel_index_unregister(c->key, c);
  gpr_mu_lock(&c->mu);
  GPR_ASSERT(!c->disconnected);
  c->disconnected = true;
  grpc_connector_shutdown(c->connector, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                            "Subchannel disconnected"));
  con = GET_CONNECTED_SUBCHANNEL(c, no_barrier);
  if (con != nullptr) {
    GRPC_CONNECTED_SUBCHANNEL_UNREF(con, "connection");
    gpr_atm_no_barrier_store(&c->connected_subchannel, (gpr_atm)0xdeadbeef);
  }
  gpr_mu_unlock(&c->mu);
}

void grpc_subchannel_unref(grpc_subchannel* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  // add a weak ref and subtract a strong ref (atomically)
  old_refs = ref_mutate(c, (gpr_atm)1 - (gpr_atm)(1 << INTERNAL_REF_BITS),
                        1 REF_MUTATE_PURPOSE("STRONG_UNREF"));
  if ((old_refs & STRONG_REF_MASK) == (1 << INTERNAL_REF_BITS)) {
    disconnect(c);
  }
  GRPC_SUBCHANNEL_WEAK_UNREF(c, "strong-unref");
}

void grpc_subchannel_weak_unref(
    grpc_subchannel* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = ref_mutate(c, -(gpr_atm)1, 1 REF_MUTATE_PURPOSE("WEAK_UNREF"));
  if (old_refs == 1) {
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_CREATE(subchannel_destroy, c, grpc_schedule_on_exec_ctx),
        GRPC_ERROR_NONE);
  }
}

static void parse_args_for_backoff_values(
    const grpc_channel_args* args, grpc_core::BackOff::Options* backoff_options,
    grpc_millis* min_connect_timeout_ms) {
  grpc_millis initial_backoff_ms =
      GRPC_SUBCHANNEL_INITIAL_CONNECT_BACKOFF_SECONDS * 1000;
  *min_connect_timeout_ms =
      GRPC_SUBCHANNEL_RECONNECT_MIN_TIMEOUT_SECONDS * 1000;
  grpc_millis max_backoff_ms =
      GRPC_SUBCHANNEL_RECONNECT_MAX_BACKOFF_SECONDS * 1000;
  bool fixed_reconnect_backoff = false;
  if (args != nullptr) {
    for (size_t i = 0; i < args->num_args; i++) {
      if (0 == strcmp(args->args[i].key,
                      "grpc.testing.fixed_reconnect_backoff_ms")) {
        fixed_reconnect_backoff = true;
        initial_backoff_ms = *min_connect_timeout_ms = max_backoff_ms =
            grpc_channel_arg_get_integer(
                &args->args[i],
                {static_cast<int>(initial_backoff_ms), 100, INT_MAX});
      } else if (0 ==
                 strcmp(args->args[i].key, GRPC_ARG_MIN_RECONNECT_BACKOFF_MS)) {
        fixed_reconnect_backoff = false;
        *min_connect_timeout_ms = grpc_channel_arg_get_integer(
            &args->args[i],
            {static_cast<int>(*min_connect_timeout_ms), 100, INT_MAX});
      } else if (0 ==
                 strcmp(args->args[i].key, GRPC_ARG_MAX_RECONNECT_BACKOFF_MS)) {
        fixed_reconnect_backoff = false;
        max_backoff_ms = grpc_channel_arg_get_integer(
            &args->args[i], {static_cast<int>(max_backoff_ms), 100, INT_MAX});
      } else if (0 == strcmp(args->args[i].key,
                             GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS)) {
        fixed_reconnect_backoff = false;
        initial_backoff_ms = grpc_channel_arg_get_integer(
            &args->args[i],
            {static_cast<int>(initial_backoff_ms), 100, INT_MAX});
      }
    }
  }
  backoff_options->set_initial_backoff(initial_backoff_ms)
      .set_multiplier(fixed_reconnect_backoff
                          ? 1.0
                          : GRPC_SUBCHANNEL_RECONNECT_BACKOFF_MULTIPLIER)
      .set_jitter(fixed_reconnect_backoff ? 0.0
                                          : GRPC_SUBCHANNEL_RECONNECT_JITTER)
      .set_max_backoff(max_backoff_ms);
}

grpc_subchannel* grpc_subchannel_create(grpc_connector* connector,
                                        const grpc_subchannel_args* args) {
  grpc_subchannel_key* key = grpc_subchannel_key_create(args);
  grpc_subchannel* c = grpc_subchannel_index_find(key);
  if (c) {
    grpc_subchannel_key_destroy(key);
    return c;
  }

  GRPC_STATS_INC_CLIENT_SUBCHANNELS_CREATED();
  c = (grpc_subchannel*)gpr_zalloc(sizeof(*c));
  c->key = key;
  gpr_atm_no_barrier_store(&c->ref_pair, 1 << INTERNAL_REF_BITS);
  c->connector = connector;
  grpc_connector_ref(c->connector);
  c->num_filters = args->filter_count;
  if (c->num_filters > 0) {
    c->filters = (const grpc_channel_filter**)gpr_malloc(
        sizeof(grpc_channel_filter*) * c->num_filters);
    memcpy((void*)c->filters, args->filters,
           sizeof(grpc_channel_filter*) * c->num_filters);
  } else {
    c->filters = nullptr;
  }
  c->pollset_set = grpc_pollset_set_create();
  grpc_resolved_address* addr =
      (grpc_resolved_address*)gpr_malloc(sizeof(*addr));
  grpc_get_subchannel_address_arg(args->args, addr);
  grpc_resolved_address* new_address = nullptr;
  grpc_channel_args* new_args = nullptr;
  if (grpc_proxy_mappers_map_address(addr, args->args, &new_address,
                                     &new_args)) {
    GPR_ASSERT(new_address != nullptr);
    gpr_free(addr);
    addr = new_address;
  }
  static const char* keys_to_remove[] = {GRPC_ARG_SUBCHANNEL_ADDRESS};
  grpc_arg new_arg = grpc_create_subchannel_address_arg(addr);
  gpr_free(addr);
  c->args = grpc_channel_args_copy_and_add_and_remove(
      new_args != nullptr ? new_args : args->args, keys_to_remove,
      GPR_ARRAY_SIZE(keys_to_remove), &new_arg, 1);
  gpr_free(new_arg.value.string);
  if (new_args != nullptr) grpc_channel_args_destroy(new_args);
  c->root_external_state_watcher.next = c->root_external_state_watcher.prev =
      &c->root_external_state_watcher;
  GRPC_CLOSURE_INIT(&c->connected, subchannel_connected, c,
                    grpc_schedule_on_exec_ctx);
  grpc_connectivity_state_init(&c->state_tracker, GRPC_CHANNEL_IDLE,
                               "subchannel");
  grpc_core::BackOff::Options backoff_options;
  parse_args_for_backoff_values(args->args, &backoff_options,
                                &c->min_connect_timeout_ms);
  c->backoff.Init(backoff_options);
  gpr_mu_init(&c->mu);

  return grpc_subchannel_index_register(key, c);
}

static void continue_connect_locked(grpc_subchannel* c) {
  grpc_connect_in_args args;
  args.interested_parties = c->pollset_set;
  const grpc_millis min_deadline =
      c->min_connect_timeout_ms + grpc_core::ExecCtx::Get()->Now();
  c->next_attempt_deadline = c->backoff->NextAttemptTime();
  args.deadline = std::max(c->next_attempt_deadline, min_deadline);
  args.channel_args = c->args;
  grpc_connectivity_state_set(&c->state_tracker, GRPC_CHANNEL_CONNECTING,
                              GRPC_ERROR_NONE, "state_change");
  grpc_connector_connect(c->connector, &args, &c->connecting_result,
                         &c->connected);
}

grpc_connectivity_state grpc_subchannel_check_connectivity(grpc_subchannel* c,
                                                           grpc_error** error) {
  grpc_connectivity_state state;
  gpr_mu_lock(&c->mu);
  state = grpc_connectivity_state_get(&c->state_tracker, error);
  gpr_mu_unlock(&c->mu);
  return state;
}

static void on_external_state_watcher_done(void* arg, grpc_error* error) {
  external_state_watcher* w = (external_state_watcher*)arg;
  grpc_closure* follow_up = w->notify;
  if (w->pollset_set != nullptr) {
    grpc_pollset_set_del_pollset_set(w->subchannel->pollset_set,
                                     w->pollset_set);
  }
  gpr_mu_lock(&w->subchannel->mu);
  w->next->prev = w->prev;
  w->prev->next = w->next;
  gpr_mu_unlock(&w->subchannel->mu);
  GRPC_SUBCHANNEL_WEAK_UNREF(w->subchannel, "external_state_watcher");
  gpr_free(w);
  GRPC_CLOSURE_RUN(follow_up, GRPC_ERROR_REF(error));
}

static void on_alarm(void* arg, grpc_error* error) {
  grpc_subchannel* c = (grpc_subchannel*)arg;
  gpr_mu_lock(&c->mu);
  c->have_alarm = false;
  if (c->disconnected) {
    error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Disconnected",
                                                             &error, 1);
  } else {
    GRPC_ERROR_REF(error);
  }
  if (error == GRPC_ERROR_NONE) {
    gpr_log(GPR_INFO, "Failed to connect to channel, retrying");
    continue_connect_locked(c);
    gpr_mu_unlock(&c->mu);
  } else {
    gpr_mu_unlock(&c->mu);
    GRPC_SUBCHANNEL_WEAK_UNREF(c, "connecting");
  }
  GRPC_ERROR_UNREF(error);
}

static void maybe_start_connecting_locked(grpc_subchannel* c) {
  if (c->disconnected) {
    /* Don't try to connect if we're already disconnected */
    return;
  }

  if (c->connecting) {
    /* Already connecting: don't restart */
    return;
  }

  if (GET_CONNECTED_SUBCHANNEL(c, no_barrier) != nullptr) {
    /* Already connected: don't restart */
    return;
  }

  if (!grpc_connectivity_state_has_watchers(&c->state_tracker)) {
    /* Nobody is interested in connecting: so don't just yet */
    return;
  }

  c->connecting = true;
  GRPC_SUBCHANNEL_WEAK_REF(c, "connecting");

  if (!c->backoff_begun) {
    c->backoff_begun = true;
    continue_connect_locked(c);
  } else {
    GPR_ASSERT(!c->have_alarm);
    c->have_alarm = true;
    const grpc_millis time_til_next =
        c->next_attempt_deadline - grpc_core::ExecCtx::Get()->Now();
    if (time_til_next <= 0) {
      gpr_log(GPR_INFO, "Retry immediately");
    } else {
      gpr_log(GPR_INFO, "Retry in %" PRIdPTR " milliseconds", time_til_next);
    }
    GRPC_CLOSURE_INIT(&c->on_alarm, on_alarm, c, grpc_schedule_on_exec_ctx);
    grpc_timer_init(&c->alarm, c->next_attempt_deadline, &c->on_alarm);
  }
}

void grpc_subchannel_notify_on_state_change(
    grpc_subchannel* c, grpc_pollset_set* interested_parties,
    grpc_connectivity_state* state, grpc_closure* notify) {
  external_state_watcher* w;

  if (state == nullptr) {
    gpr_mu_lock(&c->mu);
    for (w = c->root_external_state_watcher.next;
         w != &c->root_external_state_watcher; w = w->next) {
      if (w->notify == notify) {
        grpc_connectivity_state_notify_on_state_change(&c->state_tracker,
                                                       nullptr, &w->closure);
      }
    }
    gpr_mu_unlock(&c->mu);
  } else {
    w = (external_state_watcher*)gpr_malloc(sizeof(*w));
    w->subchannel = c;
    w->pollset_set = interested_parties;
    w->notify = notify;
    GRPC_CLOSURE_INIT(&w->closure, on_external_state_watcher_done, w,
                      grpc_schedule_on_exec_ctx);
    if (interested_parties != nullptr) {
      grpc_pollset_set_add_pollset_set(c->pollset_set, interested_parties);
    }
    GRPC_SUBCHANNEL_WEAK_REF(c, "external_state_watcher");
    gpr_mu_lock(&c->mu);
    w->next = &c->root_external_state_watcher;
    w->prev = w->next->prev;
    w->next->prev = w->prev->next = w;
    grpc_connectivity_state_notify_on_state_change(&c->state_tracker, state,
                                                   &w->closure);
    maybe_start_connecting_locked(c);
    gpr_mu_unlock(&c->mu);
  }
}

void grpc_connected_subchannel_process_transport_op(
    grpc_connected_subchannel* con, grpc_transport_op* op) {
  grpc_channel_stack* channel_stack = CHANNEL_STACK_FROM_CONNECTION(con);
  grpc_channel_element* top_elem = grpc_channel_stack_element(channel_stack, 0);
  top_elem->filter->start_transport_op(top_elem, op);
}

static void subchannel_on_child_state_changed(void* p, grpc_error* error) {
  state_watcher* sw = (state_watcher*)p;
  grpc_subchannel* c = sw->subchannel;
  gpr_mu* mu = &c->mu;

  gpr_mu_lock(mu);

  /* if we failed just leave this closure */
  if (sw->connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    /* any errors on a subchannel ==> we're done, create a new one */
    sw->connectivity_state = GRPC_CHANNEL_SHUTDOWN;
  }
  grpc_connectivity_state_set(&c->state_tracker, sw->connectivity_state,
                              GRPC_ERROR_REF(error), "reflect_child");
  if (sw->connectivity_state != GRPC_CHANNEL_SHUTDOWN) {
    grpc_connected_subchannel_notify_on_state_change(
        GET_CONNECTED_SUBCHANNEL(c, no_barrier), nullptr,
        &sw->connectivity_state, &sw->closure);
    GRPC_SUBCHANNEL_WEAK_REF(c, "state_watcher");
    sw = nullptr;
  }

  gpr_mu_unlock(mu);
  GRPC_SUBCHANNEL_WEAK_UNREF(c, "state_watcher");
  gpr_free(sw);
}

static void connected_subchannel_state_op(grpc_connected_subchannel* con,
                                          grpc_pollset_set* interested_parties,
                                          grpc_connectivity_state* state,
                                          grpc_closure* closure) {
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  grpc_channel_element* elem;
  op->connectivity_state = state;
  op->on_connectivity_state_change = closure;
  op->bind_pollset_set = interested_parties;
  elem = grpc_channel_stack_element(CHANNEL_STACK_FROM_CONNECTION(con), 0);
  elem->filter->start_transport_op(elem, op);
}

void grpc_connected_subchannel_notify_on_state_change(
    grpc_connected_subchannel* con, grpc_pollset_set* interested_parties,
    grpc_connectivity_state* state, grpc_closure* closure) {
  connected_subchannel_state_op(con, interested_parties, state, closure);
}

void grpc_connected_subchannel_ping(grpc_connected_subchannel* con,
                                    grpc_closure* on_initiate,
                                    grpc_closure* on_ack) {
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  grpc_channel_element* elem;
  op->send_ping.on_initiate = on_initiate;
  op->send_ping.on_ack = on_ack;
  elem = grpc_channel_stack_element(CHANNEL_STACK_FROM_CONNECTION(con), 0);
  elem->filter->start_transport_op(elem, op);
}

static bool publish_transport_locked(grpc_subchannel* c) {
  grpc_connected_subchannel* con;
  grpc_channel_stack* stk;
  state_watcher* sw_subchannel;

  /* construct channel stack */
  grpc_channel_stack_builder* builder = grpc_channel_stack_builder_create();
  grpc_channel_stack_builder_set_channel_arguments(
      builder, c->connecting_result.channel_args);
  grpc_channel_stack_builder_set_transport(builder,
                                           c->connecting_result.transport);

  if (!grpc_channel_init_create_stack(builder, GRPC_CLIENT_SUBCHANNEL)) {
    grpc_channel_stack_builder_destroy(builder);
    return false;
  }
  grpc_error* error = grpc_channel_stack_builder_finish(
      builder, 0, 1, connection_destroy, nullptr, (void**)&con);
  if (error != GRPC_ERROR_NONE) {
    grpc_transport_destroy(c->connecting_result.transport);
    gpr_log(GPR_ERROR, "error initializing subchannel stack: %s",
            grpc_error_string(error));
    GRPC_ERROR_UNREF(error);
    return false;
  }
  stk = CHANNEL_STACK_FROM_CONNECTION(con);
  memset(&c->connecting_result, 0, sizeof(c->connecting_result));

  /* initialize state watcher */
  sw_subchannel = (state_watcher*)gpr_malloc(sizeof(*sw_subchannel));
  sw_subchannel->subchannel = c;
  sw_subchannel->connectivity_state = GRPC_CHANNEL_READY;
  GRPC_CLOSURE_INIT(&sw_subchannel->closure, subchannel_on_child_state_changed,
                    sw_subchannel, grpc_schedule_on_exec_ctx);

  if (c->disconnected) {
    gpr_free(sw_subchannel);
    grpc_channel_stack_destroy(stk);
    gpr_free(con);
    return false;
  }

  /* publish */
  /* TODO(ctiller): this full barrier seems to clear up a TSAN failure.
                    I'd have expected the rel_cas below to be enough, but
                    seemingly it's not.
                    Re-evaluate if we really need this. */
  gpr_atm_full_barrier();
  GPR_ASSERT(gpr_atm_rel_cas(&c->connected_subchannel, 0, (gpr_atm)con));

  /* setup subchannel watching connected subchannel for changes; subchannel
     ref for connecting is donated to the state watcher */
  GRPC_SUBCHANNEL_WEAK_REF(c, "state_watcher");
  GRPC_SUBCHANNEL_WEAK_UNREF(c, "connecting");
  grpc_connected_subchannel_notify_on_state_change(
      con, c->pollset_set, &sw_subchannel->connectivity_state,
      &sw_subchannel->closure);

  /* signal completion */
  grpc_connectivity_state_set(&c->state_tracker, GRPC_CHANNEL_READY,
                              GRPC_ERROR_NONE, "connected");
  return true;
}

static void subchannel_connected(void* arg, grpc_error* error) {
  grpc_subchannel* c = (grpc_subchannel*)arg;
  grpc_channel_args* delete_channel_args = c->connecting_result.channel_args;

  GRPC_SUBCHANNEL_WEAK_REF(c, "connected");
  gpr_mu_lock(&c->mu);
  c->connecting = false;
  if (c->connecting_result.transport != nullptr &&
      publish_transport_locked(c)) {
    /* do nothing, transport was published */
  } else if (c->disconnected) {
    GRPC_SUBCHANNEL_WEAK_UNREF(c, "connecting");
  } else {
    grpc_connectivity_state_set(
        &c->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
        grpc_error_set_int(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                               "Connect Failed", &error, 1),
                           GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE),
        "connect_failed");

    const char* errmsg = grpc_error_string(error);
    gpr_log(GPR_INFO, "Connect failed: %s", errmsg);

    maybe_start_connecting_locked(c);
    GRPC_SUBCHANNEL_WEAK_UNREF(c, "connecting");
  }
  gpr_mu_unlock(&c->mu);
  GRPC_SUBCHANNEL_WEAK_UNREF(c, "connected");
  grpc_channel_args_destroy(delete_channel_args);
}

/*
 * grpc_subchannel_call implementation
 */

static void subchannel_call_destroy(void* call, grpc_error* error) {
  grpc_subchannel_call* c = (grpc_subchannel_call*)call;
  GPR_ASSERT(c->schedule_closure_after_destroy != nullptr);
  GPR_TIMER_BEGIN("grpc_subchannel_call_unref.destroy", 0);
  grpc_connected_subchannel* connection = c->connection;
  grpc_call_stack_destroy(SUBCHANNEL_CALL_TO_CALL_STACK(c), nullptr,
                          c->schedule_closure_after_destroy);
  GRPC_CONNECTED_SUBCHANNEL_UNREF(connection, "subchannel_call");
  GPR_TIMER_END("grpc_subchannel_call_unref.destroy", 0);
}

void grpc_subchannel_call_set_cleanup_closure(grpc_subchannel_call* call,
                                              grpc_closure* closure) {
  GPR_ASSERT(call->schedule_closure_after_destroy == nullptr);
  GPR_ASSERT(closure != nullptr);
  call->schedule_closure_after_destroy = closure;
}

void grpc_subchannel_call_ref(
    grpc_subchannel_call* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(c), REF_REASON);
}

void grpc_subchannel_call_unref(
    grpc_subchannel_call* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  GRPC_CALL_STACK_UNREF(SUBCHANNEL_CALL_TO_CALL_STACK(c), REF_REASON);
}

void grpc_subchannel_call_process_op(grpc_subchannel_call* call,
                                     grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_BEGIN("grpc_subchannel_call_process_op", 0);
  grpc_call_stack* call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  grpc_call_element* top_elem = grpc_call_stack_element(call_stack, 0);
  GRPC_CALL_LOG_OP(GPR_INFO, top_elem, batch);
  top_elem->filter->start_transport_stream_op_batch(top_elem, batch);
  GPR_TIMER_END("grpc_subchannel_call_process_op", 0);
}

grpc_connected_subchannel* grpc_subchannel_get_connected_subchannel(
    grpc_subchannel* c) {
  return GET_CONNECTED_SUBCHANNEL(c, acq);
}

const grpc_subchannel_key* grpc_subchannel_get_key(
    const grpc_subchannel* subchannel) {
  return subchannel->key;
}

grpc_error* grpc_connected_subchannel_create_call(
    grpc_connected_subchannel* con,
    const grpc_connected_subchannel_call_args* args,
    grpc_subchannel_call** call) {
  grpc_channel_stack* chanstk = CHANNEL_STACK_FROM_CONNECTION(con);
  *call = (grpc_subchannel_call*)gpr_arena_alloc(
      args->arena, sizeof(grpc_subchannel_call) + chanstk->call_stack_size);
  grpc_call_stack* callstk = SUBCHANNEL_CALL_TO_CALL_STACK(*call);
  (*call)->connection = GRPC_CONNECTED_SUBCHANNEL_REF(con, "subchannel_call");
  const grpc_call_element_args call_args = {
      callstk,            /* call_stack */
      nullptr,            /* server_transport_data */
      args->context,      /* context */
      args->path,         /* path */
      args->start_time,   /* start_time */
      args->deadline,     /* deadline */
      args->arena,        /* arena */
      args->call_combiner /* call_combiner */
  };
  grpc_error* error = grpc_call_stack_init(chanstk, 1, subchannel_call_destroy,
                                           *call, &call_args);
  if (error != GRPC_ERROR_NONE) {
    const char* error_string = grpc_error_string(error);
    gpr_log(GPR_ERROR, "error: %s", error_string);
    return error;
  }
  grpc_call_stack_set_pollset_or_pollset_set(callstk, args->pollent);
  return GRPC_ERROR_NONE;
}

grpc_call_stack* grpc_subchannel_call_get_call_stack(
    grpc_subchannel_call* subchannel_call) {
  return SUBCHANNEL_CALL_TO_CALL_STACK(subchannel_call);
}

static void grpc_uri_to_sockaddr(const char* uri_str,
                                 grpc_resolved_address* addr) {
  grpc_uri* uri = grpc_uri_parse(uri_str, 0 /* suppress_errors */);
  GPR_ASSERT(uri != nullptr);
  if (!grpc_parse_uri(uri, addr)) memset(addr, 0, sizeof(*addr));
  grpc_uri_destroy(uri);
}

void grpc_get_subchannel_address_arg(const grpc_channel_args* args,
                                     grpc_resolved_address* addr) {
  const char* addr_uri_str = grpc_get_subchannel_address_uri_arg(args);
  memset(addr, 0, sizeof(*addr));
  if (*addr_uri_str != '\0') {
    grpc_uri_to_sockaddr(addr_uri_str, addr);
  }
}

const char* grpc_get_subchannel_address_uri_arg(const grpc_channel_args* args) {
  const grpc_arg* addr_arg =
      grpc_channel_args_find(args, GRPC_ARG_SUBCHANNEL_ADDRESS);
  GPR_ASSERT(addr_arg != nullptr);  // Should have been set by LB policy.
  GPR_ASSERT(addr_arg->type == GRPC_ARG_STRING);
  return addr_arg->value.string;
}

grpc_arg grpc_create_subchannel_address_arg(const grpc_resolved_address* addr) {
  return grpc_channel_arg_string_create(
      (char*)GRPC_ARG_SUBCHANNEL_ADDRESS,
      addr->len > 0 ? grpc_sockaddr_to_uri(addr) : gpr_strdup(""));
}
