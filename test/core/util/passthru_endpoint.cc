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
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::grpc_get_wrapped_event_engine_endpoint;
using ::grpc_event_engine::experimental::SliceBuffer;

// DO NOT SUBMIT(hork): NOTES
// * me_get_peer was wrong

typedef struct passthru_endpoint passthru_endpoint;

class HalfEndpoint;

struct pending_op {
  bool is_armed = false;
  HalfEndpoint* ep = nullptr;
  SliceBuffer* slices = nullptr;
  absl::AnyInvocable<void(absl::Status)> cb;
};

// TODO(hork):
typedef struct {
  absl::optional<EventEngine::TaskHandle> timer_handle;
  uint64_t allowed_write_bytes;
  uint64_t allowed_read_bytes;
  std::vector<grpc_passthru_endpoint_channel_action> actions;
} grpc_passthru_endpoint_channel_effects;

struct SharedEndpointState : std::enable_shared_from_this<SharedEndpointState> {
  SharedEndpointState(grpc_passthru_endpoint_stats* stats_arg,
                      bool simulate_channel_actions_arg)
      : channel_effects(new grpc_passthru_endpoint_channel_effects()),
        simulate_channel_actions(simulate_channel_actions_arg),
        event_engine(GetDefaultEventEngine()) {
    if (stats_arg == nullptr) {
      stats = grpc_passthru_endpoint_stats_create();
    } else {
      gpr_ref(&stats_arg->refs);
      stats = stats_arg;
    }
    if (!simulate_channel_actions) {
      channel_effects->allowed_read_bytes = UINT64_MAX;
      channel_effects->allowed_write_bytes = UINT64_MAX;
    }
  }

  ~SharedEndpointState() {
    grpc_passthru_endpoint_stats_destroy(stats);
    delete channel_effects;
  }

  void DoNextSchedChannelAction(absl::Status error) ABSL_LOCKS_EXCLUDED(mu);
  void SchedNextChannelActionLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu) {
    if (channel_effects->actions.empty()) {
      grpc_error_handle err = GRPC_ERROR_CREATE("Channel actions complete");
      shutdown_locked(m, err);
      return;
    }
    channel_effects->timer_handle = event_engine->RunAfter(
        grpc_core::Duration::Milliseconds(channel_effects->actions[0].wait_ms),
        [this] {
          grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
          grpc_core::ExecCtx exec_ctx;
          DoNextSchedChannelAction(absl::OkStatus());
        });
  }

  grpc_core::Mutex mu;
  bool shutdown = false;
  grpc_passthru_endpoint_stats* stats = nullptr;
  grpc_passthru_endpoint_channel_effects* channel_effects = nullptr;
  bool simulate_channel_actions;
  grpc_endpoint* client;
  grpc_endpoint* server;
  // Easy accessors. Pointers are owned by the grpc_endpoints above
  HalfEndpoint* half_endpoint_client;
  HalfEndpoint* half_endpoint_server;
  std::shared_ptr<EventEngine> event_engine;
};

class HalfEndpoint : public EventEngine::Endpoint {
 public:
  explicit HalfEndpoint(absl::string_view name,
                        std::shared_ptr<SharedEndpointState> shared_state)
      : shared_state_(std::move(shared_state)),
        local_addr_uri_(absl::StrFormat("fake:passthru_endpoint_%s_%p", name,
                                        shared_state_.get())) {
    auto addr =
        grpc_event_engine::experimental::URIToResolvedAddress(local_addr_uri_);
    if (!addr.ok()) {
      grpc_core::Crash("Invalid ResolvedAddress URI: %s",
                       local_addr_uri_.c_str());
    }
    local_resolved_addr_ = *addr;
  }

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const ReadArgs* args) override {
    grpc_core::MutexLock lock(&shared_state_->mu);
    if (shared_state_->shutdown) {
      shared_state_->event_engine->Run(
          [on_read = std::move(on_read)]() mutable {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            on_read(GRPC_ERROR_CREATE("Already shutdown"));
          });
      return false;
    }
    GPR_ASSERT(!pending_read_op_.is_armed);
    if (read_buffer_.Count() > 0) {
      GPR_ASSERT(!on_read_);
      pending_read_op_.is_armed = true;
      pending_read_op_.cb = std::move(on_read);
      pending_read_op_.ep = this;
      pending_read_op_.slices = buffer;
      DoPendingReadOpLocked(absl::OkStatus());
    } else {
      on_read_ = std::move(on_read);
      on_read_out_ = buffer;
    }
    return false;
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const WriteArgs* args) override {
    grpc_core::Crash("DO NOT SUBMIT - implement");
  }

  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    return GetOther()->GetLocalAddress();
  }

  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    return local_resolved_addr_;
  }

  // Custom Methods

  const HalfEndpoint* GetOther() const {
    if (shared_state_->half_endpoint_client == this)
      return shared_state_->half_endpoint_server;
    return shared_state_->half_endpoint_client;
  }

  void FlushPendingOpsLocked(absl::Status error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(shared_state_->mu) {
    if (pending_read_op_.is_armed) DoPendingReadOpLocked(error);
    if (pending_write_op_.is_armed) DoPendingWriteOpLocked(error);
  }

  void DoPendingReadOpLocked(absl::Status error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(shared_state_->mu) {
    GPR_ASSERT(pending_read_op_.is_armed);
    GPR_ASSERT(bytes_read_so_far_ <=
               shared_state_->channel_effects->allowed_read_bytes);
    if (shared_state_->shutdown) {
      shared_state_->event_engine->Run(
          [cb = std::move(pending_read_op_.cb)]() mutable {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            cb(GRPC_ERROR_CREATE("Already shutdown"));
          });
      // Move any pending data into pending_read_op.slices so that it may be
      // free'ed by the executing callback.

      // DO NOT SUBMIT(hork): EE slice ops
      grpc_slice_buffer_move_into(&read_buffer_, pending_read_op_.slices);
      pending_read_op_.is_armed = false;
      return;
    }
    if (bytes_read_so_far_ ==
        shared_state_->channel_effects->allowed_read_bytes) {
      // Keep it in pending state.
      return;
    }
    // This delayed processing should only be invoked when read_buffer has
    // something in it.
    GPR_ASSERT(read_buffer_.Count() > 0);
    uint64_t readable_length =
        std::min<uint64_t>(read_buffer_.Length(),
                           shared_state_->channel_effects->allowed_read_bytes -
                               bytes_read_so_far_);
    GPR_ASSERT(readable_length > 0);
    // DO NOT SUBMIT(hork): EE slice ops
    grpc_slice_buffer_move_first(&read_buffer_, readable_length,
                                 pending_read_op_.slices);
    shared_state_->event_engine->Run(
        [cb = std::move(pending_read_op_.cb), error]() mutable {
          grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
          grpc_core::ExecCtx exec_ctx;
          cb(error);
        });
    if (shared_state_->simulate_channel_actions) {
      bytes_read_so_far_ += readable_length;
    }
    pending_read_op_.is_armed = false;
  }

  void DoPendingWriteOpLocked(absl::Status error) {
    grpc_core::Crash("DO NOT SUBMIT - implement");
  }

 private:
  std::shared_ptr<SharedEndpointState> shared_state_;
  SliceBuffer read_buffer_;
  SliceBuffer write_buffer_;
  SliceBuffer* on_read_out_ = nullptr;
  absl::AnyInvocable<void(absl::Status)> on_read_;
  pending_op pending_read_op_;
  pending_op pending_write_op_;
  uint64_t bytes_read_so_far_ = 0;
  uint64_t bytes_written_so_far_ = 0;
  std::string local_addr_uri_;
  EventEngine::ResolvedAddress local_resolved_addr_;
};

// ---- SharedEndpointState implementation --------------------------------

void SharedEndpointState::DoNextSchedChannelAction(absl::Status error) {
  grpc_core::MutexLock lock(&mu);
  GPR_ASSERT(channel_effects->actions.empty());
  auto curr_action = channel_effects->actions[0];
  channel_effects->actions.erase(channel_effects->actions.begin());
  channel_effects->allowed_read_bytes += curr_action.add_n_readable_bytes;
  channel_effects->allowed_write_bytes += curr_action.add_n_writable_bytes;
  static_cast<HalfEndpoint*>(grpc_get_wrapped_event_engine_endpoint(client))
      ->FlushPendingOpsLocked(error);
  static_cast<HalfEndpoint*>(grpc_get_wrapped_event_engine_endpoint(server))
      ->FlushPendingOpsLocked(error);
  SchedNextChannelActionLocked();
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

// DO NOT SUBMIT(hork): this needs to move to ~HalfEndpoint
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

static const grpc_endpoint_vtable vtable = {
    me_write,
};

// DO NOT SUBMIT(hork): good
void grpc_passthru_endpoint_create(grpc_endpoint** client,
                                   grpc_endpoint** server,
                                   grpc_passthru_endpoint_stats* stats,
                                   bool simulate_channel_actions) {
  auto shared_endpoint_state =
      std::make_shared<SharedEndpointState>(stats, simulate_channel_actions);
  auto* half_client = grpc_event_engine_endpoint_create(
      std::make_unique<HalfEndpoint>("client", shared_endpoint_state));
  auto* half_server = grpc_event_engine_endpoint_create(
      std::make_unique<HalfEndpoint>("server", shared_endpoint_state));
  shared_endpoint_state->client = half_client;
  shared_endpoint_state->server = half_server;
  *client = half_client;
  *server = half_server;
}

// DO NOT SUBMIT(hork): good
grpc_passthru_endpoint_stats* grpc_passthru_endpoint_stats_create() {
  grpc_passthru_endpoint_stats* stats =
      static_cast<grpc_passthru_endpoint_stats*>(
          gpr_malloc(sizeof(grpc_passthru_endpoint_stats)));
  memset(stats, 0, sizeof(*stats));
  gpr_ref_init(&stats->refs, 1);
  return stats;
}

// DO NOT SUBMIT(hork): good
void grpc_passthru_endpoint_stats_destroy(grpc_passthru_endpoint_stats* stats) {
  if (gpr_unref(&stats->refs)) {
    gpr_free(stats);
  }
}
