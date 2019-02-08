/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/ext/filters/max_age/max_age_filter.h"

#include <limits.h>
#include <string.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/http2_errors.h"

#define DEFAULT_MAX_CONNECTION_AGE_MS INT_MAX
#define DEFAULT_MAX_CONNECTION_AGE_GRACE_MS INT_MAX
#define DEFAULT_MAX_CONNECTION_IDLE_MS INT_MAX
#define MAX_CONNECTION_AGE_JITTER 0.1

#define MAX_CONNECTION_AGE_INTEGER_OPTIONS \
  { DEFAULT_MAX_CONNECTION_AGE_MS, 1, INT_MAX }
#define MAX_CONNECTION_IDLE_INTEGER_OPTIONS \
  { DEFAULT_MAX_CONNECTION_IDLE_MS, 1, INT_MAX }

/* States for idle_state in channel_data */
#define MAX_IDLE_STATE_INIT ((gpr_atm)0)
#define MAX_IDLE_STATE_SEEN_EXIT_IDLE ((gpr_atm)1)
#define MAX_IDLE_STATE_SEEN_ENTER_IDLE ((gpr_atm)2)
#define MAX_IDLE_STATE_TIMER_SET ((gpr_atm)3)

namespace {
struct channel_data {
  /* We take a reference to the channel stack for the timer callback */
  grpc_channel_stack* channel_stack;
  /* Guards access to max_age_timer, max_age_timer_pending, max_age_grace_timer
     and max_age_grace_timer_pending */
  gpr_mu max_age_timer_mu;
  /* True if the max_age timer callback is currently pending */
  bool max_age_timer_pending;
  /* True if the max_age_grace timer callback is currently pending */
  bool max_age_grace_timer_pending;
  /* The timer for checking if the channel has reached its max age */
  grpc_timer max_age_timer;
  /* The timer for checking if the max-aged channel has uesed up the grace
     period */
  grpc_timer max_age_grace_timer;
  /* The timer for checking if the channel's idle duration reaches
     max_connection_idle */
  grpc_timer max_idle_timer;
  /* Allowed max time a channel may have no outstanding rpcs */
  grpc_millis max_connection_idle;
  /* Allowed max time a channel may exist */
  grpc_millis max_connection_age;
  /* Allowed grace period after the channel reaches its max age */
  grpc_millis max_connection_age_grace;
  /* Closure to run when the channel's idle duration reaches max_connection_idle
     and should be closed gracefully */
  grpc_closure max_idle_timer_cb;
  /* Closure to run when the channel reaches its max age and should be closed
     gracefully */
  grpc_closure close_max_age_channel;
  /* Closure to run the channel uses up its max age grace time and should be
     closed forcibly */
  grpc_closure force_close_max_age_channel;
  /* Closure to run when the init fo channel stack is done and the max_idle
     timer should be started */
  grpc_closure start_max_idle_timer_after_init;
  /* Closure to run when the init fo channel stack is done and the max_age timer
     should be started */
  grpc_closure start_max_age_timer_after_init;
  /* Closure to run when the goaway op is finished and the max_age_timer */
  grpc_closure start_max_age_grace_timer_after_goaway_op;
  /* Closure to run when the channel connectivity state changes */
  grpc_closure channel_connectivity_changed;
  /* Records the current connectivity state */
  grpc_connectivity_state connectivity_state;
  /* Number of active calls */
  gpr_atm call_count;
  /* TODO(zyc): C++lize this state machine */
  /* 'idle_state' holds the states of max_idle_timer and channel idleness.
      It can contain one of the following values:
     +--------------------------------+----------------+---------+
     |           idle_state           | max_idle_timer | channel |
     +--------------------------------+----------------+---------+
     | MAX_IDLE_STATE_INIT            | unset          | busy    |
     | MAX_IDLE_STATE_TIMER_SET       | set, valid     | idle    |
     | MAX_IDLE_STATE_SEEN_EXIT_IDLE  | set, invalid   | busy    |
     | MAX_IDLE_STATE_SEEN_ENTER_IDLE | set, invalid   | idle    |
     +--------------------------------+----------------+---------+

     MAX_IDLE_STATE_INIT: The initial and final state of 'idle_state'. The
     channel has 1 or 1+ active calls, and the timer is not set. Note that
     we may put a virtual call to hold this state at channel initialization or
     shutdown, so that the channel won't enter other states.

     MAX_IDLE_STATE_TIMER_SET: The state after the timer is set and no calls
     have arrived after the timer is set. The channel must have 0 active call in
     this state. If the timer is fired in this state, we will close the channel
     due to idleness.

     MAX_IDLE_STATE_SEEN_EXIT_IDLE: The state after the timer is set and at
     least one call has arrived after the timer is set. The channel must have 1
     or 1+ active calls in this state. If the timer is fired in this state, we
     won't reschudle it.

     MAX_IDLE_STATE_SEEN_ENTER_IDLE: The state after the timer is set and the at
     least one call has arrived after the timer is set, BUT the channel
     currently has 1 or 1+ active calls. If the timer is fired in this state, we
     will reschudle it.

     max_idle_timer will not be cancelled (unless the channel is shutting down).
     If the timer callback is called when the max_idle_timer is valid (i.e.
     idle_state is MAX_IDLE_STATE_TIMER_SET), the channel will be closed due to
     idleness, otherwise the channel won't be changed.

     State transitions:
         MAX_IDLE_STATE_INIT <-------3------ MAX_IDLE_STATE_SEEN_EXIT_IDLE
              ^    |                              ^     ^    |
              |    |                              |     |    |
              1    2     +-----------4------------+     6    7
              |    |     |                              |    |
              |    v     |                              |    v
       MAX_IDLE_STATE_TIMER_SET <----5------ MAX_IDLE_STATE_SEEN_ENTER_IDLE

     For 1, 3, 5 :  See max_idle_timer_cb() function
     For 2, 7    :  See decrease_call_count() function
     For 4, 6    :  See increase_call_count() function */
  gpr_atm idle_state;
  /* Time when the channel finished its last outstanding call, in grpc_millis */
  gpr_atm last_enter_idle_time_millis;
};
}  // namespace

/* Increase the nubmer of active calls. Before the increasement, if there are no
   calls, the max_idle_timer should be cancelled. */
static void increase_call_count(channel_data* chand) {
  /* Exit idle */
  if (gpr_atm_full_fetch_add(&chand->call_count, 1) == 0) {
    while (true) {
      gpr_atm idle_state = gpr_atm_acq_load(&chand->idle_state);
      switch (idle_state) {
        case MAX_IDLE_STATE_TIMER_SET:
          /* max_idle_timer_cb may have already set idle_state to
             MAX_IDLE_STATE_INIT, in this case, we don't need to set it to
             MAX_IDLE_STATE_SEEN_EXIT_IDLE */
          gpr_atm_rel_cas(&chand->idle_state, MAX_IDLE_STATE_TIMER_SET,
                          MAX_IDLE_STATE_SEEN_EXIT_IDLE);
          return;
        case MAX_IDLE_STATE_SEEN_ENTER_IDLE:
          gpr_atm_rel_store(&chand->idle_state, MAX_IDLE_STATE_SEEN_EXIT_IDLE);
          return;
        default:
          /* try again */
          break;
      }
    }
  }
}

/* Decrease the nubmer of active calls. After the decrement, if there are no
   calls, the max_idle_timer should be started. */
static void decrease_call_count(channel_data* chand) {
  /* Enter idle */
  if (gpr_atm_full_fetch_add(&chand->call_count, -1) == 1) {
    gpr_atm_no_barrier_store(&chand->last_enter_idle_time_millis,
                             (gpr_atm)grpc_core::ExecCtx::Get()->Now());
    while (true) {
      gpr_atm idle_state = gpr_atm_acq_load(&chand->idle_state);
      switch (idle_state) {
        case MAX_IDLE_STATE_INIT:
          GRPC_CHANNEL_STACK_REF(chand->channel_stack,
                                 "max_age max_idle_timer");
          grpc_timer_init(
              &chand->max_idle_timer,
              grpc_core::ExecCtx::Get()->Now() + chand->max_connection_idle,
              &chand->max_idle_timer_cb);
          gpr_atm_rel_store(&chand->idle_state, MAX_IDLE_STATE_TIMER_SET);
          return;
        case MAX_IDLE_STATE_SEEN_EXIT_IDLE:
          if (gpr_atm_rel_cas(&chand->idle_state, MAX_IDLE_STATE_SEEN_EXIT_IDLE,
                              MAX_IDLE_STATE_SEEN_ENTER_IDLE)) {
            return;
          }
          break;
        default:
          /* try again */
          break;
      }
    }
  }
}

static void start_max_idle_timer_after_init(void* arg, grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(arg);
  /* Decrease call_count. If there are no active calls at this time,
     max_idle_timer will start here. If the number of active calls is not 0,
     max_idle_timer will start after all the active calls end. */
  decrease_call_count(chand);
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack,
                           "max_age start_max_idle_timer_after_init");
}

static void start_max_age_timer_after_init(void* arg, grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(arg);
  gpr_mu_lock(&chand->max_age_timer_mu);
  chand->max_age_timer_pending = true;
  GRPC_CHANNEL_STACK_REF(chand->channel_stack, "max_age max_age_timer");
  grpc_timer_init(&chand->max_age_timer,
                  grpc_core::ExecCtx::Get()->Now() + chand->max_connection_age,
                  &chand->close_max_age_channel);
  gpr_mu_unlock(&chand->max_age_timer_mu);
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  op->on_connectivity_state_change = &chand->channel_connectivity_changed;
  op->connectivity_state = &chand->connectivity_state;
  grpc_channel_next_op(grpc_channel_stack_element(chand->channel_stack, 0), op);
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack,
                           "max_age start_max_age_timer_after_init");
}

static void start_max_age_grace_timer_after_goaway_op(void* arg,
                                                      grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(arg);
  gpr_mu_lock(&chand->max_age_timer_mu);
  chand->max_age_grace_timer_pending = true;
  GRPC_CHANNEL_STACK_REF(chand->channel_stack, "max_age max_age_grace_timer");
  grpc_timer_init(
      &chand->max_age_grace_timer,
      chand->max_connection_age_grace == GRPC_MILLIS_INF_FUTURE
          ? GRPC_MILLIS_INF_FUTURE
          : grpc_core::ExecCtx::Get()->Now() + chand->max_connection_age_grace,
      &chand->force_close_max_age_channel);
  gpr_mu_unlock(&chand->max_age_timer_mu);
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack,
                           "max_age start_max_age_grace_timer_after_goaway_op");
}

static void close_max_idle_channel(channel_data* chand) {
  /* Prevent the max idle timer from being set again */
  gpr_atm_no_barrier_fetch_add(&chand->call_count, 1);
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  op->goaway_error =
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("max_idle"),
                         GRPC_ERROR_INT_HTTP2_ERROR, GRPC_HTTP2_NO_ERROR);
  grpc_channel_element* elem =
      grpc_channel_stack_element(chand->channel_stack, 0);
  elem->filter->start_transport_op(elem, op);
}

static void max_idle_timer_cb(void* arg, grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(arg);
  if (error == GRPC_ERROR_NONE) {
    bool try_again = true;
    while (try_again) {
      gpr_atm idle_state = gpr_atm_acq_load(&chand->idle_state);
      switch (idle_state) {
        case MAX_IDLE_STATE_TIMER_SET:
          close_max_idle_channel(chand);
          /* This MAX_IDLE_STATE_INIT is a final state, we don't have to check
           * if idle_state has been changed */
          gpr_atm_rel_store(&chand->idle_state, MAX_IDLE_STATE_INIT);
          try_again = false;
          break;
        case MAX_IDLE_STATE_SEEN_EXIT_IDLE:
          if (gpr_atm_rel_cas(&chand->idle_state, MAX_IDLE_STATE_SEEN_EXIT_IDLE,
                              MAX_IDLE_STATE_INIT)) {
            try_again = false;
          }
          break;
        case MAX_IDLE_STATE_SEEN_ENTER_IDLE:
          GRPC_CHANNEL_STACK_REF(chand->channel_stack,
                                 "max_age max_idle_timer");
          grpc_timer_init(&chand->max_idle_timer,
                          static_cast<grpc_millis>(gpr_atm_no_barrier_load(
                              &chand->last_enter_idle_time_millis)) +
                              chand->max_connection_idle,
                          &chand->max_idle_timer_cb);
          /* idle_state may have already been set to
             MAX_IDLE_STATE_SEEN_EXIT_IDLE by increase_call_count(), in this
             case, we don't need to set it to MAX_IDLE_STATE_TIMER_SET */
          gpr_atm_rel_cas(&chand->idle_state, MAX_IDLE_STATE_SEEN_ENTER_IDLE,
                          MAX_IDLE_STATE_TIMER_SET);
          try_again = false;
          break;
        default:
          /* try again */
          break;
      }
    }
  }
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack, "max_age max_idle_timer");
}

static void close_max_age_channel(void* arg, grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(arg);
  gpr_mu_lock(&chand->max_age_timer_mu);
  chand->max_age_timer_pending = false;
  gpr_mu_unlock(&chand->max_age_timer_mu);
  if (error == GRPC_ERROR_NONE) {
    GRPC_CHANNEL_STACK_REF(chand->channel_stack,
                           "max_age start_max_age_grace_timer_after_goaway_op");
    grpc_transport_op* op = grpc_make_transport_op(
        &chand->start_max_age_grace_timer_after_goaway_op);
    op->goaway_error =
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("max_age"),
                           GRPC_ERROR_INT_HTTP2_ERROR, GRPC_HTTP2_NO_ERROR);
    grpc_channel_element* elem =
        grpc_channel_stack_element(chand->channel_stack, 0);
    elem->filter->start_transport_op(elem, op);
  } else if (error != GRPC_ERROR_CANCELLED) {
    GRPC_LOG_IF_ERROR("close_max_age_channel", error);
  }
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack, "max_age max_age_timer");
}

static void force_close_max_age_channel(void* arg, grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(arg);
  gpr_mu_lock(&chand->max_age_timer_mu);
  chand->max_age_grace_timer_pending = false;
  gpr_mu_unlock(&chand->max_age_timer_mu);
  if (error == GRPC_ERROR_NONE) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->disconnect_with_error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel reaches max age");
    grpc_channel_element* elem =
        grpc_channel_stack_element(chand->channel_stack, 0);
    elem->filter->start_transport_op(elem, op);
  } else if (error != GRPC_ERROR_CANCELLED) {
    GRPC_LOG_IF_ERROR("force_close_max_age_channel", error);
  }
  GRPC_CHANNEL_STACK_UNREF(chand->channel_stack, "max_age max_age_grace_timer");
}

static void channel_connectivity_changed(void* arg, grpc_error* error) {
  channel_data* chand = static_cast<channel_data*>(arg);
  if (chand->connectivity_state != GRPC_CHANNEL_SHUTDOWN) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->on_connectivity_state_change = &chand->channel_connectivity_changed;
    op->connectivity_state = &chand->connectivity_state;
    grpc_channel_next_op(grpc_channel_stack_element(chand->channel_stack, 0),
                         op);
  } else {
    gpr_mu_lock(&chand->max_age_timer_mu);
    if (chand->max_age_timer_pending) {
      grpc_timer_cancel(&chand->max_age_timer);
      chand->max_age_timer_pending = false;
    }
    if (chand->max_age_grace_timer_pending) {
      grpc_timer_cancel(&chand->max_age_grace_timer);
      chand->max_age_grace_timer_pending = false;
    }
    gpr_mu_unlock(&chand->max_age_timer_mu);
    /* If there are no active calls, this increasement will cancel
       max_idle_timer, and prevent max_idle_timer from being started in the
       future. */
    increase_call_count(chand);
    if (gpr_atm_acq_load(&chand->idle_state) == MAX_IDLE_STATE_SEEN_EXIT_IDLE) {
      grpc_timer_cancel(&chand->max_idle_timer);
    }
  }
}

/* A random jitter of +/-10% will be added to MAX_CONNECTION_AGE to spread out
   connection storms. Note that the MAX_CONNECTION_AGE option without jitter
   would not create connection storms by itself, but if there happened to be a
   connection storm it could cause it to repeat at a fixed period. */
static grpc_millis
add_random_max_connection_age_jitter_and_convert_to_grpc_millis(int value) {
  /* generate a random number between 1 - MAX_CONNECTION_AGE_JITTER and
     1 + MAX_CONNECTION_AGE_JITTER */
  double multiplier = rand() * MAX_CONNECTION_AGE_JITTER * 2.0 / RAND_MAX +
                      1.0 - MAX_CONNECTION_AGE_JITTER;
  double result = multiplier * value;
  /* INT_MAX - 0.5 converts the value to float, so that result will not be
     cast to int implicitly before the comparison. */
  return result > (static_cast<double>(GRPC_MILLIS_INF_FUTURE)) - 0.5
             ? GRPC_MILLIS_INF_FUTURE
             : static_cast<grpc_millis>(result);
}

/* Constructor for call_data. */
static grpc_error* init_call_elem(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  increase_call_count(chand);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data. */
static void destroy_call_elem(grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  decrease_call_count(chand);
}

/* Constructor for channel_data. */
static grpc_error* init_channel_elem(grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  gpr_mu_init(&chand->max_age_timer_mu);
  chand->max_age_timer_pending = false;
  chand->max_age_grace_timer_pending = false;
  chand->channel_stack = args->channel_stack;
  chand->max_connection_age =
      add_random_max_connection_age_jitter_and_convert_to_grpc_millis(
          DEFAULT_MAX_CONNECTION_AGE_MS);
  chand->max_connection_age_grace =
      DEFAULT_MAX_CONNECTION_AGE_GRACE_MS == INT_MAX
          ? GRPC_MILLIS_INF_FUTURE
          : DEFAULT_MAX_CONNECTION_AGE_GRACE_MS;
  chand->max_connection_idle = DEFAULT_MAX_CONNECTION_IDLE_MS == INT_MAX
                                   ? GRPC_MILLIS_INF_FUTURE
                                   : DEFAULT_MAX_CONNECTION_IDLE_MS;
  chand->idle_state = MAX_IDLE_STATE_INIT;
  gpr_atm_no_barrier_store(&chand->last_enter_idle_time_millis, GPR_ATM_MIN);
  for (size_t i = 0; i < args->channel_args->num_args; ++i) {
    if (0 == strcmp(args->channel_args->args[i].key,
                    GRPC_ARG_MAX_CONNECTION_AGE_MS)) {
      const int value = grpc_channel_arg_get_integer(
          &args->channel_args->args[i], MAX_CONNECTION_AGE_INTEGER_OPTIONS);
      chand->max_connection_age =
          add_random_max_connection_age_jitter_and_convert_to_grpc_millis(
              value);
    } else if (0 == strcmp(args->channel_args->args[i].key,
                           GRPC_ARG_MAX_CONNECTION_AGE_GRACE_MS)) {
      const int value = grpc_channel_arg_get_integer(
          &args->channel_args->args[i],
          {DEFAULT_MAX_CONNECTION_AGE_GRACE_MS, 0, INT_MAX});
      chand->max_connection_age_grace =
          value == INT_MAX ? GRPC_MILLIS_INF_FUTURE : value;
    } else if (0 == strcmp(args->channel_args->args[i].key,
                           GRPC_ARG_MAX_CONNECTION_IDLE_MS)) {
      const int value = grpc_channel_arg_get_integer(
          &args->channel_args->args[i], MAX_CONNECTION_IDLE_INTEGER_OPTIONS);
      chand->max_connection_idle =
          value == INT_MAX ? GRPC_MILLIS_INF_FUTURE : value;
    }
  }
  GRPC_CLOSURE_INIT(&chand->max_idle_timer_cb, max_idle_timer_cb, chand,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&chand->close_max_age_channel, close_max_age_channel, chand,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&chand->force_close_max_age_channel,
                    force_close_max_age_channel, chand,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&chand->start_max_idle_timer_after_init,
                    start_max_idle_timer_after_init, chand,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&chand->start_max_age_timer_after_init,
                    start_max_age_timer_after_init, chand,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&chand->start_max_age_grace_timer_after_goaway_op,
                    start_max_age_grace_timer_after_goaway_op, chand,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&chand->channel_connectivity_changed,
                    channel_connectivity_changed, chand,
                    grpc_schedule_on_exec_ctx);

  if (chand->max_connection_age != GRPC_MILLIS_INF_FUTURE) {
    /* When the channel reaches its max age, we send down an op with
       goaway_error set.  However, we can't send down any ops until after the
       channel stack is fully initialized.  If we start the timer here, we have
       no guarantee that the timer won't pop before channel stack initialization
       is finished.  To avoid that problem, we create a closure to start the
       timer, and we schedule that closure to be run after call stack
       initialization is done. */
    GRPC_CHANNEL_STACK_REF(chand->channel_stack,
                           "max_age start_max_age_timer_after_init");
    GRPC_CLOSURE_SCHED(&chand->start_max_age_timer_after_init, GRPC_ERROR_NONE);
  }

  /* Initialize the number of calls as 1, so that the max_idle_timer will not
     start until start_max_idle_timer_after_init is invoked. */
  gpr_atm_rel_store(&chand->call_count, 1);
  if (chand->max_connection_idle != GRPC_MILLIS_INF_FUTURE) {
    GRPC_CHANNEL_STACK_REF(chand->channel_stack,
                           "max_age start_max_idle_timer_after_init");
    GRPC_CLOSURE_SCHED(&chand->start_max_idle_timer_after_init,
                       GRPC_ERROR_NONE);
  }
  return GRPC_ERROR_NONE;
}

/* Destructor for channel_data. */
static void destroy_channel_elem(grpc_channel_element* elem) {}

const grpc_channel_filter grpc_max_age_filter = {
    grpc_call_next_op,
    grpc_channel_next_op,
    0, /* sizeof_call_data */
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_channel_next_get_info,
    "max_age"};

static bool maybe_add_max_age_filter(grpc_channel_stack_builder* builder,
                                     void* arg) {
  const grpc_channel_args* channel_args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  bool enable =
      grpc_channel_arg_get_integer(
          grpc_channel_args_find(channel_args, GRPC_ARG_MAX_CONNECTION_AGE_MS),
          MAX_CONNECTION_AGE_INTEGER_OPTIONS) != INT_MAX ||
      grpc_channel_arg_get_integer(
          grpc_channel_args_find(channel_args, GRPC_ARG_MAX_CONNECTION_IDLE_MS),
          MAX_CONNECTION_IDLE_INTEGER_OPTIONS) != INT_MAX;
  if (enable) {
    return grpc_channel_stack_builder_prepend_filter(
        builder, &grpc_max_age_filter, nullptr, nullptr);
  } else {
    return true;
  }
}

void grpc_max_age_filter_init(void) {
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_max_age_filter, nullptr);
}

void grpc_max_age_filter_shutdown(void) {}
