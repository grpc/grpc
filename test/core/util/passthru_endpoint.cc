//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "test/core/util/passthru_endpoint.h"

#include <string.h>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;

typedef struct passthru_endpoint passthru_endpoint;

typedef struct {
  bool is_armed;
  grpc_endpoint* ep;
  grpc_slice_buffer* slices;
  grpc_closure* cb;
} pending_op;

typedef struct {
  absl::optional<EventEngine::TaskHandle> timer_handle;
  uint64_t allowed_write_bytes;
  uint64_t allowed_read_bytes;
  std::vector<grpc_passthru_endpoint_channel_action> actions;
  std::function<void()> on_complete;
} grpc_passthru_endpoint_channel_effects;

typedef struct {
  grpc_endpoint base;
  passthru_endpoint* parent;
  grpc_slice_buffer read_buffer;
  grpc_slice_buffer write_buffer;
  grpc_slice_buffer* on_read_out;
  grpc_closure* on_read;
  pending_op pending_read_op;
  pending_op pending_write_op;
  uint64_t bytes_read_so_far;
  uint64_t bytes_written_so_far;
} half;

struct passthru_endpoint {
  gpr_mu mu;
  int halves;
  grpc_passthru_endpoint_stats* stats;
  grpc_passthru_endpoint_channel_effects* channel_effects;
  bool simulate_channel_actions;
  bool shutdown;
  half client;
  half server;
};

static void do_pending_read_op_locked(half* m, grpc_error_handle error) {
  GPR_ASSERT(m->pending_read_op.is_armed);
  GPR_ASSERT(m->bytes_read_so_far <=
             m->parent->channel_effects->allowed_read_bytes);
  if (m->parent->shutdown) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, m->pending_read_op.cb,
                            GRPC_ERROR_CREATE("Already shutdown"));
    // Move any pending data into pending_read_op.slices so that it may be
    // free'ed by the executing callback.
    grpc_slice_buffer_move_into(&m->read_buffer, m->pending_read_op.slices);
    m->pending_read_op.is_armed = false;
    return;
  }

  if (m->bytes_read_so_far == m->parent->channel_effects->allowed_read_bytes) {
    // Keep it in pending state.
    return;
  }
  // This delayed processing should only be invoked when read_buffer has
  // something in it.
  GPR_ASSERT(m->read_buffer.count > 0);
  uint64_t readable_length = std::min<uint64_t>(
      m->read_buffer.length,
      m->parent->channel_effects->allowed_read_bytes - m->bytes_read_so_far);
  GPR_ASSERT(readable_length > 0);
  grpc_slice_buffer_move_first(&m->read_buffer, readable_length,
                               m->pending_read_op.slices);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, m->pending_read_op.cb, error);
  if (m->parent->simulate_channel_actions) {
    m->bytes_read_so_far += readable_length;
  }
  m->pending_read_op.is_armed = false;
}

static void me_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                    grpc_closure* cb, bool /*urgent*/,
                    int /*min_progress_size*/) {
  half* m = reinterpret_cast<half*>(ep);
  gpr_mu_lock(&m->parent->mu);
  if (m->parent->shutdown) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb,
                            GRPC_ERROR_CREATE("Already shutdown"));
  } else if (m->read_buffer.count > 0) {
    GPR_ASSERT(!m->pending_read_op.is_armed);
    GPR_ASSERT(!m->on_read);
    m->pending_read_op.is_armed = true;
    m->pending_read_op.cb = cb;
    m->pending_read_op.ep = ep;
    m->pending_read_op.slices = slices;
    do_pending_read_op_locked(m, absl::OkStatus());
  } else {
    GPR_ASSERT(!m->pending_read_op.is_armed);
    m->on_read = cb;
    m->on_read_out = slices;
  }
  gpr_mu_unlock(&m->parent->mu);
}

// Copy src slice and split the copy at n bytes into two separate slices
void grpc_slice_copy_split(grpc_slice src, uint64_t n, grpc_slice& split1,
                           grpc_slice& split2) {
  GPR_ASSERT(n <= GRPC_SLICE_LENGTH(src));
  if (n == GRPC_SLICE_LENGTH(src)) {
    split1 = grpc_slice_copy(src);
    split2 = grpc_empty_slice();
    return;
  }
  split1 = GRPC_SLICE_MALLOC(n);
  memcpy(GRPC_SLICE_START_PTR(split1), GRPC_SLICE_START_PTR(src), n);
  split2 = GRPC_SLICE_MALLOC(GRPC_SLICE_LENGTH(src) - n);
  memcpy(GRPC_SLICE_START_PTR(split2), GRPC_SLICE_START_PTR(src) + n,
         GRPC_SLICE_LENGTH(src) - n);
}

static half* other_half(half* h) {
  if (h == &h->parent->client) return &h->parent->server;
  return &h->parent->client;
}

static void do_pending_write_op_locked(half* m, grpc_error_handle error) {
  GPR_ASSERT(m->pending_write_op.is_armed);
  GPR_ASSERT(m->bytes_written_so_far <=
             m->parent->channel_effects->allowed_write_bytes);
  if (m->parent->shutdown) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, m->pending_write_op.cb,
                            GRPC_ERROR_CREATE("Already shutdown"));
    m->pending_write_op.is_armed = false;
    grpc_slice_buffer_reset_and_unref(m->pending_write_op.slices);
    return;
  }
  if (m->bytes_written_so_far ==
      m->parent->channel_effects->allowed_write_bytes) {
    // Keep it in pending state.
    return;
  }

  half* other = other_half(m);
  uint64_t max_writable =
      std::min<uint64_t>(m->pending_write_op.slices->length,
                         m->parent->channel_effects->allowed_write_bytes -
                             m->bytes_written_so_far);
  uint64_t max_readable = other->parent->channel_effects->allowed_read_bytes -
                          other->bytes_read_so_far;
  uint64_t immediate_bytes_read =
      other->on_read != nullptr ? std::min<uint64_t>(max_readable, max_writable)
                                : 0;

  GPR_ASSERT(max_writable > 0);
  GPR_ASSERT(max_readable >= 0);
  // At the end of this process, we should have written max_writable bytes;
  if (m->parent->simulate_channel_actions) {
    m->bytes_written_so_far += max_writable;
  }
  // Estimate if the original write would still be pending at the end of this
  // process
  bool would_write_be_pending =
      max_writable < m->pending_write_op.slices->length;
  if (!m->parent->simulate_channel_actions) {
    GPR_ASSERT(!would_write_be_pending);
  }
  grpc_slice_buffer* slices = m->pending_write_op.slices;
  grpc_slice_buffer* dest =
      other->on_read != nullptr ? other->on_read_out : &other->read_buffer;
  while (max_writable > 0) {
    grpc_slice slice = grpc_slice_buffer_take_first(slices);
    uint64_t slice_length = GRPC_SLICE_LENGTH(slice);
    GPR_ASSERT(slice_length > 0);
    grpc_slice split1, split2;
    uint64_t split_length = 0;
    if (slice_length <= max_readable) {
      split_length = std::min<uint64_t>(slice_length, max_writable);
    } else if (max_readable > 0) {
      // slice_length > max_readable
      split_length = std::min<uint64_t>(max_readable, max_writable);
    } else {
      // slice_length still > max_readable but max_readable is 0.
      // In this case put the bytes into other->read_buffer. During a future
      // read if max_readable still remains zero at the time of read, the
      // pending read logic will kick in.
      dest = &other->read_buffer;
      split_length = std::min<uint64_t>(slice_length, max_writable);
    }

    grpc_slice_copy_split(slice, split_length, split1, split2);
    grpc_slice_unref(slice);
    // Write a copy of the slice to the destination to be read
    grpc_slice_buffer_add_indexed(dest, split1);
    // Re-insert split2 into source for next iteration.
    if (GRPC_SLICE_LENGTH(split2) > 0) {
      grpc_slice_buffer_undo_take_first(slices, split2);
    } else {
      grpc_slice_unref(split2);
    }

    if (max_readable > 0) {
      GPR_ASSERT(max_readable >= static_cast<uint64_t>(split_length));
      max_readable -= split_length;
    }

    GPR_ASSERT(max_writable >= static_cast<uint64_t>(split_length));
    max_writable -= split_length;
  }

  if (immediate_bytes_read > 0) {
    GPR_ASSERT(!other->pending_read_op.is_armed);
    if (m->parent->simulate_channel_actions) {
      other->bytes_read_so_far += immediate_bytes_read;
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, other->on_read, error);
    other->on_read = nullptr;
  }

  if (!would_write_be_pending) {
    // No slices should be left
    GPR_ASSERT(m->pending_write_op.slices->count == 0);
    grpc_slice_buffer_reset_and_unref(m->pending_write_op.slices);
    m->pending_write_op.is_armed = false;
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, m->pending_write_op.cb, error);
  }
}

static void me_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                     grpc_closure* cb, void* /*arg*/, int /*max_frame_size*/) {
  half* m = reinterpret_cast<half*>(ep);
  gpr_mu_lock(&m->parent->mu);
  gpr_atm_full_fetch_add(&m->parent->stats->num_writes, (gpr_atm)1);
  if (m->parent->shutdown) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb,
                            GRPC_ERROR_CREATE("Endpoint already shutdown"));
  } else {
    GPR_ASSERT(!m->pending_write_op.is_armed);
    // Copy slices into m->pending_write_op.slices
    m->pending_write_op.slices = &m->write_buffer;
    GPR_ASSERT(m->pending_write_op.slices->count == 0);
    for (int i = 0; i < static_cast<int>(slices->count); i++) {
      if (GRPC_SLICE_LENGTH(slices->slices[i]) > 0) {
        grpc_slice_buffer_add_indexed(m->pending_write_op.slices,
                                      grpc_slice_copy(slices->slices[i]));
      }
    }
    if (m->pending_write_op.slices->count > 0) {
      m->pending_write_op.is_armed = true;
      m->pending_write_op.cb = cb;
      m->pending_write_op.ep = ep;
      do_pending_write_op_locked(m, absl::OkStatus());
    } else {
      // There is nothing to write. Schedule callback to be run right away.
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::OkStatus());
    }
  }
  gpr_mu_unlock(&m->parent->mu);
}

void flush_pending_ops_locked(half* m, grpc_error_handle error) {
  if (m->pending_read_op.is_armed) {
    do_pending_read_op_locked(m, error);
  }
  if (m->pending_write_op.is_armed) {
    do_pending_write_op_locked(m, error);
  }
}

static void me_add_to_pollset(grpc_endpoint* /*ep*/,
                              grpc_pollset* /*pollset*/) {}

static void me_add_to_pollset_set(grpc_endpoint* /*ep*/,
                                  grpc_pollset_set* /*pollset*/) {}

static void me_delete_from_pollset_set(grpc_endpoint* /*ep*/,
                                       grpc_pollset_set* /*pollset*/) {}

static void shutdown_locked(half* m, grpc_error_handle why) {
  m->parent->shutdown = true;
  flush_pending_ops_locked(m, absl::OkStatus());
  if (m->on_read) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, m->on_read,
                            GRPC_ERROR_CREATE_REFERENCING("Shutdown", &why, 1));
    m->on_read = nullptr;
  }
  m = other_half(m);
  flush_pending_ops_locked(m, absl::OkStatus());
  if (m->on_read) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, m->on_read,
                            GRPC_ERROR_CREATE_REFERENCING("Shutdown", &why, 1));
    m->on_read = nullptr;
  }
}

static void me_shutdown(grpc_endpoint* ep, grpc_error_handle why) {
  half* m = reinterpret_cast<half*>(ep);
  gpr_mu_lock(&m->parent->mu);
  shutdown_locked(m, why);
  gpr_mu_unlock(&m->parent->mu);
}

void grpc_passthru_endpoint_destroy(passthru_endpoint* p) {
  gpr_mu_destroy(&p->mu);
  grpc_passthru_endpoint_stats_destroy(p->stats);
  delete p->channel_effects;
  grpc_slice_buffer_destroy(&p->client.read_buffer);
  grpc_slice_buffer_destroy(&p->server.read_buffer);
  grpc_slice_buffer_destroy(&p->client.write_buffer);
  grpc_slice_buffer_destroy(&p->server.write_buffer);
  gpr_free(p);
}

static void do_next_sched_channel_action(void* arg, grpc_error_handle error);

static void me_destroy(grpc_endpoint* ep) {
  passthru_endpoint* p = (reinterpret_cast<half*>(ep))->parent;
  gpr_mu_lock(&p->mu);
  if (0 == --p->halves && p->channel_effects->actions.empty()) {
    // no pending channel actions exist
    gpr_mu_unlock(&p->mu);
    grpc_passthru_endpoint_destroy(p);
  } else {
    if (p->halves == 0 && p->simulate_channel_actions) {
      if (p->channel_effects->timer_handle.has_value()) {
        if (GetDefaultEventEngine()->Cancel(
                *p->channel_effects->timer_handle)) {
          gpr_mu_unlock(&p->mu);
          // This will destroy the passthru endpoint so just return after that.
          do_next_sched_channel_action(ep, absl::CancelledError());
          return;
        }
        p->channel_effects->timer_handle.reset();
      }
    }
    gpr_mu_unlock(&p->mu);
  }
}

static absl::string_view me_get_peer(grpc_endpoint* ep) {
  passthru_endpoint* p = (reinterpret_cast<half*>(ep))->parent;
  return (reinterpret_cast<half*>(ep)) == &p->client
             ? "fake:mock_client_endpoint"
             : "fake:mock_server_endpoint";
}

static absl::string_view me_get_local_address(grpc_endpoint* ep) {
  passthru_endpoint* p = (reinterpret_cast<half*>(ep))->parent;
  return (reinterpret_cast<half*>(ep)) == &p->client
             ? "fake:mock_client_endpoint"
             : "fake:mock_server_endpoint";
}

static int me_get_fd(grpc_endpoint* /*ep*/) { return -1; }

static bool me_can_track_err(grpc_endpoint* /*ep*/) { return false; }

static const grpc_endpoint_vtable vtable = {
    me_read,
    me_write,
    me_add_to_pollset,
    me_add_to_pollset_set,
    me_delete_from_pollset_set,
    me_shutdown,
    me_destroy,
    me_get_peer,
    me_get_local_address,
    me_get_fd,
    me_can_track_err,
};

static void half_init(half* m, passthru_endpoint* parent,
                      const char* half_name) {
  m->base.vtable = &vtable;
  m->parent = parent;
  grpc_slice_buffer_init(&m->read_buffer);
  grpc_slice_buffer_init(&m->write_buffer);
  m->pending_write_op.slices = nullptr;
  m->on_read = nullptr;
  m->bytes_read_so_far = 0;
  m->bytes_written_so_far = 0;
  m->pending_write_op.is_armed = false;
  m->pending_read_op.is_armed = false;
  std::string name =
      absl::StrFormat("passthru_endpoint_%s_%p", half_name, parent);
}

void grpc_passthru_endpoint_create(grpc_endpoint** client,
                                   grpc_endpoint** server,
                                   grpc_passthru_endpoint_stats* stats,
                                   bool simulate_channel_actions) {
  passthru_endpoint* m =
      static_cast<passthru_endpoint*>(gpr_malloc(sizeof(*m)));
  m->halves = 2;
  m->shutdown = false;
  if (stats == nullptr) {
    m->stats = grpc_passthru_endpoint_stats_create();
  } else {
    gpr_ref(&stats->refs);
    m->stats = stats;
  }
  m->channel_effects = new grpc_passthru_endpoint_channel_effects();
  m->simulate_channel_actions = simulate_channel_actions;
  if (!simulate_channel_actions) {
    m->channel_effects->allowed_read_bytes = UINT64_MAX;
    m->channel_effects->allowed_write_bytes = UINT64_MAX;
  }
  half_init(&m->client, m, "client");
  half_init(&m->server, m, "server");
  gpr_mu_init(&m->mu);
  *client = &m->client.base;
  *server = &m->server.base;
}

grpc_passthru_endpoint_stats* grpc_passthru_endpoint_stats_create() {
  grpc_passthru_endpoint_stats* stats =
      static_cast<grpc_passthru_endpoint_stats*>(
          gpr_malloc(sizeof(grpc_passthru_endpoint_stats)));
  memset(stats, 0, sizeof(*stats));
  gpr_ref_init(&stats->refs, 1);
  return stats;
}

void grpc_passthru_endpoint_stats_destroy(grpc_passthru_endpoint_stats* stats) {
  if (gpr_unref(&stats->refs)) {
    gpr_free(stats);
  }
}

static void sched_next_channel_action_locked(half* m);

static void do_next_sched_channel_action(void* arg, grpc_error_handle error) {
  half* m = reinterpret_cast<half*>(arg);
  gpr_mu_lock(&m->parent->mu);
  GPR_ASSERT(!m->parent->channel_effects->actions.empty());
  if (m->parent->halves == 0) {
    gpr_mu_unlock(&m->parent->mu);
    grpc_passthru_endpoint_destroy(m->parent);
    return;
  }
  auto curr_action = m->parent->channel_effects->actions[0];
  m->parent->channel_effects->actions.erase(
      m->parent->channel_effects->actions.begin());
  m->parent->channel_effects->allowed_read_bytes +=
      curr_action.add_n_readable_bytes;
  m->parent->channel_effects->allowed_write_bytes +=
      curr_action.add_n_writable_bytes;
  flush_pending_ops_locked(m, error);
  flush_pending_ops_locked(other_half(m), error);
  sched_next_channel_action_locked(m);
  gpr_mu_unlock(&m->parent->mu);
}

static void sched_next_channel_action_locked(half* m) {
  if (m->parent->channel_effects->actions.empty()) {
    grpc_error_handle err = GRPC_ERROR_CREATE("Channel actions complete");
    shutdown_locked(m, err);
    return;
  }
  m->parent->channel_effects->timer_handle = GetDefaultEventEngine()->RunAfter(
      grpc_core::Duration::Milliseconds(
          m->parent->channel_effects->actions[0].wait_ms),
      [m] {
        grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
        grpc_core::ExecCtx exec_ctx;
        do_next_sched_channel_action(m, absl::OkStatus());
      });
}

void start_scheduling_grpc_passthru_endpoint_channel_effects(
    grpc_endpoint* ep,
    const std::vector<grpc_passthru_endpoint_channel_action>& actions) {
  half* m = reinterpret_cast<half*>(ep);
  gpr_mu_lock(&m->parent->mu);
  if (!m->parent->simulate_channel_actions || m->parent->shutdown) {
    gpr_mu_unlock(&m->parent->mu);
    return;
  }
  m->parent->channel_effects->actions = actions;
  sched_next_channel_action_locked(m);
  gpr_mu_unlock(&m->parent->mu);
}
