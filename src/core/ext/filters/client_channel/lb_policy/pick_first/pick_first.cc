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

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_index.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/connectivity_state.h"

grpc_core::TraceFlag grpc_lb_pick_first_trace(false, "pick_first");

typedef struct pending_pick {
  struct pending_pick* next;
  uint32_t initial_metadata_flags;
  grpc_connected_subchannel** target;
  grpc_closure* on_complete;
} pending_pick;

typedef struct {
  /** base policy: must be first */
  grpc_lb_policy base;
  /** all our subchannels */
  grpc_lb_subchannel_list* subchannel_list;
  /** latest pending subchannel list */
  grpc_lb_subchannel_list* latest_pending_subchannel_list;
  /** selected subchannel in \a subchannel_list */
  grpc_lb_subchannel_data* selected;
  /** have we started picking? */
  bool started_picking;
  /** are we shut down? */
  bool shutdown;
  /** list of picks that are waiting on connectivity */
  pending_pick* pending_picks;
  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;
} pick_first_lb_policy;

static void pf_destroy(grpc_exec_ctx* exec_ctx, grpc_lb_policy* pol) {
  pick_first_lb_policy* p = (pick_first_lb_policy*)pol;
  GPR_ASSERT(p->subchannel_list == nullptr);
  GPR_ASSERT(p->latest_pending_subchannel_list == nullptr);
  GPR_ASSERT(p->pending_picks == nullptr);
  grpc_connectivity_state_destroy(exec_ctx, &p->state_tracker);
  gpr_free(p);
  grpc_subchannel_index_unref();
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_DEBUG, "Pick First %p destroyed.", (void*)p);
  }
}

static void shutdown_locked(grpc_exec_ctx* exec_ctx, pick_first_lb_policy* p,
                            grpc_error* error) {
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_DEBUG, "Pick First %p Shutting down", p);
  }
  p->shutdown = true;
  pending_pick* pp;
  while ((pp = p->pending_picks) != nullptr) {
    p->pending_picks = pp->next;
    *pp->target = nullptr;
    GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete, GRPC_ERROR_REF(error));
    gpr_free(pp);
  }
  grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                              GRPC_CHANNEL_SHUTDOWN, GRPC_ERROR_REF(error),
                              "shutdown");
  if (p->subchannel_list != nullptr) {
    grpc_lb_subchannel_list_shutdown_and_unref(exec_ctx, p->subchannel_list,
                                               "pf_shutdown");
    p->subchannel_list = nullptr;
  }
  if (p->latest_pending_subchannel_list != nullptr) {
    grpc_lb_subchannel_list_shutdown_and_unref(
        exec_ctx, p->latest_pending_subchannel_list, "pf_shutdown");
    p->latest_pending_subchannel_list = nullptr;
  }
  GRPC_ERROR_UNREF(error);
}

static void pf_shutdown_locked(grpc_exec_ctx* exec_ctx, grpc_lb_policy* pol) {
  shutdown_locked(exec_ctx, (pick_first_lb_policy*)pol,
                  GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel shutdown"));
}

static void pf_cancel_pick_locked(grpc_exec_ctx* exec_ctx, grpc_lb_policy* pol,
                                  grpc_connected_subchannel** target,
                                  grpc_error* error) {
  pick_first_lb_policy* p = (pick_first_lb_policy*)pol;
  pending_pick* pp = p->pending_picks;
  p->pending_picks = nullptr;
  while (pp != nullptr) {
    pending_pick* next = pp->next;
    if (pp->target == target) {
      *target = nullptr;
      GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
      gpr_free(pp);
    } else {
      pp->next = p->pending_picks;
      p->pending_picks = pp;
    }
    pp = next;
  }
  GRPC_ERROR_UNREF(error);
}

static void pf_cancel_picks_locked(grpc_exec_ctx* exec_ctx, grpc_lb_policy* pol,
                                   uint32_t initial_metadata_flags_mask,
                                   uint32_t initial_metadata_flags_eq,
                                   grpc_error* error) {
  pick_first_lb_policy* p = (pick_first_lb_policy*)pol;
  pending_pick* pp = p->pending_picks;
  p->pending_picks = nullptr;
  while (pp != nullptr) {
    pending_pick* next = pp->next;
    if ((pp->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
      gpr_free(pp);
    } else {
      pp->next = p->pending_picks;
      p->pending_picks = pp;
    }
    pp = next;
  }
  GRPC_ERROR_UNREF(error);
}

static void start_picking_locked(grpc_exec_ctx* exec_ctx,
                                 pick_first_lb_policy* p) {
  p->started_picking = true;
  if (p->subchannel_list != nullptr &&
      p->subchannel_list->num_subchannels > 0) {
    p->subchannel_list->checking_subchannel = 0;
    grpc_lb_subchannel_list_ref_for_connectivity_watch(
        p->subchannel_list, "connectivity_watch+start_picking");
    grpc_lb_subchannel_data_start_connectivity_watch(
        exec_ctx, &p->subchannel_list->subchannels[0]);
  }
}

static void pf_exit_idle_locked(grpc_exec_ctx* exec_ctx, grpc_lb_policy* pol) {
  pick_first_lb_policy* p = (pick_first_lb_policy*)pol;
  if (!p->started_picking) {
    start_picking_locked(exec_ctx, p);
  }
}

static int pf_pick_locked(grpc_exec_ctx* exec_ctx, grpc_lb_policy* pol,
                          const grpc_lb_policy_pick_args* pick_args,
                          grpc_connected_subchannel** target,
                          grpc_call_context_element* context, void** user_data,
                          grpc_closure* on_complete) {
  pick_first_lb_policy* p = (pick_first_lb_policy*)pol;
  // If we have a selected subchannel already, return synchronously.
  if (p->selected != nullptr) {
    *target = GRPC_CONNECTED_SUBCHANNEL_REF(p->selected->connected_subchannel,
                                            "picked");
    return 1;
  }
  // No subchannel selected yet, so handle asynchronously.
  if (!p->started_picking) {
    start_picking_locked(exec_ctx, p);
  }
  pending_pick* pp = (pending_pick*)gpr_malloc(sizeof(*pp));
  pp->next = p->pending_picks;
  pp->target = target;
  pp->initial_metadata_flags = pick_args->initial_metadata_flags;
  pp->on_complete = on_complete;
  p->pending_picks = pp;
  return 0;
}

static void destroy_unselected_subchannels_locked(grpc_exec_ctx* exec_ctx,
                                                  pick_first_lb_policy* p) {
  for (size_t i = 0; i < p->subchannel_list->num_subchannels; ++i) {
    grpc_lb_subchannel_data* sd = &p->subchannel_list->subchannels[i];
    if (p->selected != sd) {
      grpc_lb_subchannel_data_unref_subchannel(exec_ctx, sd,
                                               "selected_different_subchannel");
    }
  }
}

static grpc_connectivity_state pf_check_connectivity_locked(
    grpc_exec_ctx* exec_ctx, grpc_lb_policy* pol, grpc_error** error) {
  pick_first_lb_policy* p = (pick_first_lb_policy*)pol;
  return grpc_connectivity_state_get(&p->state_tracker, error);
}

static void pf_notify_on_state_change_locked(grpc_exec_ctx* exec_ctx,
                                             grpc_lb_policy* pol,
                                             grpc_connectivity_state* current,
                                             grpc_closure* notify) {
  pick_first_lb_policy* p = (pick_first_lb_policy*)pol;
  grpc_connectivity_state_notify_on_state_change(exec_ctx, &p->state_tracker,
                                                 current, notify);
}

static void pf_ping_one_locked(grpc_exec_ctx* exec_ctx, grpc_lb_policy* pol,
                               grpc_closure* closure) {
  pick_first_lb_policy* p = (pick_first_lb_policy*)pol;
  if (p->selected) {
    grpc_connected_subchannel_ping(exec_ctx, p->selected->connected_subchannel,
                                   closure);
  } else {
    GRPC_CLOSURE_SCHED(exec_ctx, closure,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Not connected"));
  }
}

static void pf_connectivity_changed_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                           grpc_error* error);

static void pf_update_locked(grpc_exec_ctx* exec_ctx, grpc_lb_policy* policy,
                             const grpc_lb_policy_args* args) {
  pick_first_lb_policy* p = (pick_first_lb_policy*)policy;
  const grpc_arg* arg =
      grpc_channel_args_find(args->args, GRPC_ARG_LB_ADDRESSES);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) {
    if (p->subchannel_list == nullptr) {
      // If we don't have a current subchannel list, go into TRANSIENT FAILURE.
      grpc_connectivity_state_set(
          exec_ctx, &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing update in args"),
          "pf_update_missing");
    } else {
      // otherwise, keep using the current subchannel list (ignore this update).
      gpr_log(GPR_ERROR,
              "No valid LB addresses channel arg for Pick First %p update, "
              "ignoring.",
              (void*)p);
    }
    return;
  }
  const grpc_lb_addresses* addresses =
      (const grpc_lb_addresses*)arg->value.pointer.p;
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_INFO, "Pick First %p received update with %lu addresses",
            (void*)p, (unsigned long)addresses->num_addresses);
  }
  grpc_lb_subchannel_list* subchannel_list = grpc_lb_subchannel_list_create(
      exec_ctx, &p->base, &grpc_lb_pick_first_trace, addresses, args,
      pf_connectivity_changed_locked);
  if (subchannel_list->num_subchannels == 0) {
    // Empty update or no valid subchannels. Unsubscribe from all current
    // subchannels and put the channel in TRANSIENT_FAILURE.
    grpc_connectivity_state_set(
        exec_ctx, &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty update"),
        "pf_update_empty");
    if (p->subchannel_list != nullptr) {
      grpc_lb_subchannel_list_shutdown_and_unref(exec_ctx, p->subchannel_list,
                                                 "sl_shutdown_empty_update");
    }
    p->subchannel_list = subchannel_list;  // Empty list.
    p->selected = nullptr;
    return;
  }
  if (p->selected == nullptr) {
    // We don't yet have a selected subchannel, so replace the current
    // subchannel list immediately.
    if (p->subchannel_list != nullptr) {
      grpc_lb_subchannel_list_shutdown_and_unref(exec_ctx, p->subchannel_list,
                                                 "pf_update_before_selected");
    }
    p->subchannel_list = subchannel_list;
  } else {
    // We do have a selected subchannel.
    // Check if it's present in the new list.  If so, we're done.
    for (size_t i = 0; i < subchannel_list->num_subchannels; ++i) {
      grpc_lb_subchannel_data* sd = &subchannel_list->subchannels[i];
      if (sd->subchannel == p->selected->subchannel) {
        // The currently selected subchannel is in the update: we are done.
        if (grpc_lb_pick_first_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "Pick First %p found already selected subchannel %p "
                  "at update index %" PRIuPTR " of %" PRIuPTR "; update done",
                  p, p->selected->subchannel, i,
                  subchannel_list->num_subchannels);
        }
        if (p->selected->connected_subchannel != nullptr) {
          sd->connected_subchannel = GRPC_CONNECTED_SUBCHANNEL_REF(
              p->selected->connected_subchannel, "pf_update_includes_selected");
        }
        p->selected = sd;
        if (p->subchannel_list != nullptr) {
          grpc_lb_subchannel_list_shutdown_and_unref(
              exec_ctx, p->subchannel_list, "pf_update_includes_selected");
        }
        p->subchannel_list = subchannel_list;
        destroy_unselected_subchannels_locked(exec_ctx, p);
        grpc_lb_subchannel_list_ref_for_connectivity_watch(
            subchannel_list, "connectivity_watch+replace_selected");
        grpc_lb_subchannel_data_start_connectivity_watch(exec_ctx, sd);
        // If there was a previously pending update (which may or may
        // not have contained the currently selected subchannel), drop
        // it, so that it doesn't override what we've done here.
        if (p->latest_pending_subchannel_list != nullptr) {
          grpc_lb_subchannel_list_shutdown_and_unref(
              exec_ctx, p->latest_pending_subchannel_list,
              "pf_update_includes_selected+outdated");
          p->latest_pending_subchannel_list = nullptr;
        }
        return;
      }
    }
    // Not keeping the previous selected subchannel, so set the latest
    // pending subchannel list to the new subchannel list.  We will wait
    // for it to report READY before swapping it into the current
    // subchannel list.
    if (p->latest_pending_subchannel_list != nullptr) {
      if (grpc_lb_pick_first_trace.enabled()) {
        gpr_log(GPR_DEBUG,
                "Pick First %p Shutting down latest pending subchannel list "
                "%p, about to be replaced by newer latest %p",
                (void*)p, (void*)p->latest_pending_subchannel_list,
                (void*)subchannel_list);
      }
      grpc_lb_subchannel_list_shutdown_and_unref(
          exec_ctx, p->latest_pending_subchannel_list,
          "sl_outdated_dont_smash");
    }
    p->latest_pending_subchannel_list = subchannel_list;
  }
  // If we've started picking, start trying to connect to the first
  // subchannel in the new list.
  if (p->started_picking) {
    grpc_lb_subchannel_list_ref_for_connectivity_watch(
        subchannel_list, "connectivity_watch+update");
    grpc_lb_subchannel_data_start_connectivity_watch(
        exec_ctx, &subchannel_list->subchannels[0]);
  }
}

static void pf_connectivity_changed_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                           grpc_error* error) {
  grpc_lb_subchannel_data* sd = (grpc_lb_subchannel_data*)arg;
  pick_first_lb_policy* p = (pick_first_lb_policy*)sd->subchannel_list->policy;
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "Pick First %p connectivity changed for subchannel %p (%" PRIuPTR
            " of %" PRIuPTR
            "), subchannel_list %p: state=%s p->shutdown=%d "
            "sd->subchannel_list->shutting_down=%d error=%s",
            (void*)p, (void*)sd->subchannel,
            sd->subchannel_list->checking_subchannel,
            sd->subchannel_list->num_subchannels, (void*)sd->subchannel_list,
            grpc_connectivity_state_name(sd->pending_connectivity_state_unsafe),
            p->shutdown, sd->subchannel_list->shutting_down,
            grpc_error_string(error));
  }
  // If the policy is shutting down, unref and return.
  if (p->shutdown) {
    grpc_lb_subchannel_data_stop_connectivity_watch(exec_ctx, sd);
    grpc_lb_subchannel_data_unref_subchannel(exec_ctx, sd, "pf_shutdown");
    grpc_lb_subchannel_list_unref_for_connectivity_watch(
        exec_ctx, sd->subchannel_list, "pf_shutdown");
    return;
  }
  // If the subchannel list is shutting down, stop watching.
  if (sd->subchannel_list->shutting_down || error == GRPC_ERROR_CANCELLED) {
    grpc_lb_subchannel_data_stop_connectivity_watch(exec_ctx, sd);
    grpc_lb_subchannel_data_unref_subchannel(exec_ctx, sd, "pf_sl_shutdown");
    grpc_lb_subchannel_list_unref_for_connectivity_watch(
        exec_ctx, sd->subchannel_list, "pf_sl_shutdown");
    return;
  }
  // If we're still here, the notification must be for a subchannel in
  // either the current or latest pending subchannel lists.
  GPR_ASSERT(sd->subchannel_list == p->subchannel_list ||
             sd->subchannel_list == p->latest_pending_subchannel_list);
  // Update state.
  sd->curr_connectivity_state = sd->pending_connectivity_state_unsafe;
  // Handle updates for the currently selected subchannel.
  if (p->selected == sd) {
    // If the new state is anything other than READY and there is a
    // pending update, switch to the pending update.
    if (sd->curr_connectivity_state != GRPC_CHANNEL_READY &&
        p->latest_pending_subchannel_list != nullptr) {
      p->selected = nullptr;
      grpc_lb_subchannel_list_shutdown_and_unref(
          exec_ctx, p->subchannel_list, "selected_not_ready+switch_to_update");
      p->subchannel_list = p->latest_pending_subchannel_list;
      p->latest_pending_subchannel_list = nullptr;
      grpc_connectivity_state_set(
          exec_ctx, &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_REF(error), "selected_not_ready+switch_to_update");
    } else {
      if (sd->curr_connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        /* if the selected channel goes bad, we're done */
        sd->curr_connectivity_state = GRPC_CHANNEL_SHUTDOWN;
      }
      grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                  sd->curr_connectivity_state,
                                  GRPC_ERROR_REF(error), "selected_changed");
      if (sd->curr_connectivity_state != GRPC_CHANNEL_SHUTDOWN) {
        // Renew notification.
        grpc_lb_subchannel_data_start_connectivity_watch(exec_ctx, sd);
      } else {
        grpc_lb_subchannel_data_stop_connectivity_watch(exec_ctx, sd);
        grpc_lb_subchannel_list_unref_for_connectivity_watch(
            exec_ctx, sd->subchannel_list, "pf_selected_shutdown");
        shutdown_locked(exec_ctx, p, GRPC_ERROR_REF(error));
      }
    }
    return;
  }
  // If we get here, there are two possible cases:
  // 1. We do not currently have a selected subchannel, and the update is
  //    for a subchannel in p->subchannel_list that we're trying to
  //    connect to.  The goal here is to find a subchannel that we can
  //    select.
  // 2. We do currently have a selected subchannel, and the update is
  //    for a subchannel in p->latest_pending_subchannel_list.  The
  //    goal here is to find a subchannel from the update that we can
  //    select in place of the current one.
  switch (sd->curr_connectivity_state) {
    case GRPC_CHANNEL_READY: {
      // Case 2.  Promote p->latest_pending_subchannel_list to
      // p->subchannel_list.
      if (sd->subchannel_list == p->latest_pending_subchannel_list) {
        GPR_ASSERT(p->subchannel_list != nullptr);
        grpc_lb_subchannel_list_shutdown_and_unref(exec_ctx, p->subchannel_list,
                                                   "finish_update");
        p->subchannel_list = p->latest_pending_subchannel_list;
        p->latest_pending_subchannel_list = nullptr;
      }
      // Cases 1 and 2.
      grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                  GRPC_CHANNEL_READY, GRPC_ERROR_NONE,
                                  "connecting_ready");
      sd->connected_subchannel = GRPC_CONNECTED_SUBCHANNEL_REF(
          grpc_subchannel_get_connected_subchannel(sd->subchannel),
          "connected");
      p->selected = sd;
      if (grpc_lb_pick_first_trace.enabled()) {
        gpr_log(GPR_INFO, "Pick First %p selected subchannel %p", (void*)p,
                (void*)sd->subchannel);
      }
      // Drop all other subchannels, since we are now connected.
      destroy_unselected_subchannels_locked(exec_ctx, p);
      // Update any calls that were waiting for a pick.
      pending_pick* pp;
      while ((pp = p->pending_picks)) {
        p->pending_picks = pp->next;
        *pp->target = GRPC_CONNECTED_SUBCHANNEL_REF(
            p->selected->connected_subchannel, "picked");
        if (grpc_lb_pick_first_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "Servicing pending pick with selected subchannel %p",
                  (void*)p->selected);
        }
        GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete, GRPC_ERROR_NONE);
        gpr_free(pp);
      }
      // Renew notification.
      grpc_lb_subchannel_data_start_connectivity_watch(exec_ctx, sd);
      break;
    }
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      grpc_lb_subchannel_data_stop_connectivity_watch(exec_ctx, sd);
      do {
        sd->subchannel_list->checking_subchannel =
            (sd->subchannel_list->checking_subchannel + 1) %
            sd->subchannel_list->num_subchannels;
        sd = &sd->subchannel_list
                  ->subchannels[sd->subchannel_list->checking_subchannel];
      } while (sd->subchannel == nullptr);
      // Case 1: Only set state to TRANSIENT_FAILURE if we've tried
      // all subchannels.
      if (sd->subchannel_list->checking_subchannel == 0 &&
          sd->subchannel_list == p->subchannel_list) {
        grpc_connectivity_state_set(
            exec_ctx, &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
            GRPC_ERROR_REF(error), "connecting_transient_failure");
      }
      // Reuses the connectivity refs from the previous watch.
      grpc_lb_subchannel_data_start_connectivity_watch(exec_ctx, sd);
      break;
    }
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE: {
      // Only update connectivity state in case 1.
      if (sd->subchannel_list == p->subchannel_list) {
        grpc_connectivity_state_set(
            exec_ctx, &p->state_tracker, GRPC_CHANNEL_CONNECTING,
            GRPC_ERROR_REF(error), "connecting_changed");
      }
      // Renew notification.
      grpc_lb_subchannel_data_start_connectivity_watch(exec_ctx, sd);
      break;
    }
    case GRPC_CHANNEL_SHUTDOWN: {
      grpc_lb_subchannel_data_stop_connectivity_watch(exec_ctx, sd);
      grpc_lb_subchannel_data_unref_subchannel(exec_ctx, sd,
                                               "pf_candidate_shutdown");
      // Advance to next subchannel and check its state.
      grpc_lb_subchannel_data* original_sd = sd;
      do {
        sd->subchannel_list->checking_subchannel =
            (sd->subchannel_list->checking_subchannel + 1) %
            sd->subchannel_list->num_subchannels;
        sd = &sd->subchannel_list
                  ->subchannels[sd->subchannel_list->checking_subchannel];
      } while (sd->subchannel == nullptr && sd != original_sd);
      if (sd == original_sd) {
        grpc_lb_subchannel_list_unref_for_connectivity_watch(
            exec_ctx, sd->subchannel_list, "pf_candidate_shutdown");
        shutdown_locked(exec_ctx, p,
                        GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                            "Pick first exhausted channels", &error, 1));
        break;
      }
      if (sd->subchannel_list == p->subchannel_list) {
        grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                    GRPC_CHANNEL_TRANSIENT_FAILURE,
                                    GRPC_ERROR_REF(error), "subchannel_failed");
      }
      // Reuses the connectivity refs from the previous watch.
      grpc_lb_subchannel_data_start_connectivity_watch(exec_ctx, sd);
      break;
    }
  }
}

static const grpc_lb_policy_vtable pick_first_lb_policy_vtable = {
    pf_destroy,
    pf_shutdown_locked,
    pf_pick_locked,
    pf_cancel_pick_locked,
    pf_cancel_picks_locked,
    pf_ping_one_locked,
    pf_exit_idle_locked,
    pf_check_connectivity_locked,
    pf_notify_on_state_change_locked,
    pf_update_locked};

static void pick_first_factory_ref(grpc_lb_policy_factory* factory) {}

static void pick_first_factory_unref(grpc_lb_policy_factory* factory) {}

static grpc_lb_policy* create_pick_first(grpc_exec_ctx* exec_ctx,
                                         grpc_lb_policy_factory* factory,
                                         grpc_lb_policy_args* args) {
  GPR_ASSERT(args->client_channel_factory != nullptr);
  pick_first_lb_policy* p = (pick_first_lb_policy*)gpr_zalloc(sizeof(*p));
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_DEBUG, "Pick First %p created.", (void*)p);
  }
  pf_update_locked(exec_ctx, &p->base, args);
  grpc_lb_policy_init(&p->base, &pick_first_lb_policy_vtable, args->combiner);
  grpc_subchannel_index_ref();
  return &p->base;
}

static const grpc_lb_policy_factory_vtable pick_first_factory_vtable = {
    pick_first_factory_ref, pick_first_factory_unref, create_pick_first,
    "pick_first"};

static grpc_lb_policy_factory pick_first_lb_policy_factory = {
    &pick_first_factory_vtable};

static grpc_lb_policy_factory* pick_first_lb_factory_create() {
  return &pick_first_lb_policy_factory;
}

/* Plugin registration */

extern "C" void grpc_lb_policy_pick_first_init() {
  grpc_register_lb_policy(pick_first_lb_factory_create());
}

extern "C" void grpc_lb_policy_pick_first_shutdown() {}
