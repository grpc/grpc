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
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::grpc_get_wrapped_event_engine_endpoint;
using ::grpc_event_engine::experimental::Slice;
using ::grpc_event_engine::experimental::SliceBuffer;

// DO NOT SUBMIT(hork): NOTES
// * me_get_peer was wrong

typedef struct passthru_endpoint passthru_endpoint;

namespace {

// TODO(hork): C++ify
// Copy src slice and split the copy at n bytes into two separate slices
void SliceCopyAndSplit(const Slice& src, uint64_t n, Slice& split1,
                       Slice& split2) {
  GPR_ASSERT(n <= src.length());
  if (n == src.length()) {
    split1 = src.Copy();
    split2 = Slice();
    return;
  }
  split1 = src.Copy();
  split2 = split2.Split(n);
}

enum class EndpointType { client, server };

class HalfEndpoint;

EventEngine::ResolvedAddress MakeHalfEndpointResolverAddress(const char* name,
                                                             void* parent) {
  const auto addr = grpc_event_engine::experimental::URIToResolvedAddress(
      absl::StrFormat("unix-abstract:passthru_endpoint_%s_%p", name, parent));
  if (!addr.ok()) {
    grpc_core::Crash("Invalid ResolvedAddress URI: %s",
                     addr.status().ToString().c_str());
  }
  return *addr;
}

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
    client_addr = MakeHalfEndpointResolverAddress("client", this);
    server_addr = MakeHalfEndpointResolverAddress("server", this);
  }

  ~SharedEndpointState() {
    grpc_passthru_endpoint_stats_destroy(stats);
    delete channel_effects;
  }

  void DoNextSchedChannelAction(absl::Status error) ABSL_LOCKS_EXCLUDED(mu);
  void SchedNextChannelActionLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu);

  struct grpc_passthru_endpoint_channel_effects {
    absl::optional<EventEngine::TaskHandle> timer_handle;
    uint64_t allowed_write_bytes;
    uint64_t allowed_read_bytes;
    std::vector<grpc_passthru_endpoint_channel_action> actions;
  };

  EventEngine::ResolvedAddress& GetEndpointResolvedAddress(EndpointType type) {
    if (type == EndpointType::client) return client_addr;
    return server_addr;
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
  EventEngine::ResolvedAddress client_addr;
  HalfEndpoint* half_endpoint_server;
  EventEngine::ResolvedAddress server_addr;
  std::shared_ptr<EventEngine> event_engine;
};

class HalfEndpoint : public EventEngine::Endpoint {
 public:
  explicit HalfEndpoint(EndpointType endpoint_type,
                        std::shared_ptr<SharedEndpointState> shared_state)
      : endpoint_type_(endpoint_type), shared_state_(std::move(shared_state)) {}

  ~HalfEndpoint() override {
    grpc_core::MutexLock lock(&shared_state_->mu);
    shared_state_->shutdown = true;
    FlushPendingOpsLocked(absl::OkStatus());
    if (on_read_) {
      shared_state_->event_engine->Run(
          [on_read = std::move(on_read_)]() mutable {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            on_read(GRPC_ERROR_CREATE("Shutdown"));
          });
    }
  }

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const ReadArgs* /* args */)
      ABSL_LOCKS_EXCLUDED(shared_state_->mu) override {
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
             SliceBuffer* data, const WriteArgs* /* args */) override {
    grpc_core::MutexLock lock(&shared_state_->mu);
    gpr_atm_full_fetch_add(&shared_state_->stats->num_writes, (gpr_atm)1);
    if (shared_state_->shutdown) {
      shared_state_->event_engine->Run(
          [on_writable = std::move(on_writable)]() mutable {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            on_writable(GRPC_ERROR_CREATE("Endpoint already shutdown"));
          });
      return false;
    }
    GPR_ASSERT(!pending_write_op_.is_armed);
    // Copy slices into m->pending_write_op.slices
    pending_write_op_.slices = &write_buffer_;
    GPR_ASSERT(pending_write_op_.slices->Count() == 0);
    for (int i = 0; i < static_cast<int>(data->Count()); i++) {
      auto& slice = data->MutableSliceAt(i);
      if (slice.length() > 0) {
        pending_write_op_.slices->AppendIndexed(slice.Copy());
      }
    }
    if (pending_write_op_.slices->Count() > 0) {
      pending_write_op_.is_armed = true;
      pending_write_op_.cb = std::move(on_writable);
      pending_write_op_.ep = this;
      DoPendingWriteOpLocked(absl::OkStatus());
    } else {
      // There is nothing to write. Schedule callback to be run right away.
      shared_state_->event_engine->Run(
          [on_writable = std::move(on_writable)]() mutable {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            on_writable(GRPC_ERROR_CREATE("Already shutdown"));
          });
    }
    return false;
  }

  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    if (endpoint_type_ == EndpointType::client) {
      return shared_state_->GetEndpointResolvedAddress(EndpointType::server);
    }
    return shared_state_->GetEndpointResolvedAddress(EndpointType::client);
  }

  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    return shared_state_->GetEndpointResolvedAddress(endpoint_type_);
  }

  // Custom Methods

  HalfEndpoint* GetOther() const {
    if (shared_state_->half_endpoint_client == this) {
      return shared_state_->half_endpoint_server;
    }
    return shared_state_->half_endpoint_client;
  }

  // TODO(b/7273178): ABSL_EXCLUSIVE_LOCKS_REQUIRED(shared_state_->mu)
  void FlushPendingOpsLocked(absl::Status error) {
    if (pending_read_op_.is_armed) DoPendingReadOpLocked(error);
    if (pending_write_op_.is_armed) DoPendingWriteOpLocked(error);
  }

  // TODO(b/7273178): ABSL_EXCLUSIVE_LOCKS_REQUIRED(shared_state_->mu)
  void DoPendingReadOpLocked(absl::Status error) {
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
      read_buffer_.TakeAndAppend(*pending_read_op_.slices);
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
    read_buffer_.MoveFirstNBytesIntoSliceBuffer(readable_length,
                                                *pending_read_op_.slices);
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
    GPR_ASSERT(pending_write_op_.is_armed);
    GPR_ASSERT(bytes_written_so_far_ <=
               shared_state_->channel_effects->allowed_write_bytes);
    if (shared_state_->shutdown) {
      shared_state_->event_engine->Run(
          [cb = std::move(pending_write_op_.cb)]() mutable {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            cb(GRPC_ERROR_CREATE("Already shutdown"));
          });
      pending_write_op_.cb = nullptr;
      pending_write_op_.is_armed = false;
      pending_write_op_.slices->Clear();
      return;
    }
    if (bytes_written_so_far_ ==
        shared_state_->channel_effects->allowed_write_bytes) {
      // Keep it in pending state.
      return;
    }

    HalfEndpoint* other = GetOther();
    uint64_t max_writable =
        std::min<uint64_t>(pending_write_op_.slices->Length(),
                           shared_state_->channel_effects->allowed_write_bytes -
                               bytes_written_so_far_);
    uint64_t max_readable = shared_state_->channel_effects->allowed_read_bytes -
                            other->bytes_read_so_far_;
    uint64_t immediate_bytes_read =
        other->on_read_ ? std::min<uint64_t>(max_readable, max_writable) : 0;

    GPR_ASSERT(max_writable > 0);
    GPR_ASSERT(max_readable >= 0);
    // At the end of this process, we should have written max_writable bytes;
    if (shared_state_->simulate_channel_actions) {
      bytes_written_so_far_ += max_writable;
    }
    // Estimate if the original write would still be pending at the end of
    // this process
    bool would_write_be_pending =
        max_writable < pending_write_op_.slices->Length();
    if (!shared_state_->simulate_channel_actions) {
      GPR_ASSERT(!would_write_be_pending);
    }
    SliceBuffer* slices = pending_write_op_.slices;
    SliceBuffer* dest =
        other->on_read_ ? other->on_read_out_ : &other->read_buffer_;
    while (max_writable > 0) {
      const Slice slice = slices->TakeFirst();
      uint64_t slice_length = slice.length();
      GPR_ASSERT(slice_length > 0);
      Slice split1, split2;
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
        dest = &other->read_buffer_;
        split_length = std::min<uint64_t>(slice_length, max_writable);
      }

      SliceCopyAndSplit(slice, split_length, split1, split2);
      // Write a copy of the slice to the destination to be read
      dest->AppendIndexed(std::move(split1));
      // Re-insert split2 into source for next iteration.
      if (split2.length() > 0) {
        slices->Prepend(std::move(split2));
      }
      if (max_readable > 0) {
        GPR_ASSERT(max_readable >= static_cast<uint64_t>(split_length));
        max_readable -= split_length;
      }

      GPR_ASSERT(max_writable >= static_cast<uint64_t>(split_length));
      max_writable -= split_length;
    }

    if (immediate_bytes_read > 0) {
      GPR_ASSERT(!other->pending_read_op_.is_armed);
      if (shared_state_->simulate_channel_actions) {
        other->bytes_read_so_far_ += immediate_bytes_read;
      }
      shared_state_->event_engine->Run(
          [on_read = std::move(other->on_read_), error]() mutable {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            on_read(error);
          });
      other->on_read_ = nullptr;
    }

    if (!would_write_be_pending) {
      // No slices should be left
      GPR_ASSERT(pending_write_op_.slices->Count() == 0);
      pending_write_op_.slices->Clear();
      pending_write_op_.is_armed = false;
      shared_state_->event_engine->Run(
          [cb = std::move(pending_write_op_.cb), error]() mutable {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            cb(error);
          });
      pending_write_op_.cb = nullptr;
    }
  }

 private:
  struct pending_op {
    bool is_armed = false;
    HalfEndpoint* ep = nullptr;
    SliceBuffer* slices = nullptr;
    absl::AnyInvocable<void(absl::Status)> cb;
  };

  EndpointType endpoint_type_;
  std::shared_ptr<SharedEndpointState> shared_state_;
  SliceBuffer read_buffer_;
  SliceBuffer write_buffer_;
  SliceBuffer* on_read_out_ = nullptr;
  absl::AnyInvocable<void(absl::Status)> on_read_;
  pending_op pending_read_op_;
  pending_op pending_write_op_;
  uint64_t bytes_read_so_far_ = 0;
  uint64_t bytes_written_so_far_ = 0;
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

void SharedEndpointState::SchedNextChannelActionLocked()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu) {
  if (channel_effects->actions.empty()) {
    // DO NOT SUBMIT(hork): do we need to shutdown here somehow?
    // grpc_error_handle err = GRPC_ERROR_CREATE("Channel actions complete");
    // shutdown_locked(m, err);
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

}  // namespace

void grpc_passthru_endpoint_create(grpc_endpoint** client,
                                   grpc_endpoint** server,
                                   grpc_passthru_endpoint_stats* stats,
                                   bool simulate_channel_actions) {
  auto shared_endpoint_state =
      std::make_shared<SharedEndpointState>(stats, simulate_channel_actions);
  // Client setup
  shared_endpoint_state->client =
      grpc_event_engine_endpoint_create(std::make_unique<HalfEndpoint>(
          EndpointType::client, shared_endpoint_state));
  shared_endpoint_state->half_endpoint_client = reinterpret_cast<HalfEndpoint*>(
      grpc_get_wrapped_event_engine_endpoint(shared_endpoint_state->client));
  *client = shared_endpoint_state->client;
  // Server setup
  shared_endpoint_state->server =
      grpc_event_engine_endpoint_create(std::make_unique<HalfEndpoint>(
          EndpointType::server, shared_endpoint_state));
  shared_endpoint_state->half_endpoint_server = reinterpret_cast<HalfEndpoint*>(
      grpc_get_wrapped_event_engine_endpoint(shared_endpoint_state->server));
  *server = shared_endpoint_state->server;
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
