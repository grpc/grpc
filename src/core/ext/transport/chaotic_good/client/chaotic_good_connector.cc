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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chaotic_good/client/chaotic_good_connector.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/promise/wait_for_callback.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/handshaker.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {
using grpc_event_engine::experimental::EventEngine;
namespace {
void MaybeNotify(const DebugLocation& location, grpc_closure* notify,
                 grpc_error_handle error) {
  if (notify != nullptr) {
    ExecCtx exec_ctx;
    ExecCtx::Run(location, notify, error);
  }
}
}  // namespace
ChaoticGoodConnector::ChaoticGoodConnector()
    : channel_args_(ChannelArgs()),
      memory_quota_(std::make_unique<MemoryQuota>("chaotic_good_connector")),
      resource_quota_(ResourceQuota::Default()),
      memory_allocator_(
          memory_quota_->CreateMemoryAllocator("chaotic_good_connector")),
      arena_(MakeScopedArena(initial_arena_size, &memory_allocator_)),
      timeout_(EventEngine::Duration(60)),
      context_(
          std::make_shared<promise_detail::Context<Arena>>((arena_.get()))),
      event_engine_(grpc_event_engine::experimental::CreateEventEngine()),
      handshake_mgr_(std::make_shared<HandshakeManager>()),
      data_endpoint_latch_(
          std::make_shared<Latch<std::shared_ptr<PromiseEndpoint>>>()),
      wait_for_data_endpoint_callback_(std::make_shared<WaitForCallback>()) {
  channel_args_ = channel_args_.SetObject(event_engine_);
  channel_args_ = channel_args_.Set(GRPC_ARG_RESOURCE_QUOTA, resource_quota_);
  ee_config_ =
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(channel_args_);
}
ChaoticGoodConnector::~ChaoticGoodConnector() {
  if (connect_activity_ != nullptr) {
    connect_activity_.reset();
  }
}

auto ChaoticGoodConnector::DataEndpointReadSettingsFrame(
    std::shared_ptr<ChaoticGoodConnector> self) {
  GPR_ASSERT(self->data_endpoint_ != nullptr);
  return TrySeq(
      self->data_endpoint_->ReadSlice(FrameHeader::frame_header_size_),
      [self = self](Slice slice) mutable {
        // Read setting frame;
        // Parse frame header
        GPR_ASSERT(self->data_endpoint_ != nullptr);
        auto frame_header_ = std::make_shared<FrameHeader>(
            FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
                                   GRPC_SLICE_START_PTR(slice.c_slice())))
                .value());
        auto frame_header_length = frame_header_->GetFrameLength();
        return self->data_endpoint_->Read(frame_header_length);
      },
      [](SliceBuffer buffer) {
        // TODO(ladynana): Initial client transport.
        return absl::OkStatus();
      });
}

auto ChaoticGoodConnector::DataEndpointWriteSettingsFrame(
    std::shared_ptr<ChaoticGoodConnector> self) {
  GPR_ASSERT(self->data_endpoint_ != nullptr);
  return TrySeq(
      [self = self]() mutable {
        // Serialize setting frame.
        SettingsFrame frame;
        // frame.header set connectiion_type: control
        ClientMetadataHandle metadata =
            self->arena_->MakePooled<ClientMetadata>(self->arena_.get());
        metadata->Set(ChaoticGoodConnectionTypeMetadata(),
                      Slice::FromCopiedString("data"));
        auto connection_type =
            metadata->get_pointer(ChaoticGoodConnectionTypeMetadata())->Ref();
        metadata->Set(ChaoticGoodConnectionIdMetadata(),
                      self->connection_id_.Ref());
        frame.headers = std::move(metadata);
        auto write_buffer = frame.Serialize(&self->hpack_compressor_);
        return self->data_endpoint_->Write(std::move(write_buffer.control));
      },
      []() { return absl::OkStatus(); });
}

auto ChaoticGoodConnector::ControlEndpointReadSettingsFrame(
    std::shared_ptr<ChaoticGoodConnector> self) {
  GPR_ASSERT(self->control_endpoint_ != nullptr);
  return TrySeq(
      [self = self]() {
        GPR_ASSERT(Activity::current() != nullptr);
        GPR_ASSERT(Activity::current() == self->connect_activity_->current());
        return Activity::current();
      },
      [self = self]() {
        return self->control_endpoint_->ReadSlice(
            FrameHeader::frame_header_size_);
      },
      [self = self](Slice slice) mutable {
        // Parse frame header
        auto frame_header = std::make_shared<FrameHeader>(
            FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
                                   GRPC_SLICE_START_PTR(slice.c_slice())))
                .value());
        return TrySeq(
            self->control_endpoint_->Read(frame_header->GetFrameLength()),
            [frame_header = frame_header, self = self](SliceBuffer buffer) {
              // Deserialize setting frame.
              SettingsFrame frame;
              // Initialized to get this_cpu() info in
              // global_stat().
              ExecCtx exec_ctx;
              absl::BitGen bitgen;
              BufferPair buffer_pair{std::move(buffer), SliceBuffer()};
              auto status = frame.Deserialize(
                  &self->hpack_parser_, *frame_header, absl::BitGenRef(bitgen),
                  self->arena_.get(), std::move(buffer_pair));
              GPR_ASSERT(status.ok());
              self->connection_id_ =
                  frame.headers->get_pointer(ChaoticGoodConnectionIdMetadata())
                      ->Ref();
              // Data endpoint on_connect callback.
              grpc_event_engine::experimental::EventEngine::OnConnectCallback
                  on_data_endpoint_connect =
                      [self = self](
                          absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>
                              endpoint) mutable {
                        self->data_endpoint_latch_->Set(
                            std::make_shared<PromiseEndpoint>(
                                std::move(endpoint.value()), SliceBuffer()));
                        auto cb = self->wait_for_data_endpoint_callback_
                                      ->MakeCallback();
                        // Wake up wait_for_data_endpoint_callback_.
                        cb();
                      };
              self->event_engine_->Connect(
                  std::move(on_data_endpoint_connect), *self->resolved_addr_,
                  self->ee_config_,
                  self->memory_quota_->CreateMemoryAllocator(
                      "data_endpoint_connection"),
                  self->timeout_);

              return self->wait_for_data_endpoint_callback_->MakeWaitPromise();
            },
            Race(TrySeq(self->data_endpoint_latch_->Wait(),
                        [self = self](std::shared_ptr<PromiseEndpoint>
                                          data_endpoint) mutable {
                          self->data_endpoint_.swap(data_endpoint);
                          return TrySeq(
                              TryJoin<absl::StatusOr>(
                                  DataEndpointReadSettingsFrame(self),
                                  DataEndpointWriteSettingsFrame(self)),
                              []() -> absl::Status {
                                return absl::OkStatus();
                              });
                        }),
                 TrySeq(Sleep(Timestamp::Now() + Duration::Seconds(5)),
                        []() -> absl::Status {
                          return absl::DeadlineExceededError(
                              "Data endpoint connect deadline excced.");
                        })),
            []() { return absl::OkStatus(); });
      });
}

auto ChaoticGoodConnector::ControlEndpointWriteSettingsFrame(
    std::shared_ptr<ChaoticGoodConnector> self) {
  return TrySeq(
      [self = self]() mutable {
        GPR_ASSERT(self->control_endpoint_ != nullptr);
        // Serialize setting frame.
        SettingsFrame frame;
        // frame.header set connectiion_type: control
        ClientMetadataHandle metadata =
            self->arena_->MakePooled<ClientMetadata>(self->arena_.get());
        metadata->Set(ChaoticGoodConnectionTypeMetadata(),
                      Slice::FromCopiedString("control"));
        auto connection_type =
            metadata->get_pointer(ChaoticGoodConnectionTypeMetadata())->Ref();
        frame.headers = std::move(metadata);
        auto write_buffer = frame.Serialize(&self->hpack_compressor_);
        return self->control_endpoint_->Write(std::move(write_buffer.control));
      },
      []() { return absl::OkStatus(); });
}
void ChaoticGoodConnector::Connect(const Args& args, Result* result,
                                   grpc_closure* notify) {
  {
    ReleasableMutexLock lock(&mu_);
    result_ = result;
    if (is_shutdown_) {
      auto error = GRPC_ERROR_CREATE("connector shutdown");
      MaybeNotify(DEBUG_LOCATION, notify, error);
      lock.Release();
      return;
    }
  }
  args_ = std::move(args);
  notify_ = notify;
  resolved_addr_ = EventEngine::ResolvedAddress(
      reinterpret_cast<const sockaddr*>(args.address->addr), args.address->len);
  GPR_ASSERT(resolved_addr_.value().address() != nullptr);
  Ref().release();  // Ref held by OnHandshakeDone().
  grpc_event_engine::experimental::EventEngine::OnConnectCallback on_connect =
      [this](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> endpoint) {
        if (!endpoint.ok() || handshake_mgr_ == nullptr) {
          auto error = GRPC_ERROR_CREATE("connect endpoint failed");
          MaybeNotify(DEBUG_LOCATION, notify_, error);
          return;
        }
        ExecCtx exec_ctx;
        handshake_mgr_->DoHandshake(
            grpc_event_engine_endpoint_create(std::move(endpoint.value())),
            channel_args_, args_.deadline, nullptr /* acceptor */,
            OnHandshakeDone, this);
      };
  event_engine_->Connect(
      std::move(on_connect), *resolved_addr_, ee_config_,
      memory_quota_->CreateMemoryAllocator("control_endpoint_connection"),
      timeout_);
}

void ChaoticGoodConnector::OnHandshakeDone(void* arg, grpc_error_handle error) {
  auto* args = static_cast<HandshakerArgs*>(arg);
  std::shared_ptr<ChaoticGoodConnector> self =
      static_cast<ChaoticGoodConnector*>(args->user_data)->shared_from_this();
  // Start receiving setting frames;
  {
    ReleasableMutexLock lock(&self->mu_);
    if (!error.ok() || self->is_shutdown_) {
      if (error.ok()) {
        error = GRPC_ERROR_CREATE("connector shutdown");
        // We were shut down after handshaking completed successfully, so
        // destroy the endpoint here.
        if (args->endpoint != nullptr) {
          grpc_endpoint_shutdown(args->endpoint, error);
          grpc_endpoint_destroy(args->endpoint);
          grpc_slice_buffer_destroy(args->read_buffer);
          gpr_free(args->read_buffer);
        }
      }
      self->result_->Reset();
      MaybeNotify(DEBUG_LOCATION, self->notify_, error);
      lock.Release();
      return;
    }
  }
  if (args->endpoint != nullptr) {
    GPR_ASSERT(grpc_event_engine::experimental::grpc_is_event_engine_endpoint(
        args->endpoint));
    self->control_endpoint_ = std::make_shared<PromiseEndpoint>(
        grpc_event_engine::experimental::
            grpc_take_wrapped_event_engine_endpoint(args->endpoint),
        SliceBuffer());

    self->connect_activity_ = MakeActivity(
        TrySeq(ControlEndpointWriteSettingsFrame(self),
               ControlEndpointReadSettingsFrame(self),
               []() { return absl::OkStatus(); }),
        EventEngineWakeupScheduler(
            grpc_event_engine::experimental::GetDefaultEventEngine()),
        [self](absl::Status status) {
          MutexLock lock(&self->mu_);
          if (self->handshake_mgr_ != nullptr) {
            self->handshake_mgr_.reset();
          }
          MaybeNotify(DEBUG_LOCATION, self->notify_, status);
        },
        self->arena_.get(), self->event_engine_.get());
  } else {
    // Handshaking succeeded but there is no endpoint.
    {
      ReleasableMutexLock lock(&self->mu_);
      self->result_->Reset();
      auto error = GRPC_ERROR_CREATE("handshake complete with empty endpoint.");
      MaybeNotify(DEBUG_LOCATION, self->notify_, error);
      lock.Release();
    }
  }
  MutexLock lock(&self->mu_);
  if (self->handshake_mgr_ != nullptr) {
    self->handshake_mgr_.reset();
  }
}

}  // namespace chaotic_good
}  // namespace grpc_core
