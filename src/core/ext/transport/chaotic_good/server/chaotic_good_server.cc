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

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/port_platform.h>

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
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chaotic_good/server_transport.h"
#include "src/core/ext/transport/chaotic_good_legacy/server/chaotic_good_server.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/event_engine/extensions/chaotic_good_extension.h"
#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/lib/event_engine/posix.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/event_engine/shim.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
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
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/server/server.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace chaotic_good {

namespace {
const Duration kConnectionDeadline = Duration::Seconds(120);

void LogInitFailure(Server* server, absl::string_view what,
                    std::optional<absl::Status> status) {
  LOG(ERROR) << "ChaoticGoodServerListener Init failed: " << what
             << " with status: "
             << (status.has_value() ? status->ToString() : "no status");
  auto* server_node = server->channelz_node();
  if (server_node != nullptr) {
    if (status.has_value()) {
      server_node->NewTraceNode(what, ": ", *status).Commit();
    } else {
      server_node->NewTraceNode(what).Commit();
    }
  }
}

void LogInformational(Server* server, absl::string_view what) {
  VLOG(2) << "ChaoticGoodServerListener: " << what;
  auto* server_node = server->channelz_node();
  if (server_node != nullptr) {
    server_node->NewTraceNode(what).Commit();
  }
}

}  // namespace

using grpc_event_engine::experimental::EventEngine;

ChaoticGoodServerListener::ChaoticGoodServerListener(
    Server* server, const ChannelArgs& args,
    absl::AnyInvocable<std::string()> connection_id_generator)
    : server_(server),
      args_(args),
      event_engine_(
          args.GetObjectRef<grpc_event_engine::experimental::EventEngine>()),
      data_connection_listener_(MakeRefCounted<DataConnectionListener>(
          std::move(connection_id_generator),
          args.GetDurationFromIntMillis(GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS)
              .value_or(kConnectionDeadline),
          event_engine_)) {}

ChaoticGoodServerListener::~ChaoticGoodServerListener() {
  if (on_destroy_done_ != nullptr) {
    event_engine_->Run([on_destroy_done = on_destroy_done_]() {
      ExecCtx exec_ctx;
      ExecCtx::Run(DEBUG_LOCATION, on_destroy_done, absl::OkStatus());
    });
  }
}

absl::StatusOr<
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Listener>>
ChaoticGoodServerListener::CreateListener(bool must_be_posix) {
  CHECK_NE(event_engine_, nullptr);
  auto* event_engine_supports_fd =
      grpc_event_engine::experimental::QueryExtension<
          grpc_event_engine::experimental::EventEngineSupportsFdExtension>(
          event_engine_.get());
  if (must_be_posix && event_engine_supports_fd == nullptr) {
    LogInitFailure(server_,
                   "EventEngine does not support external fd listeners",
                   absl::InternalError(
                       "EventEngine does not support external fd listeners"));
    return absl::InternalError(
        "EventEngine does not support external fd listeners");
  }
  auto shutdown_cb =
      [self = RefAsSubclass<ChaoticGoodServerListener>()](absl::Status status) {
        if (!status.ok()) {
          self->LogConnectionFailure("Server accept connection failed", status);
        }
      };
  if (event_engine_supports_fd != nullptr) {
    grpc_event_engine::experimental::PosixEventEngineWithFdSupport::
        PosixAcceptCallback accept_cb =
            [self = RefAsSubclass<ChaoticGoodServerListener>()](
                int listener_fd, std::unique_ptr<EventEngine::Endpoint> ep,
                bool is_external, MemoryAllocator,
                grpc_event_engine::experimental::SliceBuffer* pending_data) {
              ExecCtx exec_ctx;
              LogInformational(
                  self->server_,
                  absl::StrCat("Accepting connection: ",
                               ResolvedAddressToString(ep->GetPeerAddress())
                                   .value_or("<<unknown peer address>>")));
              grpc_byte_buffer* pending_buf = nullptr;
              if (pending_data != nullptr && pending_data->Length() > 0) {
                pending_buf = grpc_raw_byte_buffer_create(nullptr, 0);
                grpc_slice_buffer_swap(&pending_buf->data.raw.slice_buffer,
                                       pending_data->c_slice_buffer());
              }
              MutexLock lock(&self->mu_);
              if (self->shutdown_) return;
              self->connection_list_.emplace(MakeOrphanable<ActiveConnection>(
                  self, std::move(ep), is_external, listener_fd, pending_buf));
            };
    return event_engine_supports_fd->CreatePosixListener(
        std::move(accept_cb), std::move(shutdown_cb),
        grpc_event_engine::experimental::ChannelArgsEndpointConfig(args_),
        std::make_unique<MemoryQuota>("chaotic_good_server_listener"));
  }
  EventEngine::Listener::AcceptCallback accept_cb =
      [self = RefAsSubclass<ChaoticGoodServerListener>()](
          std::unique_ptr<EventEngine::Endpoint> ep, MemoryAllocator) {
        ExecCtx exec_ctx;
        LogInformational(
            self->server_,
            absl::StrCat("Accepting connection: ",
                         ResolvedAddressToString(ep->GetPeerAddress())
                             .value_or("<<unknown peer address>>")));
        MutexLock lock(&self->mu_);
        if (self->shutdown_) return;
        self->connection_list_.emplace(MakeOrphanable<ActiveConnection>(
            self, std::move(ep), false, 0, nullptr));
      };
  return event_engine_->CreateListener(
      std::move(accept_cb), std::move(shutdown_cb),
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(args_),
      std::make_unique<MemoryQuota>("chaotic_good_server_listener"));
}

absl::StatusOr<int> ChaoticGoodServerListener::Bind(
    grpc_event_engine::experimental::EventEngine::ResolvedAddress addr) {
  if (GRPC_TRACE_FLAG_ENABLED(chaotic_good)) {
    auto str = grpc_event_engine::experimental::ResolvedAddressToString(addr);
    LOG(INFO) << "CHAOTIC_GOOD: Listen on "
              << (str.ok() ? str->c_str() : str.status().ToString());
  }
  auto ee_listener = CreateListener(/*must_be_posix=*/false);
  if (!ee_listener.ok()) {
    LogInitFailure(server_, "Bind failed", ee_listener.status());
    return ee_listener.status();
  }
  ee_listener_ = std::move(ee_listener.value());
  auto port_num = ee_listener_->Bind(addr);
  if (!port_num.ok()) {
    return port_num.status();
  }
  return port_num;
}

absl::Status ChaoticGoodServerListener::BindExternal(std::string addr,
                                                     const ChannelArgs& args) {
  using grpc_event_engine::experimental::EventEngine;
  class FdHandler final : public TcpServerFdHandler {
   public:
    FdHandler(RefCountedPtr<ChaoticGoodServerListener> listener,
              grpc_event_engine::experimental::ListenerSupportsFdExtension*
                  listener_supports_fd)
        : listener_(std::move(listener)),
          listener_supports_fd_(listener_supports_fd) {}

    void Handle(int listener_fd, int fd,
                grpc_byte_buffer* pending_read) override {
      grpc_event_engine::experimental::SliceBuffer pending_data;
      if (pending_read != nullptr) {
        pending_data =
            grpc_event_engine::experimental::SliceBuffer::TakeCSliceBuffer(
                pending_read->data.raw.slice_buffer);
      }
      CHECK(GRPC_LOG_IF_ERROR("listener_handle_external_connection",
                              listener_supports_fd_->HandleExternalConnection(
                                  listener_fd, fd, &pending_data)));
    }

   private:
    RefCountedPtr<ChaoticGoodServerListener> listener_;
    grpc_event_engine::experimental::ListenerSupportsFdExtension*
        listener_supports_fd_;
  };
  auto listener = CreateListener(/*must_be_posix=*/true);
  if (!listener.ok()) {
    LogInitFailure(server_, "BindExternal failed", listener.status());
    return listener.status();
  }
  auto* listener_supports_fd = grpc_event_engine::experimental::QueryExtension<
      grpc_event_engine::experimental::ListenerSupportsFdExtension>(
      listener->get());
  if (listener_supports_fd == nullptr) {
    LogInitFailure(server_,
                   "EventEngine does not support external fd listeners",
                   listener.status());
    return absl::InternalError(
        "EventEngine does not support external fd listeners");
  }
  ee_listener_ = std::move(*listener);
  TcpServerFdHandler** arg_val = args.GetPointer<TcpServerFdHandler*>(addr);
  *arg_val = new FdHandler(RefAsSubclass<ChaoticGoodServerListener>(),
                           listener_supports_fd);
  return absl::OkStatus();
}

absl::Status ChaoticGoodServerListener::StartListening() {
  CHECK(ee_listener_ != nullptr);
  auto status = ee_listener_->Start();
  if (!status.ok()) {
    LogInitFailure(server_, "Start listening failed", status);
  } else {
    GRPC_TRACE_LOG(chaotic_good, INFO) << "CHAOTIC_GOOD: Started listening";
  }
  return status;
}

ChaoticGoodServerListener::ActiveConnection::ActiveConnection(
    RefCountedPtr<ChaoticGoodServerListener> listener,
    std::unique_ptr<EventEngine::Endpoint> endpoint, bool is_external,
    int listener_fd, grpc_byte_buffer* pending_data)
    : listener_(std::move(listener)),
      acceptor_{nullptr, 0, 0, is_external, listener_fd, pending_data} {
  arena_->SetContext<grpc_event_engine::experimental::EventEngine>(
      listener_->event_engine_.get());
  handshaking_state_ = MakeRefCounted<HandshakingState>(Ref());
  handshaking_state_->Start(std::move(endpoint));
}

ChaoticGoodServerListener::ActiveConnection::~ActiveConnection() {
  if (receive_settings_activity_ != nullptr) receive_settings_activity_.reset();
  if (acceptor_.pending_data != nullptr) {
    grpc_byte_buffer_destroy(acceptor_.pending_data);
  }
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

ChaoticGoodServerListener::DataConnectionListener::DataConnectionListener(
    absl::AnyInvocable<std::string()> connection_id_generator,
    Duration connect_timeout,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : connection_id_generator_(std::move(connection_id_generator)),
      event_engine_(std::move(event_engine)),
      connect_timeout_(connect_timeout) {}

PendingConnection
ChaoticGoodServerListener::DataConnectionListener::RequestDataConnection() {
  MutexLock lock(&mu_);
  std::string connection_id;
  while (true) {
    connection_id = connection_id_generator_();
    if (!pending_connections_.contains(connection_id)) break;
  }
  if (shutdown_) {
    return PendingConnection(connection_id, []() {
      return absl::UnavailableError("Server shutdown");
    });
  }
  auto latch = std::make_shared<PromiseEndpointLatch>();
  auto timeout_task = event_engine_->RunAfter(
      connect_timeout_,
      [connection_id, self = WeakRefAsSubclass<DataConnectionListener>()]() {
        self->ConnectionTimeout(connection_id);
      });
  pending_connections_.emplace(connection_id,
                               PendingConnectionInfo{latch, timeout_task});
  return PendingConnection(connection_id,
                           Map(latch->Wait(), [latch](auto x) { return x; }));
}

ChaoticGoodServerListener::DataConnectionListener::PromiseEndpointLatchPtr
ChaoticGoodServerListener::DataConnectionListener::Extract(
    absl::string_view id) {
  MutexLock lock(&mu_);
  auto ex = pending_connections_.extract(id);
  if (!ex.empty()) {
    event_engine_->Cancel(ex.mapped().timeout);
    return std::move(ex.mapped().latch);
  }
  return nullptr;
}

void ChaoticGoodServerListener::DataConnectionListener::ConnectionTimeout(
    absl::string_view id) {
  auto latch = Extract(id);
  if (latch != nullptr) {
    latch->Set(absl::DeadlineExceededError("Connection timeout"));
  }
}

void ChaoticGoodServerListener::DataConnectionListener::FinishDataConnection(
    absl::string_view id, PromiseEndpoint endpoint) {
  auto latch = Extract(id);
  if (latch != nullptr) {
    latch->Set(std::move(endpoint));
  }
}

void ChaoticGoodServerListener::DataConnectionListener::Orphaned() {
  absl::flat_hash_map<std::string, PendingConnectionInfo> pending_connections;
  {
    MutexLock lock(&mu_);
    CHECK(!shutdown_);
    pending_connections = std::move(pending_connections_);
    pending_connections_.clear();
    shutdown_ = true;
  }
  for (const auto& conn : pending_connections) {
    event_engine_->Cancel(conn.second.timeout);
    conn.second.latch->Set(absl::UnavailableError("Server shutdown"));
  }
}

void ChaoticGoodServerListener::ActiveConnection::Done() {
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
  CoreConfiguration::Get().handshaker_registry().AddHandshakers(
      HANDSHAKER_SERVER, connection_->args(), nullptr, handshake_mgr_.get());
  RefCountedPtr<channelz::BaseNode> base_node =
      connection_->listener_->server_->channelz_node()->Ref();
  handshake_mgr_->DoHandshake(
      OrphanablePtr<grpc_endpoint>(
          grpc_event_engine_endpoint_create(std::move(endpoint))),
      connection_->args().SetObject(std::move(base_node)),
      Timestamp::Now() + connection_->listener_->data_connection_listener_
                             ->connection_timeout(),
      &connection_->acceptor_,
      [self = Ref()](absl::StatusOr<HandshakerArgs*> result) {
        self->OnHandshakeDone(std::move(result));
      });
}

auto ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    EndpointReadSettingsFrame(RefCountedPtr<HandshakingState> self) {
  return TrySeq(
      self->connection_->endpoint_.ReadSlice(TcpFrameHeader::kFrameHeaderSize),
      [self](Slice slice) {
        // Parse frame header
        auto frame_header =
            TcpFrameHeader::Parse(reinterpret_cast<const uint8_t*>(
                GRPC_SLICE_START_PTR(slice.c_slice())));
        if (frame_header.ok()) {
          if (frame_header->header.type != FrameType::kSettings) {
            frame_header = absl::InternalError("Not a settings frame");
          } else if (frame_header->payload_tag != 0) {
            frame_header = absl::InternalError("Unexpected connection id");
          } else if (frame_header->header.stream_id != 0) {
            frame_header = absl::InternalError("Unexpected stream id");
          }
        }
        return If(
            frame_header.ok(),
            [self, &frame_header]() {
              return TrySeq(
                  self->connection_->endpoint_.Read(
                      frame_header->header.payload_length),
                  [frame_header = *frame_header,
                   self](SliceBuffer buffer) -> absl::StatusOr<bool> {
                    // Read Setting frame.
                    SettingsFrame frame;
                    // Deserialize frame from read buffer.
                    auto status = frame.Deserialize(frame_header.header,
                                                    std::move(buffer));
                    if (!status.ok()) return status;
                    if (frame.body.data_channel()) {
                      if (frame.body.connection_id().empty()) {
                        return absl::UnavailableError(
                            "no connection id in data endpoint settings frame");
                      }
                      if (frame.body.connection_id().size() != 1) {
                        return absl::UnavailableError(absl::StrCat(
                            "Got ", frame.body.connection_id().size(),
                            " connection ids in data endpoint "
                            "settings frame (expect one)"));
                      }
                      self->data_.emplace<DataConnection>(
                          frame.body.connection_id()[0]);
                    } else {
                      Config config{self->connection_->args()};
                      auto settings_status =
                          config.ReceiveClientIncomingSettings(frame.body);
                      if (!settings_status.ok()) return settings_status;
                      const int num_data_connections =
                          self->connection_->listener_->args()
                              .GetInt(GRPC_ARG_CHAOTIC_GOOD_DATA_CONNECTIONS)
                              .value_or(1);
                      auto& data_connection_listener =
                          *self->connection_->listener_
                               ->data_connection_listener_;
                      for (int i = 0; i < num_data_connections; i++) {
                        config.ServerAddPendingDataEndpoint(
                            data_connection_listener.RequestDataConnection());
                      }
                      self->data_.emplace<ControlConnection>(std::move(config));
                    }
                    return !frame.body.data_channel();
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
    ControlEndpointWriteSettingsFrame(RefCountedPtr<HandshakingState> self) {
  SettingsFrame frame;
  frame.body.set_data_channel(false);
  std::get<ControlConnection>(self->data_)
      .config.PrepareServerOutgoingSettings(frame.body);
  SliceBuffer write_buffer;
  TcpFrameHeader{frame.MakeHeader(), 0}.Serialize(
      write_buffer.AddTiny(TcpFrameHeader::kFrameHeaderSize));
  frame.SerializePayload(write_buffer);
  return TrySeq(
      self->connection_->endpoint_.Write(std::move(write_buffer),
                                         PromiseEndpoint::WriteArgs{}),
      [self]() {
        auto config =
            std::move(std::get<ControlConnection>(self->data_).config);
        auto& ep = self->connection_->endpoint_;
        auto socket_node =
            TcpFrameTransport::MakeSocketNode(self->connection_->args(), ep);
        auto frame_transport = MakeOrphanable<TcpFrameTransport>(
            config.MakeTcpFrameTransportOptions(), std::move(ep),
            config.TakePendingDataEndpoints(),
            MakeRefCounted<TransportContext>(
                self->connection_->handshake_result_args(),
                std::move(socket_node)));
        return self->connection_->listener_->server_->SetupTransport(
            new ChaoticGoodServerTransport(
                self->connection_->handshake_result_args(),
                std::move(frame_transport), config.MakeMessageChunker()),
            nullptr, self->connection_->handshake_result_args());
      });
}

auto ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    DataEndpointWriteSettingsFrame(RefCountedPtr<HandshakingState> self) {
  // Send data endpoint setting frame
  SettingsFrame frame;
  frame.body.set_data_channel(true);
  SliceBuffer write_buffer;
  TcpFrameHeader{frame.MakeHeader(), 0}.Serialize(
      write_buffer.AddTiny(TcpFrameHeader::kFrameHeaderSize));
  frame.SerializePayload(write_buffer);
  // ignore encoding errors: they will be logged separately already
  return TrySeq(self->connection_->endpoint_.Write(
                    std::move(write_buffer), PromiseEndpoint::WriteArgs{}),
                [self]() mutable {
                  self->connection_->listener_->data_connection_listener_
                      ->FinishDataConnection(
                          std::get<DataConnection>(self->data_).connection_id,
                          std::move(self->connection_->endpoint_));
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
    connection_->listener_->LogConnectionFailure("Handshake failed",
                                                 result.status());
    connection_->Done();
    return;
  }
  CHECK_NE(*result, nullptr);
  if ((*result)->endpoint == nullptr) {
    connection_->listener_->LogConnectionFailure(
        "Server handshake done but has empty endpoint", std::nullopt);
    connection_->Done();
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
  connection_->handshake_result_args_ = (*result)->args;
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
          self->connection_->listener_->LogConnectionFailure(
              "Chaotic Good handshake failed", status);
        }
        self->connection_->Done();
      },
      connection_->arena_.get());
  MutexLock lock(&connection_->mu_);
  if (connection_->orphaned_) return;
  connection_->receive_settings_activity_ = std::move(activity);
}

void ChaoticGoodServerListener::Orphan() {
  GRPC_TRACE_LOG(chaotic_good, INFO) << "ChaoticGoodServerListener::Orphan()";
  {
    absl::flat_hash_set<OrphanablePtr<ActiveConnection>> connection_list;
    MutexLock lock(&mu_);
    connection_list = std::move(connection_list_);
    connection_list_.clear();
    shutdown_ = true;
  }
  ee_listener_.reset();
  Unref();
};

absl::StatusOr<int> AddChaoticGoodPort(Server* server, std::string addr,
                                       const ChannelArgs& args) {
  if (!IsChaoticGoodFramingLayerEnabled()) {
    return chaotic_good_legacy::AddLegacyChaoticGoodPort(server, addr, args);
  }
  if (absl::StartsWith(addr, "external:")) {
    auto listener =
        MakeOrphanable<chaotic_good::ChaoticGoodServerListener>(server, args);
    if (auto status = listener->BindExternal(addr, args); !status.ok()) {
      return status;
    }
    server->AddListener(std::move(listener));
    return -1;
  }
  using grpc_event_engine::experimental::EventEngine;
  const std::string parsed_addr = URI::PercentDecode(addr);
  absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> results =
      std::vector<EventEngine::ResolvedAddress>();
  if (IsEventEngineDnsNonClientChannelEnabled() &&
      !grpc_event_engine::experimental::
          EventEngineExperimentDisabledForPython()) {
    absl::StatusOr<std::unique_ptr<EventEngine::DNSResolver>> ee_resolver =
        args.GetObjectRef<EventEngine>()->GetDNSResolver(
            EventEngine::DNSResolver::ResolverOptions());
    if (!ee_resolver.ok()) {
      LogInitFailure(server, absl::StrCat("Failed to resolve ", addr),
                     ee_resolver.status());
      return ee_resolver.status();
    }
    results = grpc_event_engine::experimental::LookupHostnameBlocking(
        ee_resolver->get(), parsed_addr, absl::StrCat(0xd20));
    if (!results.ok()) {
      LogInitFailure(server, absl::StrCat("Failed to resolve ", addr),
                     results.status());
      return results.status();
    }
  } else {
    // TODO(yijiem): Remove this after event_engine_dns_non_client_channel
    // is fully enabled.
    const auto resolved_or = GetDNSResolver()->LookupHostnameBlocking(
        parsed_addr, absl::StrCat(0xd20));
    if (!resolved_or.ok()) {
      LogInitFailure(server, absl::StrCat("Failed to resolve ", addr),
                     resolved_or.status());
      return resolved_or.status();
    }
    for (const auto& addr : *resolved_or) {
      results->push_back(
          grpc_event_engine::experimental::CreateResolvedAddress(addr));
    }
  }
  int port_num = 0;
  std::vector<std::pair<std::string, absl::Status>> error_list;
  for (const auto& ee_addr : results.value()) {
    auto listener =
        MakeOrphanable<chaotic_good::ChaoticGoodServerListener>(server, args);
    std::string addr_str =
        *grpc_event_engine::experimental::ResolvedAddressToString(ee_addr);
    GRPC_TRACE_LOG(chaotic_good, INFO) << "BIND: " << addr_str;
    auto bind_result = listener->Bind(ee_addr);
    if (!bind_result.ok()) {
      LogInitFailure(server, absl::StrCat("Failed to bind ", addr_str),
                     bind_result.status());
      error_list.push_back(
          std::pair(std::move(addr_str), bind_result.status()));
      continue;
    }
    if (port_num == 0) {
      port_num = bind_result.value();
    } else {
      CHECK(port_num == bind_result.value());
    }
    server->AddListener(std::move(listener));
  }
  if (error_list.size() == results->size()) {
    LogInitFailure(server,
                   absl::StrCat("Failed to bind any address for ", addr),
                   std::nullopt);
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

}  // namespace chaotic_good
}  // namespace grpc_core
