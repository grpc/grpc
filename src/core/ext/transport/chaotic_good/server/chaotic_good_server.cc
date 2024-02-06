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
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/random/bit_gen_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chaotic_good/settings_metadata.h"
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
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/handshaker.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

namespace {
const Duration kConnectionDeadline = Duration::Seconds(5);
}  // namespace

using grpc_event_engine::experimental::EventEngine;
ChaoticGoodServerListener::ChaoticGoodServerListener(
    Server* server, const ChannelArgs& args,
    absl::AnyInvocable<std::string()> connection_id_generator)
    : server_(server),
      args_(args),
      event_engine_(grpc_event_engine::experimental::GetDefaultEventEngine()),
      connection_id_generator_(std::move(connection_id_generator)) {}

ChaoticGoodServerListener::~ChaoticGoodServerListener() {
  event_engine_->Run([on_destroy_done = on_destroy_done_]() {
    ExecCtx exec_ctx;
    if (on_destroy_done != nullptr) {
      ExecCtx::Run(DEBUG_LOCATION, on_destroy_done, absl::OkStatus());
      ExecCtx::Get()->Flush();
    }
  });
}

absl::StatusOr<int> ChaoticGoodServerListener::Bind(const char* addr) {
  EventEngine::Listener::AcceptCallback accept_cb =
      [self = Ref()](std::unique_ptr<EventEngine::Endpoint> ep,
                     MemoryAllocator) {
        ExecCtx exec_ctx;
        MutexLock lock(&self->mu_);
        self->connection_list_.emplace(
            MakeOrphanable<ActiveConnection>(self, std::move(ep)));
      };
  auto shutdown_cb = [](absl::Status status) {
    if (!status.ok()) {
      gpr_log(GPR_ERROR, "Server accept connection failed: %s",
              StatusToString(status).c_str());
    }
  };
  GPR_ASSERT(event_engine_ != nullptr);
  auto ee_listener = event_engine_->CreateListener(
      std::move(accept_cb), std::move(shutdown_cb),
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(args_),
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
    RefCountedPtr<ChaoticGoodServerListener> listener,
    std::unique_ptr<EventEngine::Endpoint> endpoint)
    : InternallyRefCounted("ActiveConnection"),
      memory_allocator_(listener->memory_allocator_),
      listener_(listener) {
  handshaking_state_ = MakeRefCounted<HandshakingState>(Ref());
  handshaking_state_->Start(std::move(endpoint));
}

ChaoticGoodServerListener::ActiveConnection::~ActiveConnection() {
  if (receive_settings_activity_ != nullptr) receive_settings_activity_.reset();
}

void ChaoticGoodServerListener::ActiveConnection::NewConnectionID() {
  bool has_new_id = false;
  MutexLock lock(&listener_->mu_);
  while (!has_new_id) {
    connection_id_ = listener_->connection_id_generator_();
    if (!listener_->connectivity_map_.contains(connection_id_)) {
      has_new_id = true;
    }
  }
  listener_->connectivity_map_.emplace(
      connection_id_,
      std::make_shared<InterActivityLatch<std::shared_ptr<PromiseEndpoint>>>());
}

void ChaoticGoodServerListener::ActiveConnection::Fail(
    absl::string_view error) {
  gpr_log(GPR_ERROR, "ActiveConnection::Fail:%p %s", this,
          std::string(error).c_str());
  // Can easily be holding various locks here: bounce through EE to ensure no
  // deadlocks.
  listener_->event_engine_->Run([self = Ref()]() {
    ExecCtx exec_ctx;
    OrphanablePtr<ActiveConnection> con;
    MutexLock lock(&self->listener_->mu_);
    auto v = self->listener_->connection_list_.extract(self.get());
    if (!v.empty()) con = std::move(v.value());
  });
}

ChaoticGoodServerListener::ActiveConnection::HandshakingState::HandshakingState(
    RefCountedPtr<ActiveConnection> connection)
    : memory_allocator_(connection->memory_allocator_),
      connection_(std::move(connection)),
      handshake_mgr_(MakeRefCounted<HandshakeManager>()) {}

void ChaoticGoodServerListener::ActiveConnection::HandshakingState::Start(
    std::unique_ptr<EventEngine::Endpoint> endpoint) {
  handshake_mgr_->DoHandshake(
      grpc_event_engine_endpoint_create(std::move(endpoint)),
      connection_->args(), GetConnectionDeadline(), nullptr,
      [self = Ref()](absl::StatusOr<HandshakerArgs*> result) {
        self->OnHandshakeDone(std::move(result));
      });
}

auto ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    EndpointReadSettingsFrame(RefCountedPtr<HandshakingState> self) {
  return TrySeq(
      self->connection_->endpoint_->ReadSlice(FrameHeader::kFrameHeaderSize),
      [self](Slice slice) {
        // Parse frame header
        auto frame_header = FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
            GRPC_SLICE_START_PTR(slice.c_slice())));
        return If(
            frame_header.ok(),
            [self, &frame_header]() {
              return TrySeq(
                  self->connection_->endpoint_->Read(
                      frame_header->GetFrameLength()),
                  [frame_header = *frame_header,
                   self](SliceBuffer buffer) -> absl::StatusOr<bool> {
                    // Read Setting frame.
                    SettingsFrame frame;
                    // Deserialize frame from read buffer.
                    BufferPair buffer_pair{std::move(buffer), SliceBuffer()};
                    auto status = frame.Deserialize(
                        &self->connection_->hpack_parser_, frame_header,
                        absl::BitGenRef(self->connection_->bitgen_),
                        GetContext<Arena>(), std::move(buffer_pair),
                        FrameLimits{});
                    if (!status.ok()) return status;
                    if (frame.headers == nullptr) {
                      return absl::UnavailableError("no settings headers");
                    }
                    auto settings_metadata =
                        SettingsMetadata::FromMetadataBatch(*frame.headers);
                    if (!settings_metadata.ok()) {
                      return settings_metadata.status();
                    }
                    const bool is_control_endpoint =
                        settings_metadata->connection_type ==
                        SettingsMetadata::ConnectionType::kControl;
                    if (!is_control_endpoint) {
                      if (!settings_metadata->connection_id.has_value()) {
                        return absl::UnavailableError(
                            "no connection id in data endpoint settings frame");
                      }
                      if (!settings_metadata->alignment.has_value()) {
                        return absl::UnavailableError(
                            "no alignment in data endpoint settings frame");
                      }
                      // Get connection-id and data-alignment for data endpoint.
                      self->connection_->connection_id_ =
                          *settings_metadata->connection_id;
                      self->connection_->data_alignment_ =
                          *settings_metadata->alignment;
                    }
                    return is_control_endpoint;
                  });
            },
            [&frame_header]() {
              return [r = frame_header.status()]() -> absl::StatusOr<bool> {
                return r;
              };
            });
      });
}

auto ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    WaitForDataEndpointSetup(RefCountedPtr<HandshakingState> self) {
  return Race(TrySeq(
                  []() {
                    // TODO(ladynana): find a way to resolve SeqState to actual
                    // value.
                    return absl::OkStatus();
                  },
                  [self]() {
                    MutexLock lock(&self->connection_->listener_->mu_);
                    auto latch = self->connection_->listener_->connectivity_map_
                                     .find(self->connection_->connection_id_)
                                     ->second;
                    return latch->Wait();
                  },
                  [](std::shared_ptr<PromiseEndpoint> ret) -> absl::Status {
                    if (ret == nullptr) {
                      return absl::UnavailableError("no data endpoint");
                    }
                    // TODO(ladynana): initialize server transport.
                    return absl::OkStatus();
                  }),
              // Set timeout for waiting data endpoint connect.
              TrySeq(
                  // []() {
                  Sleep(Timestamp::Now() + kConnectionDeadline),
                  [self]() mutable -> absl::Status {
                    MutexLock lock(&self->connection_->listener_->mu_);
                    // Delete connection id from map when timeout;
                    self->connection_->listener_->connectivity_map_.erase(
                        self->connection_->connection_id_);
                    return absl::DeadlineExceededError("Deadline exceeded.");
                  }));
}

auto ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    ControlEndpointWriteSettingsFrame(RefCountedPtr<HandshakingState> self) {
  return TrySeq(
      [self]() {
        self->connection_->NewConnectionID();
        SettingsFrame frame;
        frame.headers =
            SettingsMetadata{absl::nullopt, self->connection_->connection_id_,
                             absl::nullopt}
                .ToMetadataBatch(GetContext<Arena>());
        auto write_buffer =
            frame.Serialize(&self->connection_->hpack_compressor_);
        return self->connection_->endpoint_->Write(
            std::move(write_buffer.control));
      },
      WaitForDataEndpointSetup(self));
}

auto ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    DataEndpointWriteSettingsFrame(RefCountedPtr<HandshakingState> self) {
  return TrySeq(
      [self]() {
        // Send data endpoint setting frame
        SettingsFrame frame;
        frame.headers =
            SettingsMetadata{absl::nullopt, self->connection_->connection_id_,
                             self->connection_->data_alignment_}
                .ToMetadataBatch(GetContext<Arena>());
        auto write_buffer =
            frame.Serialize(&self->connection_->hpack_compressor_);
        return self->connection_->endpoint_->Write(
            std::move(write_buffer.control));
      },
      [self]() mutable {
        MutexLock lock(&self->connection_->listener_->mu_);
        // Set endpoint to latch
        auto it = self->connection_->listener_->connectivity_map_.find(
            self->connection_->connection_id_);
        if (it == self->connection_->listener_->connectivity_map_.end()) {
          return absl::InternalError(
              absl::StrCat("Connection not in map: ",
                           absl::CEscape(self->connection_->connection_id_)));
        }
        it->second->Set(std::move(self->connection_->endpoint_));
        return absl::OkStatus();
      });
}

auto ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    EndpointWriteSettingsFrame(RefCountedPtr<HandshakingState> self,
                               bool is_control_endpoint) {
  return If(is_control_endpoint, ControlEndpointWriteSettingsFrame(self),
            DataEndpointWriteSettingsFrame(self));
}

void ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    OnHandshakeDone(absl::StatusOr<HandshakerArgs*> result) {
  if (!result.ok()) {
    connection_->Fail(
        absl::StrCat("Handshake failed: ", result.status().ToString()));
    return;
  }
  if ((*result)->endpoint == nullptr) {
    connection_->Fail("Server handshake done but has empty endpoint.");
    return;
  }
  GPR_ASSERT(grpc_event_engine::experimental::grpc_is_event_engine_endpoint(
      (*result)->endpoint));
  connection_->endpoint_ = std::make_shared<PromiseEndpoint>(
      grpc_event_engine::experimental::grpc_take_wrapped_event_engine_endpoint(
          (*result)->endpoint),
      SliceBuffer());
  auto activity = MakeActivity(
      [self = Ref()]() {
        return TrySeq(Race(EndpointReadSettingsFrame(self),
                           TrySeq(Sleep(Timestamp::Now() + kConnectionDeadline),
                                  []() -> absl::StatusOr<bool> {
                                    return absl::DeadlineExceededError(
                                        "Waiting for initial settings frame");
                                  })),
                      [self](bool is_control_endpoint) {
                        return EndpointWriteSettingsFrame(self,
                                                          is_control_endpoint);
                      });
      },
      EventEngineWakeupScheduler(
          grpc_event_engine::experimental::GetDefaultEventEngine()),
      [self = Ref()](absl::Status status) {
        if (!status.ok()) {
          self->connection_->Fail(
              absl::StrCat("Server setting frame handling failed: ",
                           StatusToString(status)));
        }
      },
      connection_->arena_.get(),
      grpc_event_engine::experimental::GetDefaultEventEngine().get());
  MutexLock lock(&connection_->mu_);
  if (connection_->orphaned_) return;
  connection_->receive_settings_activity_ = std::move(activity);
}

Timestamp ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    GetConnectionDeadline() {
  if (connection_->args().Contains(GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS)) {
    return Timestamp::Now() +
           connection_->args()
               .GetDurationFromIntMillis(GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS)
               .value();
  }
  return Timestamp::Now() + kConnectionDeadline;
}

}  // namespace chaotic_good
}  // namespace grpc_core
