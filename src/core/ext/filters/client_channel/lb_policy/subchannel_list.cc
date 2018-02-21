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
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/connectivity_state.h"

void grpc_lb_subchannel_data_unref_subchannel(grpc_lb_subchannel_data* sd,
                                              const char* reason) {
  if (sd->subchannel != nullptr) {
    if (sd->subchannel_list->tracer->enabled()) {
      gpr_log(GPR_DEBUG,
              "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
              " (subchannel %p): unreffing subchannel",
              sd->subchannel_list->tracer->name(), sd->subchannel_list->policy,
              sd->subchannel_list,
              static_cast<size_t>(sd - sd->subchannel_list->subchannels),
              sd->subchannel_list->num_subchannels, sd->subchannel);
    }
    GRPC_SUBCHANNEL_UNREF(sd->subchannel, reason);
    sd->subchannel = nullptr;
    sd->connected_subchannel.reset();
    if (sd->user_data != nullptr) {
      GPR_ASSERT(sd->user_data_vtable != nullptr);
      sd->user_data_vtable->destroy(sd->user_data);
      sd->user_data = nullptr;
    }
  }
}

void grpc_lb_subchannel_data_start_connectivity_watch(
    grpc_lb_subchannel_data* sd) {
  if (sd->subchannel_list->tracer->enabled()) {
    gpr_log(
        GPR_DEBUG,
        "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
        " (subchannel %p): requesting connectivity change "
        "notification (from %s)",
        sd->subchannel_list->tracer->name(), sd->subchannel_list->policy,
        sd->subchannel_list,
        static_cast<size_t>(sd - sd->subchannel_list->subchannels),
        sd->subchannel_list->num_subchannels, sd->subchannel,
        grpc_connectivity_state_name(sd->pending_connectivity_state_unsafe));
  }
  sd->connectivity_notification_pending = true;
  grpc_subchannel_notify_on_state_change(
      sd->subchannel, sd->subchannel_list->policy->interested_parties(),
      &sd->pending_connectivity_state_unsafe,
      &sd->connectivity_changed_closure);
}

void grpc_lb_subchannel_data_stop_connectivity_watch(
    grpc_lb_subchannel_data* sd) {
  if (sd->subchannel_list->tracer->enabled()) {
    gpr_log(GPR_DEBUG,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): stopping connectivity watch",
            sd->subchannel_list->tracer->name(), sd->subchannel_list->policy,
            sd->subchannel_list,
            static_cast<size_t>(sd - sd->subchannel_list->subchannels),
            sd->subchannel_list->num_subchannels, sd->subchannel);
  }
  GPR_ASSERT(sd->connectivity_notification_pending);
  sd->connectivity_notification_pending = false;
}

grpc_lb_subchannel_list* grpc_lb_subchannel_list_create(
    grpc_core::LoadBalancingPolicy* p, grpc_core::TraceFlag* tracer,
    const grpc_lb_addresses* addresses, grpc_combiner* combiner,
    grpc_client_channel_factory* client_channel_factory,
    const grpc_channel_args& args, grpc_iomgr_cb_func connectivity_changed_cb) {
  grpc_lb_subchannel_list* subchannel_list =
      static_cast<grpc_lb_subchannel_list*>(
          gpr_zalloc(sizeof(*subchannel_list)));
  if (tracer->enabled()) {
    gpr_log(GPR_DEBUG,
            "[%s %p] Creating subchannel list %p for %" PRIuPTR " subchannels",
            tracer->name(), p, subchannel_list, addresses->num_addresses);
  }
  subchannel_list->policy = p;
  subchannel_list->tracer = tracer;
  gpr_ref_init(&subchannel_list->refcount, 1);
  subchannel_list->subchannels = static_cast<grpc_lb_subchannel_data*>(
      gpr_zalloc(sizeof(grpc_lb_subchannel_data) * addresses->num_addresses));
  // We need to remove the LB addresses in order to be able to compare the
  // subchannel keys of subchannels from a different batch of addresses.
  static const char* keys_to_remove[] = {GRPC_ARG_SUBCHANNEL_ADDRESS,
                                         GRPC_ARG_LB_ADDRESSES};
  // Create a subchannel for each address.
  grpc_subchannel_args sc_args;
  size_t subchannel_index = 0;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    // If there were any balancer, we would have chosen grpclb policy instead.
    GPR_ASSERT(!addresses->addresses[i].is_balancer);
    memset(&sc_args, 0, sizeof(grpc_subchannel_args));
    grpc_arg addr_arg =
        grpc_create_subchannel_address_arg(&addresses->addresses[i].address);
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
        &args, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), &addr_arg, 1);
    gpr_free(addr_arg.value.string);
    sc_args.args = new_args;
    grpc_subchannel* subchannel = grpc_client_channel_factory_create_subchannel(
        client_channel_factory, &sc_args);
    grpc_channel_args_destroy(new_args);
    if (subchannel == nullptr) {
      // Subchannel could not be created.
      if (tracer->enabled()) {
        char* address_uri =
            grpc_sockaddr_to_uri(&addresses->addresses[i].address);
        gpr_log(GPR_DEBUG,
                "[%s %p] could not create subchannel for address uri %s, "
                "ignoring",
                tracer->name(), subchannel_list->policy, address_uri);
        gpr_free(address_uri);
      }
      continue;
    }
    if (tracer->enabled()) {
      char* address_uri =
          grpc_sockaddr_to_uri(&addresses->addresses[i].address);
      gpr_log(GPR_DEBUG,
              "[%s %p] subchannel list %p index %" PRIuPTR
              ": Created subchannel %p for address uri %s",
              tracer->name(), p, subchannel_list, subchannel_index, subchannel,
              address_uri);
      gpr_free(address_uri);
    }
    grpc_lb_subchannel_data* sd =
        &subchannel_list->subchannels[subchannel_index++];
    sd->subchannel_list = subchannel_list;
    sd->subchannel = subchannel;
    GRPC_CLOSURE_INIT(&sd->connectivity_changed_closure,
                      connectivity_changed_cb, sd,
                      grpc_combiner_scheduler(combiner));
    // We assume that the current state is IDLE.  If not, we'll get a
    // callback telling us that.
    sd->prev_connectivity_state = GRPC_CHANNEL_IDLE;
    sd->curr_connectivity_state = GRPC_CHANNEL_IDLE;
    sd->pending_connectivity_state_unsafe = GRPC_CHANNEL_IDLE;
    sd->user_data_vtable = addresses->user_data_vtable;
    if (sd->user_data_vtable != nullptr) {
      sd->user_data =
          sd->user_data_vtable->copy(addresses->addresses[i].user_data);
    }
  }
  subchannel_list->num_subchannels = subchannel_index;
  subchannel_list->num_idle = subchannel_index;
  return subchannel_list;
}

static void subchannel_list_destroy(grpc_lb_subchannel_list* subchannel_list) {
  if (subchannel_list->tracer->enabled()) {
    gpr_log(GPR_DEBUG, "[%s %p] Destroying subchannel_list %p",
            subchannel_list->tracer->name(), subchannel_list->policy,
            subchannel_list);
  }
  for (size_t i = 0; i < subchannel_list->num_subchannels; i++) {
    grpc_lb_subchannel_data* sd = &subchannel_list->subchannels[i];
    grpc_lb_subchannel_data_unref_subchannel(sd, "subchannel_list_destroy");
  }
  gpr_free(subchannel_list->subchannels);
  gpr_free(subchannel_list);
}

void grpc_lb_subchannel_list_ref(grpc_lb_subchannel_list* subchannel_list,
                                 const char* reason) {
  gpr_ref_non_zero(&subchannel_list->refcount);
  if (subchannel_list->tracer->enabled()) {
    const gpr_atm count = gpr_atm_acq_load(&subchannel_list->refcount.count);
    gpr_log(GPR_DEBUG, "[%s %p] subchannel_list %p REF %lu->%lu (%s)",
            subchannel_list->tracer->name(), subchannel_list->policy,
            subchannel_list, static_cast<unsigned long>(count - 1),
            static_cast<unsigned long>(count), reason);
  }
}

void grpc_lb_subchannel_list_unref(grpc_lb_subchannel_list* subchannel_list,
                                   const char* reason) {
  const bool done = gpr_unref(&subchannel_list->refcount);
  if (subchannel_list->tracer->enabled()) {
    const gpr_atm count = gpr_atm_acq_load(&subchannel_list->refcount.count);
    gpr_log(GPR_DEBUG, "[%s %p] subchannel_list %p UNREF %lu->%lu (%s)",
            subchannel_list->tracer->name(), subchannel_list->policy,
            subchannel_list, static_cast<unsigned long>(count + 1),
            static_cast<unsigned long>(count), reason);
  }
  if (done) {
    subchannel_list_destroy(subchannel_list);
  }
}

static void subchannel_data_cancel_connectivity_watch(
    grpc_lb_subchannel_data* sd, const char* reason) {
  if (sd->subchannel_list->tracer->enabled()) {
    gpr_log(GPR_DEBUG,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): canceling connectivity watch (%s)",
            sd->subchannel_list->tracer->name(), sd->subchannel_list->policy,
            sd->subchannel_list,
            static_cast<size_t>(sd - sd->subchannel_list->subchannels),
            sd->subchannel_list->num_subchannels, sd->subchannel, reason);
  }
  grpc_subchannel_notify_on_state_change(sd->subchannel, nullptr, nullptr,
                                         &sd->connectivity_changed_closure);
}

void grpc_lb_subchannel_list_shutdown_and_unref(
    grpc_lb_subchannel_list* subchannel_list, const char* reason) {
  if (subchannel_list->tracer->enabled()) {
    gpr_log(GPR_DEBUG, "[%s %p] Shutting down subchannel_list %p (%s)",
            subchannel_list->tracer->name(), subchannel_list->policy,
            subchannel_list, reason);
  }
  GPR_ASSERT(!subchannel_list->shutting_down);
  subchannel_list->shutting_down = true;
  for (size_t i = 0; i < subchannel_list->num_subchannels; i++) {
    grpc_lb_subchannel_data* sd = &subchannel_list->subchannels[i];
    // If there's a pending notification for this subchannel, cancel it;
    // the callback is responsible for unreffing the subchannel.
    // Otherwise, unref the subchannel directly.
    if (sd->connectivity_notification_pending) {
      subchannel_data_cancel_connectivity_watch(sd, reason);
    } else if (sd->subchannel != nullptr) {
      grpc_lb_subchannel_data_unref_subchannel(sd, reason);
    }
  }
  grpc_lb_subchannel_list_unref(subchannel_list, reason);
}
