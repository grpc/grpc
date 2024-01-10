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

#include "src/core/ext/transport/chaotic_good/server/chaotic_good_server.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/handshaker.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {
namespace chaotic_good {
using grpc_event_engine::experimental::EventEngine;
ChaoticGoodServerListener::ChaoticGoodServerListener(Server* server,
                                                     const ChannelArgs& args)
    : server_(server),
      args_(args),
      config_(args_),
      event_engine_(grpc_event_engine::experimental::GetDefaultEventEngine()) {}

ChaoticGoodServerListener::~ChaoticGoodServerListener() {}

absl::StatusOr<int> ChaoticGoodServerListener::Bind(const char* addr) {
  EventEngine::Listener::AcceptCallback accept_cb =
      [self = shared_from_this()](std::unique_ptr<EventEngine::Endpoint> ep,
                                  MemoryAllocator memory_allocator) {
        MutexLock lock(&self->mu_);
        self->connection_ = std::make_shared<ActiveConnection>(self);
        self->connection_->Start(std::move(ep));
      };
  auto shutdown_cb = [](absl::Status status) {
    if (!status.ok()) {
      gpr_log(GPR_ERROR, "Error server accept connection failed: %s",
              StatusToString(status).c_str());
    }
  };
  GPR_ASSERT(event_engine_ != nullptr);
  auto ee_listener = event_engine_->CreateListener(
      std::move(accept_cb), std::move(shutdown_cb), config_,
      std::make_unique<MemoryQuota>("chaotic_good_server_listener"));
  if (!ee_listener.ok()) {
    return ee_listener.status();
  }
  ee_listener_ = std::move(ee_listener.value());
  auto resolved_addr =
      grpc_event_engine::experimental::URIToResolvedAddress(addr);
  GPR_ASSERT(resolved_addr.ok());
  if (!resolved_addr.ok()) {
    return resolved_addr.status();
  }
  auto port_num = ee_listener_->Bind(resolved_addr.value());
  if (!port_num.ok()) {
    return port_num.status();
  }
  server_->AddListener(OrphanablePtr<Server::ListenerInterface>(this));
  return port_num;
}

absl::Status ChaoticGoodServerListener::StartListening() {
  GPR_ASSERT(ee_listener_ != nullptr);
  auto status = ee_listener_->Start();
  return status;
}

ChaoticGoodServerListener::ActiveConnection::ActiveConnection(
    std::shared_ptr<ChaoticGoodServerListener> listener)
    : listener_(listener),
      memory_allocator_(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "connection")),
      hpack_compressor_(std::make_unique<HPackCompressor>()),
      hpack_parser_(std::make_unique<HPackParser>()),
      connection_deadline_(Duration::Seconds(5)) {}

ChaoticGoodServerListener::ActiveConnection::~ActiveConnection() {
  listener_.reset();
  if (receive_settings_activity_ != nullptr) receive_settings_activity_.reset();
}

void ChaoticGoodServerListener::ActiveConnection::Start(
    std::unique_ptr<EventEngine::Endpoint> endpoint) {
  GPR_ASSERT(handshaking_state_ == nullptr);
  handshaking_state_ = std::make_shared<HandshakingState>(shared_from_this());
  handshaking_state_->Start(std::move(endpoint));
}

void ChaoticGoodServerListener::ActiveConnection::GenerateConnectionID() {
  std::string random_string;
  int random_length = 8;
  std::random_device rd;
  std::mt19937 generator(rd());
  std::string charset =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::uniform_int_distribution<> distribution(0, charset.size() - 1);
  random_string.reserve(8);
  for (size_t i = 0; i < random_length; ++i) {
    random_string += charset[distribution(generator)];
  }
  connection_id_ = Slice::FromCopiedString(random_string);
}

ChaoticGoodServerListener::ActiveConnection::HandshakingState::HandshakingState(
    std::shared_ptr<ActiveConnection> connection)
    : connection_(connection),
      handshake_mgr_(std::make_shared<HandshakeManager>()) {}

void ChaoticGoodServerListener::ActiveConnection::HandshakingState::Start(
    std::unique_ptr<EventEngine::Endpoint> endpoint) {
  handshake_mgr_->DoHandshake(
      grpc_event_engine_endpoint_create(std::move(endpoint)),
      connection_->args(), GetConnectionDeadline(), nullptr, OnHandshakeDone,
      this);
}

auto ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    EndpointReadSettingsFrame(std::shared_ptr<HandshakingState> self) {
  return TrySeq(
      [self = self]() {
        GPR_ASSERT(Activity::current() != nullptr);
        GPR_ASSERT(Activity::current() ==
                   self->connection_->receive_settings_activity_->current());
        return Activity::current();
      },
      [self = self]() {
        return self->connection_->endpoint_->ReadSlice(
            FrameHeader::frame_header_size_);
      },
      [self = self](Slice slice) mutable {
        // Parse frame header
        auto frame_header = FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
            GRPC_SLICE_START_PTR(slice.c_slice())));
        GPR_ASSERT(frame_header.ok());
        return TrySeq(
            self->connection_->endpoint_->Read(frame_header->GetFrameLength()),
            [frame_header = *frame_header,
             self = self](SliceBuffer buffer) mutable {
              // Read Setting frame.
              SettingsFrame frame;
              // Initialized to get this_cpu() info in global_stat().
              ExecCtx exec_ctx;
              // Deserialize frame from read buffer.
              absl::BitGen bitgen;
              BufferPair buffer_pair{std::move(buffer), SliceBuffer()};
              auto status = frame.Deserialize(
                  self->connection_->hpack_parser_.get(), frame_header,
                  absl::BitGenRef(bitgen), GetContext<Arena>(),
                  std::move(buffer_pair));
              GPR_ASSERT(status.ok());
              self->connection_->connection_type_ =
                  frame.headers
                      ->get_pointer(ChaoticGoodConnectionTypeMetadata())
                      ->Ref();
              bool is_control_endpoint =
                  std::string(
                      self->connection_->connection_type_.as_string_view()) ==
                  "control";
              std::shared_ptr<Slice> connection_id_;
              if (!is_control_endpoint) {
                // Get connection id for data endpoint.
                self->connection_->connection_id_ =
                    frame.headers
                        ->get_pointer(ChaoticGoodConnectionIdMetadata())
                        ->Ref();
              }
              return is_control_endpoint;
            });
      });
}

auto ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    EndpointWriteSettingsFrame(std::shared_ptr<HandshakingState> self,
                               bool is_control_endpoint) {
  return If(
      is_control_endpoint,
      // connection_type = control
      [self = self]() {
        self->connection_->GenerateConnectionID();
        // Add a wait latch for data endpoint to connect.
        {
          MutexLock lock(&self->connection_->listener_->mu_);
          // Add latch to map;
          self->connection_->listener_->connectivity_map_.insert(
              std::pair<
                  std::string,
                  std::shared_ptr<Latch<std::shared_ptr<PromiseEndpoint>>>>(
                  self->connection_->connection_id_.as_string_view(),
                  std::make_shared<Latch<std::shared_ptr<PromiseEndpoint>>>()));
        }
        return Race(
            TrySeq(
                [self = self]() mutable {
                  SettingsFrame frame;
                  ClientMetadataHandle metadata =
                      GetContext<Arena>()->MakePooled<ClientMetadata>(
                          GetContext<Arena>());
                  metadata->Set(ChaoticGoodConnectionIdMetadata(),
                                self->connection_->connection_id_.Ref());
                  frame.headers = std::move(metadata);
                  auto write_buffer = frame.Serialize(
                      self->connection_->hpack_compressor_.get());
                  return self->connection_->endpoint_->Write(
                      std::move(write_buffer.control));
                },
                [self = self]() mutable {
                  MutexLock lock(&self->connection_->listener_->mu_);
                  auto latch =
                      self->connection_->listener_->connectivity_map_
                          .find(std::string(self->connection_->connection_id_
                                                .as_string_view()))
                          ->second;
                  return latch->Wait();
                },
                [](std::shared_ptr<PromiseEndpoint> ret) -> absl::Status {
                  GPR_ASSERT(ret != nullptr);
                  return absl::OkStatus();
                }),
            // Set timeout for waiting data endpoint connect.
            TrySeq(
                // []() {
                Sleep(Timestamp::Now() +
                      self->connection_->connection_deadline_),
                [self = self]() mutable -> absl::Status {
                  MutexLock lock(&self->connection_->listener_->mu_);
                  // Delete connection id from map when timeout;
                  self->connection_->listener_->connectivity_map_.erase(
                      std::string(
                          self->connection_->connection_id_.as_string_view()));
                  return absl::DeadlineExceededError(
                      Timestamp::Now().ToString());
                }));
      },
      // connection_type = data.
      TrySeq([self = self]() mutable {
        // Send data endpoint setting frame
        SettingsFrame frame;
        ClientMetadataHandle metadata =
            GetContext<Arena>()->MakePooled<ClientMetadata>(
                GetContext<Arena>());
        metadata->Set(ChaoticGoodConnectionIdMetadata(),
                      self->connection_->connection_id_.Ref());
        frame.headers = std::move(metadata);
        auto write_buffer =
            frame.Serialize(self->connection_->hpack_compressor_.get());
        return TrySeq(
            self->connection_->endpoint_->Write(
                std::move(write_buffer.control)),
            [self = self]() mutable {
              MutexLock lock(&self->connection_->listener_->mu_);
              // Set endpoint to latch
              self->connection_->listener_->connectivity_map_
                  .find(std::string(
                      self->connection_->connection_id_.as_string_view()))
                  ->second->Set(std::move(self->connection_->endpoint_));
              return absl::OkStatus();
            });
      }));
}
void ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    OnHandshakeDone(void* arg, grpc_error_handle error) {
  if (!error.ok()) {
    gpr_log(GPR_ERROR, "Error server handshake failed: %s",
            StatusToString(error).c_str());
    return;
  }
  auto* args = static_cast<HandshakerArgs*>(arg);
  GPR_ASSERT(args != nullptr);
  if (args->endpoint == nullptr) {
    gpr_log(GPR_ERROR, "Error server handshake done with null endpoint.");
    return;
  }
  GPR_ASSERT(grpc_event_engine::experimental::grpc_is_event_engine_endpoint(
      args->endpoint));
  std::shared_ptr<HandshakingState> self =
      static_cast<HandshakingState*>(args->user_data)->shared_from_this();
  self->connection_->endpoint_ = std::make_shared<PromiseEndpoint>(
      grpc_event_engine::experimental::grpc_take_wrapped_event_engine_endpoint(
          args->endpoint),
      SliceBuffer());
  self->connection_->receive_settings_activity_ = MakeActivity(
      TrySeq(EndpointReadSettingsFrame(self),
             [self](bool is_control_endpoint) {
               return EndpointWriteSettingsFrame(self, is_control_endpoint);
             }),
      EventEngineWakeupScheduler(
          grpc_event_engine::experimental::GetDefaultEventEngine()),
      [](absl::Status status) {
        if (!status.ok()) {
          gpr_log(GPR_ERROR, "Error server receive setting frame failed: %s",
                  StatusToString(status).c_str());
        }
      },
      MakeScopedArena(1024, &self->connection_->memory_allocator_),
      grpc_event_engine::experimental::GetDefaultEventEngine().get());
}

Timestamp ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    GetConnectionDeadline() {
  if (!connection_->args().Contains(GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS)) {
    return Timestamp::Now() + Duration::Seconds(5);
  } else {
    return Timestamp::Now() +
           std::max(Duration::Seconds(5),
                    connection_->args()
                        .GetDurationFromIntMillis(
                            GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS)
                        .value());
  }
}
}  // namespace chaotic_good
}  // namespace grpc_core
