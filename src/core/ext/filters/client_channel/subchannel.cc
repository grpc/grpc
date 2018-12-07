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

#include "src/core/ext/filters/client_channel/subchannel.h"

#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <cstring>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/health/health_check_client.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/subchannel_index.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/mutex_lock.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/service_config.h"
#include "src/core/lib/transport/status_metadata.h"
#include "src/core/lib/uri/uri_parser.h"

#define INTERNAL_REF_BITS 16
#define STRONG_REF_MASK (~(gpr_atm)((1 << INTERNAL_REF_BITS) - 1))

#define GRPC_SUBCHANNEL_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_SUBCHANNEL_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_SUBCHANNEL_RECONNECT_MIN_TIMEOUT_SECONDS 20
#define GRPC_SUBCHANNEL_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_SUBCHANNEL_RECONNECT_JITTER 0.2

namespace {
struct state_watcher {
  grpc_closure closure;
  grpc_subchannel* subchannel;
  grpc_connectivity_state connectivity_state;
  grpc_connectivity_state last_connectivity_state;
  grpc_core::OrphanablePtr<grpc_core::HealthCheckClient> health_check_client;
  grpc_closure health_check_closure;
  grpc_connectivity_state health_state;
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

namespace grpc_core {

class ConnectedSubchannelStateWatcher;

}  // namespace grpc_core

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
  grpc_closure on_connected;

  /** callback for our alarm */
  grpc_closure on_alarm;

  /** pollset_set tracking who's interested in a connection
      being setup */
  grpc_pollset_set* pollset_set;

  grpc_core::UniquePtr<char> health_check_service_name;

  /** mutex protecting remaining elements */
  gpr_mu mu;

  /** active connection, or null */
  grpc_core::RefCountedPtr<grpc_core::ConnectedSubchannel> connected_subchannel;
  grpc_core::OrphanablePtr<grpc_core::ConnectedSubchannelStateWatcher>
      connected_subchannel_watcher;

  /** have we seen a disconnection? */
  bool disconnected;
  /** are we connecting */
  bool connecting;

  /** connectivity state tracking */
  grpc_connectivity_state_tracker state_tracker;
  grpc_connectivity_state_tracker state_and_health_tracker;

  external_state_watcher root_external_state_watcher;

  /** backoff state */
  grpc_core::ManualConstructor<grpc_core::BackOff> backoff;
  grpc_millis next_attempt_deadline;
  grpc_millis min_connect_timeout_ms;

  /** do we have an active alarm? */
  bool have_alarm;
  /** have we started the backoff loop */
  bool backoff_begun;
  // reset_backoff() was called while alarm was pending
  bool retry_immediately;
  /** our alarm */
  grpc_timer alarm;

  grpc_core::RefCountedPtr<grpc_core::channelz::SubchannelNode>
      channelz_subchannel;
};

struct grpc_subchannel_call {
  grpc_subchannel_call(grpc_core::ConnectedSubchannel* connection,
                       const grpc_core::ConnectedSubchannel::CallArgs& args)
      : connection(connection), deadline(args.deadline) {}

  grpc_core::ConnectedSubchannel* connection;
  grpc_closure* schedule_closure_after_destroy = nullptr;
  // state needed to support channelz interception of recv trailing metadata.
  grpc_closure recv_trailing_metadata_ready;
  grpc_closure* original_recv_trailing_metadata;
  grpc_metadata_batch* recv_trailing_metadata = nullptr;
  grpc_millis deadline;
};

static void maybe_start_connecting_locked(grpc_subchannel* c);

static const char* subchannel_connectivity_state_change_string(
    grpc_connectivity_state state) {
  switch (state) {
    case GRPC_CHANNEL_IDLE:
      return "Subchannel state change to IDLE";
    case GRPC_CHANNEL_CONNECTING:
      return "Subchannel state change to CONNECTING";
    case GRPC_CHANNEL_READY:
      return "Subchannel state change to READY";
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      return "Subchannel state change to TRANSIENT_FAILURE";
    case GRPC_CHANNEL_SHUTDOWN:
      return "Subchannel state change to SHUTDOWN";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

static void set_subchannel_connectivity_state_locked(
    grpc_subchannel* c, grpc_connectivity_state state, grpc_error* error,
    const char* reason) {
  if (c->channelz_subchannel != nullptr) {
    c->channelz_subchannel->AddTraceEvent(
        grpc_core::channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string(
            subchannel_connectivity_state_change_string(state)));
  }
  grpc_connectivity_state_set(&c->state_tracker, state, error, reason);
}

namespace grpc_core {

class ConnectedSubchannelStateWatcher
    : public InternallyRefCounted<ConnectedSubchannelStateWatcher> {
 public:
  // Must be instantiated while holding c->mu.
  explicit ConnectedSubchannelStateWatcher(grpc_subchannel* c)
      : subchannel_(c) {
    // Steal subchannel ref for connecting.
    GRPC_SUBCHANNEL_WEAK_REF(subchannel_, "state_watcher");
    GRPC_SUBCHANNEL_WEAK_UNREF(subchannel_, "connecting");
    // Start watching for connectivity state changes.
    // Callback uses initial ref to this.
    GRPC_CLOSURE_INIT(&on_connectivity_changed_, OnConnectivityChanged, this,
                      grpc_schedule_on_exec_ctx);
    c->connected_subchannel->NotifyOnStateChange(c->pollset_set,
                                                 &pending_connectivity_state_,
                                                 &on_connectivity_changed_);
    // Start health check if needed.
    grpc_connectivity_state health_state = GRPC_CHANNEL_READY;
    if (c->health_check_service_name != nullptr) {
      health_check_client_ = grpc_core::MakeOrphanable<HealthCheckClient>(
          c->health_check_service_name.get(), c->connected_subchannel,
          c->pollset_set, c->channelz_subchannel);
      GRPC_CLOSURE_INIT(&on_health_changed_, OnHealthChanged, this,
                        grpc_schedule_on_exec_ctx);
      Ref().release();  // Ref for health callback tracked manually.
      health_check_client_->NotifyOnHealthChange(&health_state_,
                                                 &on_health_changed_);
      health_state = GRPC_CHANNEL_CONNECTING;
    }
    // Report initial state.
    set_subchannel_connectivity_state_locked(
        c, GRPC_CHANNEL_READY, GRPC_ERROR_NONE, "subchannel_connected");
    grpc_connectivity_state_set(&c->state_and_health_tracker, health_state,
                                GRPC_ERROR_NONE, "subchannel_connected");
  }

  ~ConnectedSubchannelStateWatcher() {
    GRPC_SUBCHANNEL_WEAK_UNREF(subchannel_, "state_watcher");
  }

  void Orphan() override { health_check_client_.reset(); }

 private:
  static void OnConnectivityChanged(void* arg, grpc_error* error) {
    auto* self = static_cast<ConnectedSubchannelStateWatcher*>(arg);
    grpc_subchannel* c = self->subchannel_;
    {
      MutexLock lock(&c->mu);
      switch (self->pending_connectivity_state_) {
        case GRPC_CHANNEL_TRANSIENT_FAILURE:
        case GRPC_CHANNEL_SHUTDOWN: {
          if (!c->disconnected && c->connected_subchannel != nullptr) {
            if (grpc_trace_stream_refcount.enabled()) {
              gpr_log(GPR_INFO,
                      "Connected subchannel %p of subchannel %p has gone into "
                      "%s. Attempting to reconnect.",
                      c->connected_subchannel.get(), c,
                      grpc_connectivity_state_name(
                          self->pending_connectivity_state_));
            }
            c->connected_subchannel.reset();
            c->connected_subchannel_watcher.reset();
            self->last_connectivity_state_ = GRPC_CHANNEL_TRANSIENT_FAILURE;
            set_subchannel_connectivity_state_locked(
                c, GRPC_CHANNEL_TRANSIENT_FAILURE, GRPC_ERROR_REF(error),
                "reflect_child");
            grpc_connectivity_state_set(&c->state_and_health_tracker,
                                        GRPC_CHANNEL_TRANSIENT_FAILURE,
                                        GRPC_ERROR_REF(error), "reflect_child");
            c->backoff_begun = false;
            c->backoff->Reset();
            maybe_start_connecting_locked(c);
          } else {
            self->last_connectivity_state_ = GRPC_CHANNEL_SHUTDOWN;
          }
          self->health_check_client_.reset();
          break;
        }
        default: {
          // In principle, this should never happen.  We should not get
          // a callback for READY, because that was the state we started
          // this watch from.  And a connected subchannel should never go
          // from READY to CONNECTING or IDLE.
          self->last_connectivity_state_ = self->pending_connectivity_state_;
          set_subchannel_connectivity_state_locked(
              c, self->pending_connectivity_state_, GRPC_ERROR_REF(error),
              "reflect_child");
          if (self->pending_connectivity_state_ != GRPC_CHANNEL_READY) {
            grpc_connectivity_state_set(&c->state_and_health_tracker,
                                        self->pending_connectivity_state_,
                                        GRPC_ERROR_REF(error), "reflect_child");
          }
          c->connected_subchannel->NotifyOnStateChange(
              nullptr, &self->pending_connectivity_state_,
              &self->on_connectivity_changed_);
          self = nullptr;  // So we don't unref below.
        }
      }
    }
    // Don't unref until we've released the lock, because this might
    // cause the subchannel (which contains the lock) to be destroyed.
    if (self != nullptr) self->Unref();
  }

  static void OnHealthChanged(void* arg, grpc_error* error) {
    auto* self = static_cast<ConnectedSubchannelStateWatcher*>(arg);
    if (self->health_state_ == GRPC_CHANNEL_SHUTDOWN) {
      self->Unref();
      return;
    }
    grpc_subchannel* c = self->subchannel_;
    MutexLock lock(&c->mu);
    if (self->last_connectivity_state_ == GRPC_CHANNEL_READY) {
      grpc_connectivity_state_set(&c->state_and_health_tracker,
                                  self->health_state_, GRPC_ERROR_REF(error),
                                  "health_changed");
    }
    self->health_check_client_->NotifyOnHealthChange(&self->health_state_,
                                                     &self->on_health_changed_);
  }

  grpc_subchannel* subchannel_;
  grpc_closure on_connectivity_changed_;
  grpc_connectivity_state pending_connectivity_state_ = GRPC_CHANNEL_READY;
  grpc_connectivity_state last_connectivity_state_ = GRPC_CHANNEL_READY;
  grpc_core::OrphanablePtr<grpc_core::HealthCheckClient> health_check_client_;
  grpc_closure on_health_changed_;
  grpc_connectivity_state health_state_ = GRPC_CHANNEL_CONNECTING;
};

}  // namespace grpc_core

#define SUBCHANNEL_CALL_TO_CALL_STACK(call)                          \
  (grpc_call_stack*)((char*)(call) + GPR_ROUND_UP_TO_ALIGNMENT_SIZE( \
                                         sizeof(grpc_subchannel_call)))
#define CALLSTACK_TO_SUBCHANNEL_CALL(callstack)           \
  (grpc_subchannel_call*)(((char*)(call_stack)) -         \
                          GPR_ROUND_UP_TO_ALIGNMENT_SIZE( \
                              sizeof(grpc_subchannel_call)))

static void on_subchannel_connected(void* subchannel, grpc_error* error);

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
  grpc_channel_stack* stk = static_cast<grpc_channel_stack*>(arg);
  grpc_channel_stack_destroy(stk);
  gpr_free(stk);
}

/*
 * grpc_subchannel implementation
 */

static void subchannel_destroy(void* arg, grpc_error* error) {
  grpc_subchannel* c = static_cast<grpc_subchannel*>(arg);
  if (c->channelz_subchannel != nullptr) {
    c->channelz_subchannel->AddTraceEvent(
        grpc_core::channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string("Subchannel destroyed"));
    c->channelz_subchannel->MarkSubchannelDestroyed();
    c->channelz_subchannel.reset();
  }
  gpr_free((void*)c->filters);
  c->health_check_service_name.reset();
  grpc_channel_args_destroy(c->args);
  grpc_connectivity_state_destroy(&c->state_tracker);
  grpc_connectivity_state_destroy(&c->state_and_health_tracker);
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
  grpc_subchannel_index_unregister(c->key, c);
  gpr_mu_lock(&c->mu);
  GPR_ASSERT(!c->disconnected);
  c->disconnected = true;
  grpc_connector_shutdown(c->connector, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                            "Subchannel disconnected"));
  c->connected_subchannel.reset();
  c->connected_subchannel_watcher.reset();
  gpr_mu_unlock(&c->mu);
}

void grpc_subchannel_unref(grpc_subchannel* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  // add a weak ref and subtract a strong ref (atomically)
  old_refs = ref_mutate(
      c, static_cast<gpr_atm>(1) - static_cast<gpr_atm>(1 << INTERNAL_REF_BITS),
      1 REF_MUTATE_PURPOSE("STRONG_UNREF"));
  if ((old_refs & STRONG_REF_MASK) == (1 << INTERNAL_REF_BITS)) {
    disconnect(c);
  }
  GRPC_SUBCHANNEL_WEAK_UNREF(c, "strong-unref");
}

void grpc_subchannel_weak_unref(
    grpc_subchannel* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = ref_mutate(c, -static_cast<gpr_atm>(1),
                        1 REF_MUTATE_PURPOSE("WEAK_UNREF"));
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

namespace grpc_core {
namespace {

struct HealthCheckParams {
  UniquePtr<char> service_name;

  static void Parse(const grpc_json* field, HealthCheckParams* params) {
    if (strcmp(field->key, "healthCheckConfig") == 0) {
      if (field->type != GRPC_JSON_OBJECT) return;
      for (grpc_json* sub_field = field->child; sub_field != nullptr;
           sub_field = sub_field->next) {
        if (sub_field->key == nullptr) return;
        if (strcmp(sub_field->key, "serviceName") == 0) {
          if (params->service_name != nullptr) return;  // Duplicate.
          if (sub_field->type != GRPC_JSON_STRING) return;
          params->service_name.reset(gpr_strdup(sub_field->value));
        }
      }
    }
  }
};

}  // namespace
}  // namespace grpc_core

grpc_subchannel* grpc_subchannel_create(grpc_connector* connector,
                                        const grpc_subchannel_args* args) {
  grpc_subchannel_key* key = grpc_subchannel_key_create(args);
  grpc_subchannel* c = grpc_subchannel_index_find(key);
  if (c) {
    grpc_subchannel_key_destroy(key);
    return c;
  }

  GRPC_STATS_INC_CLIENT_SUBCHANNELS_CREATED();
  c = static_cast<grpc_subchannel*>(gpr_zalloc(sizeof(*c)));
  c->key = key;
  gpr_atm_no_barrier_store(&c->ref_pair, 1 << INTERNAL_REF_BITS);
  c->connector = connector;
  grpc_connector_ref(c->connector);
  c->num_filters = args->filter_count;
  if (c->num_filters > 0) {
    c->filters = static_cast<const grpc_channel_filter**>(
        gpr_malloc(sizeof(grpc_channel_filter*) * c->num_filters));
    memcpy((void*)c->filters, args->filters,
           sizeof(grpc_channel_filter*) * c->num_filters);
  } else {
    c->filters = nullptr;
  }
  c->pollset_set = grpc_pollset_set_create();
  grpc_resolved_address* addr =
      static_cast<grpc_resolved_address*>(gpr_malloc(sizeof(*addr)));
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
  GRPC_CLOSURE_INIT(&c->on_connected, on_subchannel_connected, c,
                    grpc_schedule_on_exec_ctx);
  grpc_connectivity_state_init(&c->state_tracker, GRPC_CHANNEL_IDLE,
                               "subchannel");
  grpc_connectivity_state_init(&c->state_and_health_tracker, GRPC_CHANNEL_IDLE,
                               "subchannel");
  grpc_core::BackOff::Options backoff_options;
  parse_args_for_backoff_values(args->args, &backoff_options,
                                &c->min_connect_timeout_ms);
  c->backoff.Init(backoff_options);
  gpr_mu_init(&c->mu);

  // Check whether we should enable health checking.
  const char* service_config_json = grpc_channel_arg_get_string(
      grpc_channel_args_find(c->args, GRPC_ARG_SERVICE_CONFIG));
  if (service_config_json != nullptr) {
    grpc_core::UniquePtr<grpc_core::ServiceConfig> service_config =
        grpc_core::ServiceConfig::Create(service_config_json);
    if (service_config != nullptr) {
      grpc_core::HealthCheckParams params;
      service_config->ParseGlobalParams(grpc_core::HealthCheckParams::Parse,
                                        &params);
      c->health_check_service_name = std::move(params.service_name);
    }
  }

  const grpc_arg* arg =
      grpc_channel_args_find(c->args, GRPC_ARG_ENABLE_CHANNELZ);
  bool channelz_enabled =
      grpc_channel_arg_get_bool(arg, GRPC_ENABLE_CHANNELZ_DEFAULT);
  arg = grpc_channel_args_find(
      c->args, GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE);
  const grpc_integer_options options = {
      GRPC_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE_DEFAULT, 0, INT_MAX};
  size_t channel_tracer_max_memory =
      (size_t)grpc_channel_arg_get_integer(arg, options);
  if (channelz_enabled) {
    c->channelz_subchannel =
        grpc_core::MakeRefCounted<grpc_core::channelz::SubchannelNode>(
            c, channel_tracer_max_memory);
    c->channelz_subchannel->AddTraceEvent(
        grpc_core::channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string("Subchannel created"));
  }

  return grpc_subchannel_index_register(key, c);
}

grpc_core::channelz::SubchannelNode* grpc_subchannel_get_channelz_node(
    grpc_subchannel* subchannel) {
  return subchannel->channelz_subchannel.get();
}

intptr_t grpc_subchannel_get_child_socket_uuid(grpc_subchannel* subchannel) {
  if (subchannel->connected_subchannel != nullptr) {
    return subchannel->connected_subchannel->socket_uuid();
  } else {
    return 0;
  }
}

static void continue_connect_locked(grpc_subchannel* c) {
  grpc_connect_in_args args;
  args.interested_parties = c->pollset_set;
  const grpc_millis min_deadline =
      c->min_connect_timeout_ms + grpc_core::ExecCtx::Get()->Now();
  c->next_attempt_deadline = c->backoff->NextAttemptTime();
  args.deadline = std::max(c->next_attempt_deadline, min_deadline);
  args.channel_args = c->args;
  set_subchannel_connectivity_state_locked(c, GRPC_CHANNEL_CONNECTING,
                                           GRPC_ERROR_NONE, "connecting");
  grpc_connectivity_state_set(&c->state_and_health_tracker,
                              GRPC_CHANNEL_CONNECTING, GRPC_ERROR_NONE,
                              "connecting");
  grpc_connector_connect(c->connector, &args, &c->connecting_result,
                         &c->on_connected);
}

grpc_connectivity_state grpc_subchannel_check_connectivity(
    grpc_subchannel* c, grpc_error** error, bool inhibit_health_checks) {
  gpr_mu_lock(&c->mu);
  grpc_connectivity_state_tracker* tracker =
      inhibit_health_checks ? &c->state_tracker : &c->state_and_health_tracker;
  grpc_connectivity_state state = grpc_connectivity_state_get(tracker, error);
  gpr_mu_unlock(&c->mu);
  return state;
}

static void on_external_state_watcher_done(void* arg, grpc_error* error) {
  external_state_watcher* w = static_cast<external_state_watcher*>(arg);
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
  GRPC_CLOSURE_SCHED(follow_up, GRPC_ERROR_REF(error));
}

static void on_alarm(void* arg, grpc_error* error) {
  grpc_subchannel* c = static_cast<grpc_subchannel*>(arg);
  gpr_mu_lock(&c->mu);
  c->have_alarm = false;
  if (c->disconnected) {
    error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Disconnected",
                                                             &error, 1);
  } else if (c->retry_immediately) {
    c->retry_immediately = false;
    error = GRPC_ERROR_NONE;
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
  if (c->connected_subchannel != nullptr) {
    /* Already connected: don't restart */
    return;
  }
  if (!grpc_connectivity_state_has_watchers(&c->state_tracker) &&
      !grpc_connectivity_state_has_watchers(&c->state_and_health_tracker)) {
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
      gpr_log(GPR_INFO, "Subchannel %p: Retry immediately", c);
    } else {
      gpr_log(GPR_INFO, "Subchannel %p: Retry in %" PRId64 " milliseconds", c,
              time_til_next);
    }
    GRPC_CLOSURE_INIT(&c->on_alarm, on_alarm, c, grpc_schedule_on_exec_ctx);
    grpc_timer_init(&c->alarm, c->next_attempt_deadline, &c->on_alarm);
  }
}

void grpc_subchannel_notify_on_state_change(
    grpc_subchannel* c, grpc_pollset_set* interested_parties,
    grpc_connectivity_state* state, grpc_closure* notify,
    bool inhibit_health_checks) {
  grpc_connectivity_state_tracker* tracker =
      inhibit_health_checks ? &c->state_tracker : &c->state_and_health_tracker;
  external_state_watcher* w;
  if (state == nullptr) {
    gpr_mu_lock(&c->mu);
    for (w = c->root_external_state_watcher.next;
         w != &c->root_external_state_watcher; w = w->next) {
      if (w->notify == notify) {
        grpc_connectivity_state_notify_on_state_change(tracker, nullptr,
                                                       &w->closure);
      }
    }
    gpr_mu_unlock(&c->mu);
  } else {
    w = static_cast<external_state_watcher*>(gpr_malloc(sizeof(*w)));
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
    grpc_connectivity_state_notify_on_state_change(tracker, state, &w->closure);
    maybe_start_connecting_locked(c);
    gpr_mu_unlock(&c->mu);
  }
}

static bool publish_transport_locked(grpc_subchannel* c) {
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
  grpc_channel_stack* stk;
  grpc_error* error = grpc_channel_stack_builder_finish(
      builder, 0, 1, connection_destroy, nullptr,
      reinterpret_cast<void**>(&stk));
  if (error != GRPC_ERROR_NONE) {
    grpc_transport_destroy(c->connecting_result.transport);
    gpr_log(GPR_ERROR, "error initializing subchannel stack: %s",
            grpc_error_string(error));
    GRPC_ERROR_UNREF(error);
    return false;
  }
  intptr_t socket_uuid = c->connecting_result.socket_uuid;
  memset(&c->connecting_result, 0, sizeof(c->connecting_result));

  if (c->disconnected) {
    grpc_channel_stack_destroy(stk);
    gpr_free(stk);
    return false;
  }

  /* publish */
  c->connected_subchannel.reset(grpc_core::New<grpc_core::ConnectedSubchannel>(
      stk, c->args, c->channelz_subchannel, socket_uuid));
  gpr_log(GPR_INFO, "New connected subchannel at %p for subchannel %p",
          c->connected_subchannel.get(), c);

  // Instantiate state watcher.  Will clean itself up.
  c->connected_subchannel_watcher =
      grpc_core::MakeOrphanable<grpc_core::ConnectedSubchannelStateWatcher>(c);

  return true;
}

static void on_subchannel_connected(void* arg, grpc_error* error) {
  grpc_subchannel* c = static_cast<grpc_subchannel*>(arg);
  grpc_channel_args* delete_channel_args = c->connecting_result.channel_args;

  GRPC_SUBCHANNEL_WEAK_REF(c, "on_subchannel_connected");
  gpr_mu_lock(&c->mu);
  c->connecting = false;
  if (c->connecting_result.transport != nullptr &&
      publish_transport_locked(c)) {
    /* do nothing, transport was published */
  } else if (c->disconnected) {
    GRPC_SUBCHANNEL_WEAK_UNREF(c, "connecting");
  } else {
    set_subchannel_connectivity_state_locked(
        c, GRPC_CHANNEL_TRANSIENT_FAILURE,
        grpc_error_set_int(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                               "Connect Failed", &error, 1),
                           GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE),
        "connect_failed");
    grpc_connectivity_state_set(
        &c->state_and_health_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
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

void grpc_subchannel_reset_backoff(grpc_subchannel* subchannel) {
  gpr_mu_lock(&subchannel->mu);
  subchannel->backoff->Reset();
  if (subchannel->have_alarm) {
    subchannel->retry_immediately = true;
    grpc_timer_cancel(&subchannel->alarm);
  } else {
    subchannel->backoff_begun = false;
    maybe_start_connecting_locked(subchannel);
  }
  gpr_mu_unlock(&subchannel->mu);
}

/*
 * grpc_subchannel_call implementation
 */

static void subchannel_call_destroy(void* call, grpc_error* error) {
  GPR_TIMER_SCOPE("grpc_subchannel_call_unref.destroy", 0);
  grpc_subchannel_call* c = static_cast<grpc_subchannel_call*>(call);
  grpc_core::ConnectedSubchannel* connection = c->connection;
  grpc_call_stack_destroy(SUBCHANNEL_CALL_TO_CALL_STACK(c), nullptr,
                          c->schedule_closure_after_destroy);
  connection->Unref(DEBUG_LOCATION, "subchannel_call");
  c->~grpc_subchannel_call();
}

void grpc_subchannel_call_set_cleanup_closure(grpc_subchannel_call* call,
                                              grpc_closure* closure) {
  GPR_ASSERT(call->schedule_closure_after_destroy == nullptr);
  GPR_ASSERT(closure != nullptr);
  call->schedule_closure_after_destroy = closure;
}

grpc_subchannel_call* grpc_subchannel_call_ref(
    grpc_subchannel_call* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(c), REF_REASON);
  return c;
}

void grpc_subchannel_call_unref(
    grpc_subchannel_call* c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  GRPC_CALL_STACK_UNREF(SUBCHANNEL_CALL_TO_CALL_STACK(c), REF_REASON);
}

// Sets *status based on md_batch and error.
static void get_call_status(grpc_subchannel_call* call,
                            grpc_metadata_batch* md_batch, grpc_error* error,
                            grpc_status_code* status) {
  if (error != GRPC_ERROR_NONE) {
    grpc_error_get_status(error, call->deadline, status, nullptr, nullptr,
                          nullptr);
  } else {
    if (md_batch->idx.named.grpc_status != nullptr) {
      *status = grpc_get_status_code_from_metadata(
          md_batch->idx.named.grpc_status->md);
    } else {
      *status = GRPC_STATUS_UNKNOWN;
    }
  }
  GRPC_ERROR_UNREF(error);
}

static void recv_trailing_metadata_ready(void* arg, grpc_error* error) {
  grpc_subchannel_call* call = static_cast<grpc_subchannel_call*>(arg);
  GPR_ASSERT(call->recv_trailing_metadata != nullptr);
  grpc_status_code status = GRPC_STATUS_OK;
  grpc_metadata_batch* md_batch = call->recv_trailing_metadata;
  get_call_status(call, md_batch, GRPC_ERROR_REF(error), &status);
  grpc_core::channelz::SubchannelNode* channelz_subchannel =
      call->connection->channelz_subchannel();
  GPR_ASSERT(channelz_subchannel != nullptr);
  if (status == GRPC_STATUS_OK) {
    channelz_subchannel->RecordCallSucceeded();
  } else {
    channelz_subchannel->RecordCallFailed();
  }
  GRPC_CLOSURE_RUN(call->original_recv_trailing_metadata,
                   GRPC_ERROR_REF(error));
}

// If channelz is enabled, intercept recv_trailing so that we may check the
// status and associate it to a subchannel.
static void maybe_intercept_recv_trailing_metadata(
    grpc_subchannel_call* call, grpc_transport_stream_op_batch* batch) {
  // only intercept payloads with recv trailing.
  if (!batch->recv_trailing_metadata) {
    return;
  }
  // only add interceptor is channelz is enabled.
  if (call->connection->channelz_subchannel() == nullptr) {
    return;
  }
  GRPC_CLOSURE_INIT(&call->recv_trailing_metadata_ready,
                    recv_trailing_metadata_ready, call,
                    grpc_schedule_on_exec_ctx);
  // save some state needed for the interception callback.
  GPR_ASSERT(call->recv_trailing_metadata == nullptr);
  call->recv_trailing_metadata =
      batch->payload->recv_trailing_metadata.recv_trailing_metadata;
  call->original_recv_trailing_metadata =
      batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
  batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      &call->recv_trailing_metadata_ready;
}

void grpc_subchannel_call_process_op(grpc_subchannel_call* call,
                                     grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_SCOPE("grpc_subchannel_call_process_op", 0);
  maybe_intercept_recv_trailing_metadata(call, batch);
  grpc_call_stack* call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  grpc_call_element* top_elem = grpc_call_stack_element(call_stack, 0);
  GRPC_CALL_LOG_OP(GPR_INFO, top_elem, batch);
  top_elem->filter->start_transport_stream_op_batch(top_elem, batch);
}

grpc_core::RefCountedPtr<grpc_core::ConnectedSubchannel>
grpc_subchannel_get_connected_subchannel(grpc_subchannel* c) {
  gpr_mu_lock(&c->mu);
  auto copy = c->connected_subchannel;
  gpr_mu_unlock(&c->mu);
  return copy;
}

const grpc_subchannel_key* grpc_subchannel_get_key(
    const grpc_subchannel* subchannel) {
  return subchannel->key;
}

void* grpc_connected_subchannel_call_get_parent_data(
    grpc_subchannel_call* subchannel_call) {
  grpc_channel_stack* chanstk = subchannel_call->connection->channel_stack();
  return (char*)subchannel_call + sizeof(grpc_subchannel_call) +
         chanstk->call_stack_size;
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

const char* grpc_subchannel_get_target(grpc_subchannel* subchannel) {
  const grpc_arg* addr_arg =
      grpc_channel_args_find(subchannel->args, GRPC_ARG_SUBCHANNEL_ADDRESS);
  const char* addr_str = grpc_channel_arg_get_string(addr_arg);
  GPR_ASSERT(addr_str != nullptr);  // Should have been set by LB policy.
  return addr_str;
}

const char* grpc_get_subchannel_address_uri_arg(const grpc_channel_args* args) {
  const grpc_arg* addr_arg =
      grpc_channel_args_find(args, GRPC_ARG_SUBCHANNEL_ADDRESS);
  const char* addr_str = grpc_channel_arg_get_string(addr_arg);
  GPR_ASSERT(addr_str != nullptr);  // Should have been set by LB policy.
  return addr_str;
}

grpc_arg grpc_create_subchannel_address_arg(const grpc_resolved_address* addr) {
  return grpc_channel_arg_string_create(
      (char*)GRPC_ARG_SUBCHANNEL_ADDRESS,
      addr->len > 0 ? grpc_sockaddr_to_uri(addr) : gpr_strdup(""));
}

namespace grpc_core {

ConnectedSubchannel::ConnectedSubchannel(
    grpc_channel_stack* channel_stack, const grpc_channel_args* args,
    grpc_core::RefCountedPtr<grpc_core::channelz::SubchannelNode>
        channelz_subchannel,
    intptr_t socket_uuid)
    : RefCounted<ConnectedSubchannel>(&grpc_trace_stream_refcount),
      channel_stack_(channel_stack),
      args_(grpc_channel_args_copy(args)),
      channelz_subchannel_(std::move(channelz_subchannel)),
      socket_uuid_(socket_uuid) {}

ConnectedSubchannel::~ConnectedSubchannel() {
  grpc_channel_args_destroy(args_);
  GRPC_CHANNEL_STACK_UNREF(channel_stack_, "connected_subchannel_dtor");
}

void ConnectedSubchannel::NotifyOnStateChange(
    grpc_pollset_set* interested_parties, grpc_connectivity_state* state,
    grpc_closure* closure) {
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  grpc_channel_element* elem;
  op->connectivity_state = state;
  op->on_connectivity_state_change = closure;
  op->bind_pollset_set = interested_parties;
  elem = grpc_channel_stack_element(channel_stack_, 0);
  elem->filter->start_transport_op(elem, op);
}

void ConnectedSubchannel::Ping(grpc_closure* on_initiate,
                               grpc_closure* on_ack) {
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  grpc_channel_element* elem;
  op->send_ping.on_initiate = on_initiate;
  op->send_ping.on_ack = on_ack;
  elem = grpc_channel_stack_element(channel_stack_, 0);
  elem->filter->start_transport_op(elem, op);
}

grpc_error* ConnectedSubchannel::CreateCall(const CallArgs& args,
                                            grpc_subchannel_call** call) {
  const size_t allocation_size =
      GetInitialCallSizeEstimate(args.parent_data_size);
  *call = new (gpr_arena_alloc(args.arena, allocation_size))
      grpc_subchannel_call(this, args);
  grpc_call_stack* callstk = SUBCHANNEL_CALL_TO_CALL_STACK(*call);
  RefCountedPtr<ConnectedSubchannel> connection =
      Ref(DEBUG_LOCATION, "subchannel_call");
  connection.release();  // Ref is passed to the grpc_subchannel_call object.
  const grpc_call_element_args call_args = {
      callstk,           /* call_stack */
      nullptr,           /* server_transport_data */
      args.context,      /* context */
      args.path,         /* path */
      args.start_time,   /* start_time */
      args.deadline,     /* deadline */
      args.arena,        /* arena */
      args.call_combiner /* call_combiner */
  };
  grpc_error* error = grpc_call_stack_init(
      channel_stack_, 1, subchannel_call_destroy, *call, &call_args);
  if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
    const char* error_string = grpc_error_string(error);
    gpr_log(GPR_ERROR, "error: %s", error_string);
    return error;
  }
  grpc_call_stack_set_pollset_or_pollset_set(callstk, args.pollent);
  if (channelz_subchannel_ != nullptr) {
    channelz_subchannel_->RecordCallStarted();
  }
  return GRPC_ERROR_NONE;
}

size_t ConnectedSubchannel::GetInitialCallSizeEstimate(
    size_t parent_data_size) const {
  size_t allocation_size =
      GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(grpc_subchannel_call));
  if (parent_data_size > 0) {
    allocation_size +=
        GPR_ROUND_UP_TO_ALIGNMENT_SIZE(channel_stack_->call_stack_size) +
        parent_data_size;
  } else {
    allocation_size += channel_stack_->call_stack_size;
  }
  return allocation_size;
}

}  // namespace grpc_core
