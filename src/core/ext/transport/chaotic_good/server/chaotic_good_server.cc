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

#include "src/core/ext/transport/chaotic_good/server/chaotic_good_server.h"

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chaotic_good/server_transport.h"
#include "src/core/ext/transport/chaotic_good/settings_metadata.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/event_engine/extensions/chaotic_good_extension.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"
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
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/server/server.h"

namespace grpc_core {
namespace chaotic_good {

namespace {
const Duration kConnectionDeadline = Duration::Seconds(120);
}  // namespace

using grpc_event_engine::experimental::EventEngine;
ChaoticGoodServerListener::ChaoticGoodServerListener(
    Server* server, const ChannelArgs& args,
    absl::AnyInvocable<std::string()> connection_id_generator)
    : server_(server),
      args_(args),
      event_engine_(
          args.GetObjectRef<grpc_event_engine::experimental::EventEngine>()),
      connection_id_generator_(std::move(connection_id_generator)) {}

ChaoticGoodServerListener::~ChaoticGoodServerListener() {
  if (on_destroy_done_ != nullptr) {
    event_engine_->Run([on_destroy_done = on_destroy_done_]() {
      ExecCtx exec_ctx;
      ExecCtx::Run(DEBUG_LOCATION, on_destroy_done, absl::OkStatus());
    });
  }
}

absl::StatusOr<int> ChaoticGoodServerListener::Bind(
    grpc_event_engine::experimental::EventEngine::ResolvedAddress addr) {
  if (GRPC_TRACE_FLAG_ENABLED(chaotic_good)) {
    auto str = grpc_event_engine::experimental::ResolvedAddressToString(addr);
    LOG(INFO) << "CHAOTIC_GOOD: Listen on "
              << (str.ok() ? str->c_str() : str.status().ToString());
  }
  EventEngine::Listener::AcceptCallback accept_cb =
      [self = RefAsSubclass<ChaoticGoodServerListener>()](
          std::unique_ptr<EventEngine::Endpoint> ep, MemoryAllocator) {
        ExecCtx exec_ctx;
        MutexLock lock(&self->mu_);
        if (self->shutdown_) return;
        self->connection_list_.emplace(
            MakeOrphanable<ActiveConnection>(self, std::move(ep)));
      };
  auto shutdown_cb = [](absl::Status status) {
    if (!status.ok()) {
      LOG(ERROR) << "Server accept connection failed: " << status;
    }
  };
  CHECK_NE(event_engine_, nullptr);
  auto ee_listener = event_engine_->CreateListener(
      std::move(accept_cb), std::move(shutdown_cb),
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(args_),
      std::make_unique<MemoryQuota>("chaotic_good_server_listener"));
  if (!ee_listener.ok()) {
    LOG(ERROR) << "Bind failed: " << ee_listener.status().ToString();
    return ee_listener.status();
  }
  ee_listener_ = std::move(ee_listener.value());
  auto port_num = ee_listener_->Bind(addr);
  if (!port_num.ok()) {
    return port_num.status();
  }
  return port_num;
}

absl::Status ChaoticGoodServerListener::StartListening() {
  CHECK(ee_listener_ != nullptr);
  auto status = ee_listener_->Start();
  if (!status.ok()) {
    LOG(ERROR) << "Start listening failed: " << status;
  } else {
    GRPC_TRACE_LOG(chaotic_good, INFO) << "CHAOTIC_GOOD: Started listening";
  }
  return status;
}

ChaoticGoodServerListener::ActiveConnection::ActiveConnection(
    RefCountedPtr<ChaoticGoodServerListener> listener,
    std::unique_ptr<EventEngine::Endpoint> endpoint)
    : listener_(std::move(listener)) {
  arena_->SetContext<grpc_event_engine::experimental::EventEngine>(
      listener_->event_engine_.get());
  handshaking_state_ = MakeRefCounted<HandshakingState>(Ref());
  handshaking_state_->Start(std::move(endpoint));
}

ChaoticGoodServerListener::ActiveConnection::~ActiveConnection() {
  if (receive_settings_activity_ != nullptr) receive_settings_activity_.reset();
}

void ChaoticGoodServerListener::ActiveConnection::Orphan() {
  GRPC_TRACE_LOG(chaotic_good, INFO) << "ActiveConnection::Orphan() " << this;
  if (handshaking_state_ != nullptr) {
    handshaking_state_->Shutdown();
    handshaking_state_.reset();
  }
  ActivityPtr activity;
  {
    MutexLock lock(&mu_);
    orphaned_ = true;
    activity = std::move(receive_settings_activity_);
  }
  activity.reset();
  Unref();
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
      connection_id_, std::make_shared<InterActivityLatch<PromiseEndpoint>>());
}

void ChaoticGoodServerListener::ActiveConnection::Done(
    absl::optional<absl::string_view> error) {
  if (error.has_value()) {
    LOG(ERROR) << "ActiveConnection::Done:" << this << " " << *error;
  }
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
    : connection_(std::move(connection)),
      handshake_mgr_(MakeRefCounted<HandshakeManager>()) {}

void ChaoticGoodServerListener::ActiveConnection::HandshakingState::Start(
    std::unique_ptr<EventEngine::Endpoint> endpoint) {
  handshake_mgr_->DoHandshake(
      OrphanablePtr<grpc_endpoint>(
          grpc_event_engine_endpoint_create(std::move(endpoint))),
      connection_->args(), GetConnectionDeadline(), nullptr,
      [self = Ref()](absl::StatusOr<HandshakerArgs*> result) {
        self->OnHandshakeDone(std::move(result));
      });
}

auto ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    EndpointReadSettingsFrame(RefCountedPtr<HandshakingState> self) {
  return TrySeq(
      self->connection_->endpoint_.ReadSlice(FrameHeader::kFrameHeaderSize),
      [self](Slice slice) {
        // Parse frame header
        auto frame_header = FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
            GRPC_SLICE_START_PTR(slice.c_slice())));
        return If(
            frame_header.ok(),
            [self, &frame_header]() {
              return TrySeq(
                  self->connection_->endpoint_.Read(
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
  return Race(
      TrySeq(
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
          [self](PromiseEndpoint ret) -> absl::Status {
            MutexLock lock(&self->connection_->listener_->mu_);
            if (GRPC_TRACE_FLAG_ENABLED(chaotic_good)) {
              LOG(INFO) << self->connection_.get()
                        << " Data endpoint setup done: shutdown="
                        << (self->connection_->listener_->shutdown_ ? "true"
                                                                    : "false");
            }
            if (self->connection_->listener_->shutdown_) {
              return absl::UnavailableError("Server shutdown");
            }
            return self->connection_->listener_->server_->SetupTransport(
                new ChaoticGoodServerTransport(
                    self->connection_->args(),
                    std::move(self->connection_->endpoint_), std::move(ret),
                    self->connection_->listener_->event_engine_,
                    std::move(self->connection_->hpack_parser_),
                    std::move(self->connection_->hpack_compressor_)),
                nullptr, self->connection_->args(), nullptr);
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
  self->connection_->NewConnectionID();
  SettingsFrame frame;
  frame.headers =
      SettingsMetadata{absl::nullopt, self->connection_->connection_id_,
                       absl::nullopt}
          .ToMetadataBatch();
  bool saw_encoding_errors = false;
  auto write_buffer = frame.Serialize(&self->connection_->hpack_compressor_,
                                      saw_encoding_errors);
  // ignore encoding errors: they will be logged separately already
  return TrySeq(
      self->connection_->endpoint_.Write(std::move(write_buffer.control)),
      WaitForDataEndpointSetup(self));
}

auto ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    DataEndpointWriteSettingsFrame(RefCountedPtr<HandshakingState> self) {
  // Send data endpoint setting frame
  SettingsFrame frame;
  frame.headers =
      SettingsMetadata{absl::nullopt, self->connection_->connection_id_,
                       self->connection_->data_alignment_}
          .ToMetadataBatch();
  bool saw_encoding_errors = false;
  auto write_buffer = frame.Serialize(&self->connection_->hpack_compressor_,
                                      saw_encoding_errors);
  // ignore encoding errors: they will be logged separately already
  return TrySeq(
      self->connection_->endpoint_.Write(std::move(write_buffer.control)),
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
  return If(
      is_control_endpoint,
      [&self] { return ControlEndpointWriteSettingsFrame(self); },
      [&self] { return DataEndpointWriteSettingsFrame(self); });
}

void ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    OnHandshakeDone(absl::StatusOr<HandshakerArgs*> result) {
  if (!result.ok()) {
    connection_->Done(
        absl::StrCat("Handshake failed: ", result.status().ToString()));
    return;
  }
  CHECK_NE(*result, nullptr);
  if ((*result)->endpoint == nullptr) {
    connection_->Done("Server handshake done but has empty endpoint.");
    return;
  }
  CHECK(grpc_event_engine::experimental::grpc_is_event_engine_endpoint(
      (*result)->endpoint.get()));
  auto ee_endpoint =
      grpc_event_engine::experimental::grpc_take_wrapped_event_engine_endpoint(
          (*result)->endpoint.release());
  auto* chaotic_good_ext = grpc_event_engine::experimental::QueryExtension<
      grpc_event_engine::experimental::ChaoticGoodExtension>(ee_endpoint.get());
  connection_->endpoint_ =
      PromiseEndpoint(std::move(ee_endpoint), SliceBuffer());
  auto activity = MakeActivity(
      [self = Ref(), chaotic_good_ext]() {
        return TrySeq(
            Race(EndpointReadSettingsFrame(self),
                 TrySeq(Sleep(Timestamp::Now() + kConnectionDeadline),
                        []() -> absl::StatusOr<bool> {
                          return absl::DeadlineExceededError(
                              "Waiting for initial settings frame");
                        })),
            [self, chaotic_good_ext](bool is_control_endpoint) {
              if (chaotic_good_ext != nullptr) {
                chaotic_good_ext->EnableStatsCollection(is_control_endpoint);
                if (is_control_endpoint) {
                  // Control endpoint should use the default memory quota
                  chaotic_good_ext->UseMemoryQuota(
                      ResourceQuota::Default()->memory_quota());
                }
              }
              return EndpointWriteSettingsFrame(self, is_control_endpoint);
            });
      },
      EventEngineWakeupScheduler(connection_->listener_->event_engine_),
      [self = Ref()](absl::Status status) {
        if (!status.ok()) {
          self->connection_->Done(
              absl::StrCat("Server setting frame handling failed: ",
                           StatusToString(status)));
        } else {
          self->connection_->Done();
        }
      },
      connection_->arena_.get());
  MutexLock lock(&connection_->mu_);
  if (connection_->orphaned_) return;
  connection_->receive_settings_activity_ = std::move(activity);
}

Timestamp ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    GetConnectionDeadline() {
  return Timestamp::Now() +
         connection_->args()
             .GetDurationFromIntMillis(GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS)
             .value_or(kConnectionDeadline);
}

void ChaoticGoodServerListener::Orphan() {
  GRPC_TRACE_LOG(chaotic_good, INFO) << "ChaoticGoodServerListener::Orphan()";
  {
    absl::flat_hash_set<OrphanablePtr<ActiveConnection>> connection_list;
    MutexLock lock(&mu_);
    connection_list = std::move(connection_list_);
    shutdown_ = true;
  }
  ee_listener_.reset();
  Unref();
};

}  // namespace chaotic_good
}  // namespace grpc_core

int grpc_server_add_chaotic_good_port(grpc_server* server, const char* addr) {
  grpc_core::ExecCtx exec_ctx;
  auto* const core_server = grpc_core::Server::FromC(server);
  const std::string parsed_addr = grpc_core::URI::PercentDecode(addr);
  const auto resolved_or = grpc_core::GetDNSResolver()->LookupHostnameBlocking(
      parsed_addr, absl::StrCat(0xd20));
  if (!resolved_or.ok()) {
    LOG(ERROR) << "Failed to resolve " << addr << ": "
               << resolved_or.status().ToString();
    return 0;
  }
  int port_num = 0;
  std::vector<std::pair<std::string, absl::Status>> error_list;
  for (const auto& resolved_addr : resolved_or.value()) {
    auto listener = grpc_core::MakeOrphanable<
        grpc_core::chaotic_good::ChaoticGoodServerListener>(
        core_server, core_server->channel_args());
    const auto ee_addr =
        grpc_event_engine::experimental::CreateResolvedAddress(resolved_addr);
    std::string addr_str =
        *grpc_event_engine::experimental::ResolvedAddressToString(ee_addr);
    GRPC_TRACE_LOG(chaotic_good, INFO) << "BIND: " << addr_str;
    auto bind_result = listener->Bind(ee_addr);
    if (!bind_result.ok()) {
      error_list.push_back(
          std::make_pair(std::move(addr_str), bind_result.status()));
      continue;
    }
    if (port_num == 0) {
      port_num = bind_result.value();
    } else {
      CHECK(port_num == bind_result.value());
    }
    core_server->AddListener(std::move(listener));
  }
  if (error_list.size() == resolved_or->size()) {
    LOG(ERROR) << "Failed to bind any address for " << addr;
    for (const auto& error : error_list) {
      LOG(ERROR) << "  " << error.first << ": " << error.second;
    }
  } else if (!error_list.empty()) {
    LOG(INFO) << "Failed to bind some addresses for " << addr;
    for (const auto& error : error_list) {
      GRPC_TRACE_LOG(chaotic_good, INFO)
          << "Binding Failed: " << error.first << ": " << error.second;
    }
  }
  return port_num;
}
