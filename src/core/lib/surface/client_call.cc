// Copyright 2024 gRPC authors.
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

#include "src/core/lib/surface/client_call.h"

#include <grpc/byte_buffer.h>
#include <grpc/compression.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/call.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/promise/all_ok.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/bitset.h"
#include "src/core/util/crash.h"
#include "src/core/util/latent_see.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

namespace {

grpc_call_error ValidateClientBatch(const grpc_op* ops, size_t nops) {
  BitSet<8> got_ops;
  for (size_t op_idx = 0; op_idx < nops; op_idx++) {
    const grpc_op& op = ops[op_idx];
    switch (op.op) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        if (!AreInitialMetadataFlagsValid(op.flags)) {
          return GRPC_CALL_ERROR_INVALID_FLAGS;
        }
        if (!ValidateMetadata(op.data.send_initial_metadata.count,
                              op.data.send_initial_metadata.metadata)) {
          return GRPC_CALL_ERROR_INVALID_METADATA;
        }
        break;
      case GRPC_OP_SEND_MESSAGE:
        if (!AreWriteFlagsValid(op.flags)) {
          return GRPC_CALL_ERROR_INVALID_FLAGS;
        }
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
      case GRPC_OP_RECV_INITIAL_METADATA:
      case GRPC_OP_RECV_MESSAGE:
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        if (op.flags != 0) return GRPC_CALL_ERROR_INVALID_FLAGS;
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        return GRPC_CALL_ERROR_NOT_ON_CLIENT;
    }
    if (got_ops.is_set(op.op)) return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
    got_ops.set(op.op);
  }
  return GRPC_CALL_OK;
}

}  // namespace

ClientCall::ClientCall(grpc_call*, uint32_t, grpc_completion_queue* cq,
                       Slice path, absl::optional<Slice> authority,
                       bool registered_method, Timestamp deadline,
                       grpc_compression_options compression_options,
                       RefCountedPtr<Arena> arena,
                       RefCountedPtr<UnstartedCallDestination> destination)
    : Call(false, deadline, std::move(arena)),
      DualRefCounted("ClientCall"),
      cq_(cq),
      call_destination_(std::move(destination)),
      compression_options_(compression_options) {
  global_stats().IncrementClientCallsCreated();
  send_initial_metadata_->Set(HttpPathMetadata(), std::move(path));
  if (authority.has_value()) {
    send_initial_metadata_->Set(HttpAuthorityMetadata(), std::move(*authority));
  }
  send_initial_metadata_->Set(
      GrpcRegisteredMethod(),
      reinterpret_cast<void*>(static_cast<uintptr_t>(registered_method)));
  if (deadline != Timestamp::InfFuture()) {
    send_initial_metadata_->Set(GrpcTimeoutMetadata(), deadline);
    UpdateDeadline(deadline);
  }
}

grpc_call_error ClientCall::StartBatch(const grpc_op* ops, size_t nops,
                                       void* notify_tag,
                                       bool is_notify_tag_closure) {
  GRPC_LATENT_SEE_PARENT_SCOPE("ClientCall::StartBatch");
  if (nops == 0) {
    EndOpImmediately(cq_, notify_tag, is_notify_tag_closure);
    return GRPC_CALL_OK;
  }
  const grpc_call_error validation_result = ValidateClientBatch(ops, nops);
  if (validation_result != GRPC_CALL_OK) {
    return validation_result;
  }
  CommitBatch(ops, nops, notify_tag, is_notify_tag_closure);
  return GRPC_CALL_OK;
}

void ClientCall::CancelWithError(grpc_error_handle error) {
  cancel_status_.Set(new absl::Status(error));
  auto cur_state = call_state_.load(std::memory_order_acquire);
  while (true) {
    GRPC_TRACE_LOG(call, INFO)
        << DebugTag() << "CancelWithError " << GRPC_DUMP_ARGS(cur_state, error);
    switch (cur_state) {
      case kCancelled:
        return;
      case kUnstarted:
        if (call_state_.compare_exchange_strong(cur_state, kCancelled,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
          ResetDeadline();
          return;
        }
        break;
      case kStarted:
        started_call_initiator_.SpawnInfallible(
            "CancelWithError", [self = WeakRefAsSubclass<ClientCall>(),
                                error = std::move(error)]() mutable {
              self->started_call_initiator_.Cancel(std::move(error));
              return Empty{};
            });
        return;
      default:
        if (call_state_.compare_exchange_strong(cur_state, kCancelled,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
          ResetDeadline();
          auto* unordered_start = reinterpret_cast<UnorderedStart*>(cur_state);
          while (unordered_start != nullptr) {
            auto next = unordered_start->next;
            delete unordered_start;
            unordered_start = next;
          }
          return;
        }
    }
  }
}

template <typename Batch>
void ClientCall::ScheduleCommittedBatch(Batch batch) {
  auto cur_state = call_state_.load(std::memory_order_acquire);
  while (true) {
    switch (cur_state) {
      case kUnstarted:
      default: {  // UnorderedStart
        auto pending = std::make_unique<UnorderedStart>();
        pending->start_pending_batch = [this,
                                        batch = std::move(batch)]() mutable {
          started_call_initiator_.SpawnInfallible("batch", std::move(batch));
        };
        while (true) {
          pending->next = reinterpret_cast<UnorderedStart*>(cur_state);
          if (call_state_.compare_exchange_strong(
                  cur_state, reinterpret_cast<uintptr_t>(pending.get()),
                  std::memory_order_acq_rel, std::memory_order_acquire)) {
            std::ignore = pending.release();
            return;
          }
          if (cur_state == kStarted) {
            pending->start_pending_batch();
            return;
          }
          if (cur_state == kCancelled) {
            return;
          }
        }
      }
      case kStarted:
        started_call_initiator_.SpawnInfallible("batch", std::move(batch));
        return;
      case kCancelled:
        return;
    }
  }
}

void ClientCall::StartCall(const grpc_op& send_initial_metadata_op) {
  auto cur_state = call_state_.load(std::memory_order_acquire);
  CToMetadata(send_initial_metadata_op.data.send_initial_metadata.metadata,
              send_initial_metadata_op.data.send_initial_metadata.count,
              send_initial_metadata_.get());
  PrepareOutgoingInitialMetadata(send_initial_metadata_op,
                                 *send_initial_metadata_);
  auto call = MakeCallPair(std::move(send_initial_metadata_), arena()->Ref());
  started_call_initiator_ = std::move(call.initiator);
  while (true) {
    GRPC_TRACE_LOG(call, INFO)
        << DebugTag() << "StartCall " << GRPC_DUMP_ARGS(cur_state);
    switch (cur_state) {
      case kUnstarted:
        if (call_state_.compare_exchange_strong(cur_state, kStarted,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
          call_destination_->StartCall(std::move(call.handler));
          return;
        }
        break;
      case kStarted:
        Crash("StartCall called twice");  // probably we crash earlier...
      case kCancelled:
        return;
      default: {  // UnorderedStart
        if (call_state_.compare_exchange_strong(cur_state, kStarted,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
          call_destination_->StartCall(std::move(call.handler));
          auto unordered_start = reinterpret_cast<UnorderedStart*>(cur_state);
          while (unordered_start->next != nullptr) {
            unordered_start->start_pending_batch();
            auto next = unordered_start->next;
            delete unordered_start;
            unordered_start = next;
          }
          return;
        }
        break;
      }
    }
  }
}

void ClientCall::CommitBatch(const grpc_op* ops, size_t nops, void* notify_tag,
                             bool is_notify_tag_closure) {
  if (nops == 1 && ops[0].op == GRPC_OP_SEND_INITIAL_METADATA) {
    StartCall(ops[0]);
    EndOpImmediately(cq_, notify_tag, is_notify_tag_closure);
    return;
  }
  if (!is_notify_tag_closure) grpc_cq_begin_op(cq_, notify_tag);
  BatchOpIndex op_index(ops, nops);
  auto send_message =
      op_index.OpHandler<GRPC_OP_SEND_MESSAGE>([this](const grpc_op& op) {
        SliceBuffer send;
        grpc_slice_buffer_swap(
            &op.data.send_message.send_message->data.raw.slice_buffer,
            send.c_slice_buffer());
        auto msg = arena()->MakePooled<Message>(std::move(send), op.flags);
        return [this, msg = std::move(msg)]() mutable {
          return started_call_initiator_.PushMessage(std::move(msg));
        };
      });
  auto send_close_from_client =
      op_index.OpHandler<GRPC_OP_SEND_CLOSE_FROM_CLIENT>(
          [this](const grpc_op&) {
            return [this]() {
              started_call_initiator_.FinishSends();
              return Success{};
            };
          });
  auto recv_message =
      op_index.OpHandler<GRPC_OP_RECV_MESSAGE>([this](const grpc_op& op) {
        return message_receiver_.MakeBatchOp(op, &started_call_initiator_);
      });
  auto recv_initial_metadata =
      op_index.OpHandler<GRPC_OP_RECV_INITIAL_METADATA>([this](
                                                            const grpc_op& op) {
        return [this,
                array = op.data.recv_initial_metadata.recv_initial_metadata]() {
          return Map(
              started_call_initiator_.PullServerInitialMetadata(),
              [this,
               array](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
                ServerMetadataHandle metadata;
                if (!md.ok() || !md->has_value()) {
                  is_trailers_only_ = true;
                  metadata = Arena::MakePooledForOverwrite<ServerMetadata>();
                } else {
                  metadata = std::move(md->value());
                  is_trailers_only_ =
                      metadata->get(GrpcTrailersOnly()).value_or(false);
                }
                ProcessIncomingInitialMetadata(*metadata);
                PublishMetadataArray(metadata.get(), array, true);
                received_initial_metadata_ = std::move(metadata);
                return Success{};
              });
        };
      });
  auto primary_ops = AllOk<StatusFlag>(
      TrySeq(std::move(send_message), std::move(send_close_from_client)),
      TrySeq(std::move(recv_initial_metadata), std::move(recv_message)));
  if (const grpc_op* op = op_index.op(GRPC_OP_SEND_INITIAL_METADATA)) {
    StartCall(*op);
  }
  if (const grpc_op* op = op_index.op(GRPC_OP_RECV_STATUS_ON_CLIENT)) {
    auto out_status = op->data.recv_status_on_client.status;
    auto out_status_details = op->data.recv_status_on_client.status_details;
    auto out_error_string = op->data.recv_status_on_client.error_string;
    auto out_trailing_metadata =
        op->data.recv_status_on_client.trailing_metadata;
    auto make_read_trailing_metadata = [this, out_status, out_status_details,
                                        out_error_string,
                                        out_trailing_metadata]() {
      return Map(
          started_call_initiator_.PullServerTrailingMetadata(),
          [this, out_status, out_status_details, out_error_string,
           out_trailing_metadata](
              ServerMetadataHandle server_trailing_metadata) {
            saw_trailing_metadata_.store(true, std::memory_order_relaxed);
            ResetDeadline();
            GRPC_TRACE_LOG(call, INFO)
                << DebugTag() << "RecvStatusOnClient "
                << server_trailing_metadata->DebugString();
            const auto status =
                server_trailing_metadata->get(GrpcStatusMetadata())
                    .value_or(GRPC_STATUS_UNKNOWN);
            *out_status = status;
            Slice message_slice;
            if (Slice* message = server_trailing_metadata->get_pointer(
                    GrpcMessageMetadata())) {
              message_slice = message->Ref();
            }
            *out_status_details = message_slice.TakeCSlice();
            if (out_error_string != nullptr) {
              if (status != GRPC_STATUS_OK) {
                *out_error_string = gpr_strdup(
                    MakeErrorString(server_trailing_metadata.get()).c_str());
              } else {
                *out_error_string = nullptr;
              }
            }
            PublishMetadataArray(server_trailing_metadata.get(),
                                 out_trailing_metadata, true);
            received_trailing_metadata_ = std::move(server_trailing_metadata);
            return Success{};
          });
    };
    ScheduleCommittedBatch(InfallibleBatch(
        std::move(primary_ops),
        OpHandler<GRPC_OP_RECV_STATUS_ON_CLIENT>(OnCancelFactory(
            std::move(make_read_trailing_metadata),
            [this, out_status, out_status_details, out_error_string,
             out_trailing_metadata]() {
              auto* status = cancel_status_.Get();
              CHECK_NE(status, nullptr);
              *out_status = static_cast<grpc_status_code>(status->code());
              *out_status_details =
                  Slice::FromCopiedString(status->message()).TakeCSlice();
              if (out_error_string != nullptr) {
                *out_error_string = nullptr;
              }
              out_trailing_metadata->count = 0;
            })),
        is_notify_tag_closure, notify_tag, cq_));
  } else {
    ScheduleCommittedBatch(FallibleBatch(
        std::move(primary_ops), is_notify_tag_closure, notify_tag, cq_));
  }
}

char* ClientCall::GetPeer() {
  Slice peer_slice = GetPeerString();
  if (!peer_slice.empty()) {
    absl::string_view peer_string_view = peer_slice.as_string_view();
    char* peer_string =
        static_cast<char*>(gpr_malloc(peer_string_view.size() + 1));
    memcpy(peer_string, peer_string_view.data(), peer_string_view.size());
    peer_string[peer_string_view.size()] = '\0';
    return peer_string;
  }
  return gpr_strdup("unknown");
}

grpc_call* MakeClientCall(grpc_call* parent_call, uint32_t propagation_mask,
                          grpc_completion_queue* cq, Slice path,
                          absl::optional<Slice> authority,
                          bool registered_method, Timestamp deadline,
                          grpc_compression_options compression_options,
                          RefCountedPtr<Arena> arena,
                          RefCountedPtr<UnstartedCallDestination> destination) {
  DCHECK_NE(arena.get(), nullptr);
  DCHECK_NE(arena->GetContext<grpc_event_engine::experimental::EventEngine>(),
            nullptr);
  return arena
      ->New<ClientCall>(parent_call, propagation_mask, cq, std::move(path),
                        std::move(authority), registered_method, deadline,
                        compression_options, arena, destination)
      ->c_ptr();
}

}  // namespace grpc_core
