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
  (grpc_integer_options) { DEFAULT_MAX_CONNECTION_AGE_MS, 1, INT_MAX }
#define MAX_CONNECTION_IDLE_INTEGER_OPTIONS \
  (grpc_integer_options) { DEFAULT_MAX_CONNECTION_IDLE_MS, 1, INT_MAX }

typedef struct channel_data {
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
  gpr_timespec max_connection_idle;
  /* Allowed max time a channel may exist */
  gpr_timespec max_connection_age;
  /* Allowed grace period after the channel reaches its max age */
  gpr_timespec max_connection_age_grace;
  /* Closure to run when the channel's idle duration reaches max_connection_idle
     and should be closed gracefully */
  grpc_closure close_max_idle_channel;
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
} channel_data;

/* Increase the nubmer of active calls. Before the increasement, if there are no
   calls, the max_idle_timer should be cancelled. */
static void increase_call_count(grpc_exec_ctx* exec_ctx, channel_data* chand) {
  if (gpr_atm_full_fetch_add(&chand->call_count, 1) == 0) {
    grpc_timer_cancel(exec_ctx, &chand->max_idle_timer);
  }
}

/* Decrease the nubmer of active calls. After the decrement, if there are no
   calls, the max_idle_timer should be started. */
static void decrease_call_count(grpc_exec_ctx* exec_ctx, channel_data* chand) {
  if (gpr_atm_full_fetch_add(&chand->call_count, -1) == 1) {
    GRPC_CHANNEL_STACK_REF(chand->channel_stack, "max_age max_idle_timer");
    grpc_timer_init(
        exec_ctx, &chand->max_idle_timer,
        gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), chand->max_connection_idle),
        &chand->close_max_idle_channel, gpr_now(GPR_CLOCK_MONOTONIC));
  }
}

static void start_max_idle_timer_after_init(grpc_exec_ctx* exec_ctx, void* arg,
                                            grpc_error* error) {
  channel_data* chand = (channel_data*)arg;
  /* Decrease call_count. If there are no active calls at this time,
     max_idle_timer will start here. If the number of active calls is not 0,
     max_idle_timer will start after all the active calls end. */
  decrease_call_count(exec_ctx, chand);
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, chand->channel_stack,
                           "max_age start_max_idle_timer_after_init");
}

static void start_max_age_timer_after_init(grpc_exec_ctx* exec_ctx, void* arg,
                                           grpc_error* error) {
  channel_data* chand = (channel_data*)arg;
  gpr_mu_lock(&chand->max_age_timer_mu);
  chand->max_age_timer_pending = true;
  GRPC_CHANNEL_STACK_REF(chand->channel_stack, "max_age max_age_timer");
  grpc_timer_init(
      exec_ctx, &chand->max_age_timer,
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), chand->max_connection_age),
      &chand->close_max_age_channel, gpr_now(GPR_CLOCK_MONOTONIC));
  gpr_mu_unlock(&chand->max_age_timer_mu);
  grpc_transport_op* op = grpc_make_transport_op(NULL);
  op->on_connectivity_state_change = &chand->channel_connectivity_changed,
  op->connectivity_state = &chand->connectivity_state;
  grpc_channel_next_op(exec_ctx,
                       grpc_channel_stack_element(chand->channel_stack, 0), op);
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, chand->channel_stack,
                           "max_age start_max_age_timer_after_init");
}

static void start_max_age_grace_timer_after_goaway_op(grpc_exec_ctx* exec_ctx,
                                                      void* arg,
                                                      grpc_error* error) {
  channel_data* chand = (channel_data*)arg;
  gpr_mu_lock(&chand->max_age_timer_mu);
  chand->max_age_grace_timer_pending = true;
  GRPC_CHANNEL_STACK_REF(chand->channel_stack, "max_age max_age_grace_timer");
  grpc_timer_init(exec_ctx, &chand->max_age_grace_timer,
                  gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               chand->max_connection_age_grace),
                  &chand->force_close_max_age_channel,
                  gpr_now(GPR_CLOCK_MONOTONIC));
  gpr_mu_unlock(&chand->max_age_timer_mu);
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, chand->channel_stack,
                           "max_age start_max_age_grace_timer_after_goaway_op");
}

static void close_max_idle_channel(grpc_exec_ctx* exec_ctx, void* arg,
                                   grpc_error* error) {
  channel_data* chand = (channel_data*)arg;
  if (error == GRPC_ERROR_NONE) {
    /* Prevent the max idle timer from being set again */
    gpr_atm_no_barrier_fetch_add(&chand->call_count, 1);
    grpc_transport_op* op = grpc_make_transport_op(NULL);
    op->goaway_error =
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("max_idle"),
                           GRPC_ERROR_INT_HTTP2_ERROR, GRPC_HTTP2_NO_ERROR);
    grpc_channel_element* elem =
        grpc_channel_stack_element(chand->channel_stack, 0);
    elem->filter->start_transport_op(exec_ctx, elem, op);
  } else if (error != GRPC_ERROR_CANCELLED) {
    GRPC_LOG_IF_ERROR("close_max_idle_channel", error);
  }
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, chand->channel_stack,
                           "max_age max_idle_timer");
}

static void close_max_age_channel(grpc_exec_ctx* exec_ctx, void* arg,
                                  grpc_error* error) {
  channel_data* chand = (channel_data*)arg;
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
    elem->filter->start_transport_op(exec_ctx, elem, op);
  } else if (error != GRPC_ERROR_CANCELLED) {
    GRPC_LOG_IF_ERROR("close_max_age_channel", error);
  }
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, chand->channel_stack,
                           "max_age max_age_timer");
}

static void force_close_max_age_channel(grpc_exec_ctx* exec_ctx, void* arg,
                                        grpc_error* error) {
  channel_data* chand = (channel_data*)arg;
  gpr_mu_lock(&chand->max_age_timer_mu);
  chand->max_age_grace_timer_pending = false;
  gpr_mu_unlock(&chand->max_age_timer_mu);
  if (error == GRPC_ERROR_NONE) {
    grpc_transport_op* op = grpc_make_transport_op(NULL);
    op->disconnect_with_error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel reaches max age");
    grpc_channel_element* elem =
        grpc_channel_stack_element(chand->channel_stack, 0);
    elem->filter->start_transport_op(exec_ctx, elem, op);
  } else if (error != GRPC_ERROR_CANCELLED) {
    GRPC_LOG_IF_ERROR("force_close_max_age_channel", error);
  }
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, chand->channel_stack,
                           "max_age max_age_grace_timer");
}

static void channel_connectivity_changed(grpc_exec_ctx* exec_ctx, void* arg,
                                         grpc_error* error) {
  channel_data* chand = (channel_data*)arg;
  if (chand->connectivity_state != GRPC_CHANNEL_SHUTDOWN) {
    grpc_transport_op* op = grpc_make_transport_op(NULL);
    op->on_connectivity_state_change = &chand->channel_connectivity_changed,
    op->connectivity_state = &chand->connectivity_state;
    grpc_channel_next_op(
        exec_ctx, grpc_channel_stack_element(chand->channel_stack, 0), op);
  } else {
    gpr_mu_lock(&chand->max_age_timer_mu);
    if (chand->max_age_timer_pending) {
      grpc_timer_cancel(exec_ctx, &chand->max_age_timer);
      chand->max_age_timer_pending = false;
    }
    if (chand->max_age_grace_timer_pending) {
      grpc_timer_cancel(exec_ctx, &chand->max_age_grace_timer);
      chand->max_age_grace_timer_pending = false;
    }
    gpr_mu_unlock(&chand->max_age_timer_mu);
    /* If there are no active calls, this increasement will cancel
       max_idle_timer, and prevent max_idle_timer from being started in the
       future. */
    increase_call_count(exec_ctx, chand);
  }
}

/* A random jitter of +/-10% will be added to MAX_CONNECTION_AGE to spread out
   connection storms. Note that the MAX_CONNECTION_AGE option without jitter
   would not create connection storms by itself, but if there happened to be a
   connection storm it could cause it to repeat at a fixed period. */
static int add_random_max_connection_age_jitter(int value) {
  /* generate a random number between 1 - MAX_CONNECTION_AGE_JITTER and
     1 + MAX_CONNECTION_AGE_JITTER */
  double multiplier = rand() * MAX_CONNECTION_AGE_JITTER * 2.0 / RAND_MAX +
                      1.0 - MAX_CONNECTION_AGE_JITTER;
  double result = multiplier * value;
  /* INT_MAX - 0.5 converts the value to float, so that result will not be
     cast to int implicitly before the comparison. */
  return result > INT_MAX - 0.5 ? INT_MAX : (int)result;
}

/* Constructor for call_data. */
static grpc_error* init_call_elem(grpc_exec_ctx* exec_ctx,
                                  grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  channel_data* chand = (channel_data*)elem->channel_data;
  increase_call_count(exec_ctx, chand);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data. */
static void destroy_call_elem(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {
  channel_data* chand = (channel_data*)elem->channel_data;
  decrease_call_count(exec_ctx, chand);
}

/* Constructor for channel_data. */
static grpc_error* init_channel_elem(grpc_exec_ctx* exec_ctx,
                                     grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  channel_data* chand = (channel_data*)elem->channel_data;
  gpr_mu_init(&chand->max_age_timer_mu);
  chand->max_age_timer_pending = false;
  chand->max_age_grace_timer_pending = false;
  chand->channel_stack = args->channel_stack;
  chand->max_connection_age =
      DEFAULT_MAX_CONNECTION_AGE_MS == INT_MAX
          ? gpr_inf_future(GPR_TIMESPAN)
          : gpr_time_from_millis(add_random_max_connection_age_jitter(
                                     DEFAULT_MAX_CONNECTION_AGE_MS),
                                 GPR_TIMESPAN);
  chand->max_connection_age_grace =
      DEFAULT_MAX_CONNECTION_AGE_GRACE_MS == INT_MAX
          ? gpr_inf_future(GPR_TIMESPAN)
          : gpr_time_from_millis(DEFAULT_MAX_CONNECTION_AGE_GRACE_MS,
                                 GPR_TIMESPAN);
  chand->max_connection_idle =
      DEFAULT_MAX_CONNECTION_IDLE_MS == INT_MAX
          ? gpr_inf_future(GPR_TIMESPAN)
          : gpr_time_from_millis(DEFAULT_MAX_CONNECTION_IDLE_MS, GPR_TIMESPAN);
  for (size_t i = 0; i < args->channel_args->num_args; ++i) {
    if (0 == strcmp(args->channel_args->args[i].key,
                    GRPC_ARG_MAX_CONNECTION_AGE_MS)) {
      const int value = grpc_channel_arg_get_integer(
          &args->channel_args->args[i], MAX_CONNECTION_AGE_INTEGER_OPTIONS);
      chand->max_connection_age =
          value == INT_MAX
              ? gpr_inf_future(GPR_TIMESPAN)
              : gpr_time_from_millis(
                    add_random_max_connection_age_jitter(value), GPR_TIMESPAN);
    } else if (0 == strcmp(args->channel_args->args[i].key,
                           GRPC_ARG_MAX_CONNECTION_AGE_GRACE_MS)) {
      const int value = grpc_channel_arg_get_integer(
          &args->channel_args->args[i],
          (grpc_integer_options){DEFAULT_MAX_CONNECTION_AGE_GRACE_MS, 0,
                                 INT_MAX});
      chand->max_connection_age_grace =
          value == INT_MAX ? gpr_inf_future(GPR_TIMESPAN)
                           : gpr_time_from_millis(value, GPR_TIMESPAN);
    } else if (0 == strcmp(args->channel_args->args[i].key,
                           GRPC_ARG_MAX_CONNECTION_IDLE_MS)) {
      const int value = grpc_channel_arg_get_integer(
          &args->channel_args->args[i], MAX_CONNECTION_IDLE_INTEGER_OPTIONS);
      chand->max_connection_idle =
          value == INT_MAX ? gpr_inf_future(GPR_TIMESPAN)
                           : gpr_time_from_millis(value, GPR_TIMESPAN);
    }
  }
  GRPC_CLOSURE_INIT(&chand->close_max_idle_channel, close_max_idle_channel,
                    chand, grpc_schedule_on_exec_ctx);
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

  if (gpr_time_cmp(chand->max_connection_age, gpr_inf_future(GPR_TIMESPAN)) !=
      0) {
    /* When the channel reaches its max age, we send down an op with
       goaway_error set.  However, we can't send down any ops until after the
       channel stack is fully initialized.  If we start the timer here, we have
       no guarantee that the timer won't pop before channel stack initialization
       is finished.  To avoid that problem, we create a closure to start the
       timer, and we schedule that closure to be run after call stack
       initialization is done. */
    GRPC_CHANNEL_STACK_REF(chand->channel_stack,
                           "max_age start_max_age_timer_after_init");
    GRPC_CLOSURE_SCHED(exec_ctx, &chand->start_max_age_timer_after_init,
                       GRPC_ERROR_NONE);
  }

  /* Initialize the number of calls as 1, so that the max_idle_timer will not
     start until start_max_idle_timer_after_init is invoked. */
  gpr_atm_rel_store(&chand->call_count, 1);
  if (gpr_time_cmp(chand->max_connection_idle, gpr_inf_future(GPR_TIMESPAN)) !=
      0) {
    GRPC_CHANNEL_STACK_REF(chand->channel_stack,
                           "max_age start_max_idle_timer_after_init");
    GRPC_CLOSURE_SCHED(exec_ctx, &chand->start_max_idle_timer_after_init,
                       GRPC_ERROR_NONE);
  }
  return GRPC_ERROR_NONE;
}

/* Destructor for channel_data. */
static void destroy_channel_elem(grpc_exec_ctx* exec_ctx,
                                 grpc_channel_element* elem) {}

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

static bool maybe_add_max_age_filter(grpc_exec_ctx* exec_ctx,
                                     grpc_channel_stack_builder* builder,
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
        builder, &grpc_max_age_filter, NULL, NULL);
  } else {
    return true;
  }
}

void grpc_max_age_filter_init(void) {
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_max_age_filter, NULL);
}

void grpc_max_age_filter_shutdown(void) {}
