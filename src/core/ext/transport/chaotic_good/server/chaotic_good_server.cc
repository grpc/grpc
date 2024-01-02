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

ChaoticGoodServerListener::ChaoticGoodServerListener(Server* server,
                                                     const ChannelArgs& args)
    : server_(server),
      args_(args),
      config_(args),
      event_engine_(args_.GetObject<EventEngine>()),
      hpack_compressor_(std::make_unique<HPackCompressor>()),
      hpack_parser_(std::make_unique<HPackParser>()) {}

absl::StatusOr<int> ChaoticGoodServerListener::Bind(const char* addr) {
  EventEngine::Listener::AcceptCallback accept_cb =
      [this](std::unique_ptr<EventEngine::Endpoint> ep,
             MemoryAllocator memory_allocator) {
        std::cout << "accept connection start on accept callback "
                  << "\n";
        fflush(stdout);
        // Create connection
        auto listener_ref = this->Ref();
        auto connection = new ActiveConnection(std::move(listener_ref));
        std::cout << "start connection "
                  << "\n";
        fflush(stdout);
        connection->Start(std::move(ep));
      };
  auto on_shutdown = [](absl::Status status) {
    // TODO(ladynana): add condition of status not ok.
    GPR_ASSERT(status.ok());
  };
  GPR_ASSERT(event_engine_ != nullptr);
  auto ee_listener = event_engine_->CreateListener(
      std::move(accept_cb), std::move(on_shutdown), config_,
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
    RefCountedPtr<ChaoticGoodServerListener> listener)
    : listener_(listener),
      memory_allocator_(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "connection")),
      arena_(MakeScopedArena(1024, &memory_allocator_)),
      context_(arena_.get()),
      handshaking_state_(std::make_shared<HandshakingState>(Ref())) {}

void ChaoticGoodServerListener::ActiveConnection::Start(
    std::unique_ptr<EventEngine::Endpoint> endpoint) {
  GPR_ASSERT(handshaking_state_ != nullptr);
  handshaking_state_->Start(std::move(endpoint));
}

ChaoticGoodServerListener::ActiveConnection::HandshakingState::HandshakingState(
    RefCountedPtr<ActiveConnection> connection)
    : connection_(connection),
      handshake_mgr_(std::make_shared<HandshakeManager>()),
      arena_(connection_->arena_.get()),
      context_(arena_.get()) {}

void ChaoticGoodServerListener::ActiveConnection::HandshakingState::Start(
    std::unique_ptr<EventEngine::Endpoint> endpoint) {
  // TODO(ladynana): Add another DoHandshake() for EventEngine::Endpoint
  // directly.
  std::cout << "start handshake"
            << "\n";
  fflush(stdout);
  auto context = GetContext<Arena>();
  GPR_ASSERT(context != nullptr);
  std::cout << "get context"
            << "\n";
  fflush(stdout);
  handshake_mgr_->DoHandshake(
      grpc_event_engine_endpoint_create(std::move(endpoint)),
      connection_->args(), GetConnectionDeadline(connection_->args()), nullptr,
      OnHandshakeDone, this);
}
void ChaoticGoodServerListener::ActiveConnection::HandshakingState::
    OnHandshakeDone(void* arg, grpc_error_handle error) {
  // TODO(ladynana): add condition of error not ok.
  GPR_ASSERT(error.ok());
  auto* args = static_cast<HandshakerArgs*>(arg);
  GPR_ASSERT(args != nullptr);
  GPR_ASSERT(args->endpoint != nullptr);
  GPR_ASSERT(grpc_event_engine::experimental::grpc_is_event_engine_endpoint(
      args->endpoint));
  // Start receiving setting frames;
  HandshakingState* self = static_cast<HandshakingState*>(args->user_data);
  self->connection_->endpoint_ = std::make_shared<PromiseEndpoint>(
      std::unique_ptr<EventEngine::Endpoint>(
          grpc_event_engine::experimental::
              grpc_get_wrapped_event_engine_endpoint(args->endpoint)),
      SliceBuffer());
  std::cout << "handshake done "
            << "\n";
  fflush(stdout);
  self->connection_->receive_setting_frames_ =
      OnReceive(self->connection_->endpoint_, self);
}

ActivityPtr
ChaoticGoodServerListener::ActiveConnection::HandshakingState::OnReceive(
    std::shared_ptr<PromiseEndpoint> endpoint, HandshakingState* self) {
  std::cout << "server start receiving "
            << "\n";
  fflush(stdout);
  auto on_receive_frames = TrySeq(
      // TODO(ladynana): remove memcpy in ReadSlice.
      endpoint->ReadSlice(FrameHeader::frame_header_size_),
      [endpoint = endpoint, self = self](Slice slice) mutable {
        // Parse frame header
        auto frame_header = std::make_shared<FrameHeader>(
            FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
                                   GRPC_SLICE_START_PTR(slice.c_slice())))
                .value());
        std::cout << "server receive frame headers "
                  << frame_header->GetFrameLength() << "\n";
        fflush(stdout);
        return TrySeq(
            endpoint->Read(frame_header->GetFrameLength()),
            [frame_header = frame_header, self = self,
             endpoint = endpoint](SliceBuffer buffer) mutable {
              std::cout << "deserialize setting frame "
                        << frame_header->header_length << "\n";
              fflush(stdout);
              // Read Setting frame.
              SettingsFrame frame;
              // Initialized to get this_cpu() info in global_stat().
              ExecCtx exec_ctx;
              // Deserialize frame from read buffer.
              absl::BitGen bitgen;
              auto status = frame.Deserialize(
                  self->connection_->listener_->hpack_parser_.get(),
                  *frame_header, absl::BitGenRef(bitgen), buffer);
              GPR_ASSERT(status.ok());
              auto connection_type =
                  frame.headers
                      ->get_pointer(ChaoticGoodConnectionTypeMetadata())
                      ->Ref();
              // GPR_ASSERT(connection_type);
              std::cout << "get connection type "
                        << connection_type.as_string_view() << "\n";
              fflush(stdout);
              bool is_control_endpoint =
                  std::string(connection_type.as_string_view()) == "control";
              std::cout << "is control endpoint " << is_control_endpoint
                        << "\n";
              fflush(stdout);
              std::shared_ptr<Slice> connection_id_;
              if (!is_control_endpoint) {
                connection_id_ = std::make_shared<Slice>(
                    frame.headers
                        ->get_pointer(ChaoticGoodConnectionIdMetadata())
                        ->Ref());
                std::cout << "server get connection id "
                          << connection_id_->as_string_view() << "\n";
                fflush(stdout);
              }
              return If(
                  is_control_endpoint,
                  // connection_type = control
                  [self = self, endpoint = endpoint]() mutable {
                    // TODO(ladynana): generate connection id.
                    auto connection_id = "randome_test_string";
                    // Add a wait latch for data endpoint to connect.
                    auto latch = std::make_shared<
                        Latch<std::shared_ptr<PromiseEndpoint>>>();
                    {
                      MutexLock lock(&self->connection_->listener_->mu_);
                      // Add latch to map;
                      self->connection_->listener_->connectivity_map_.insert(
                          std::pair<std::string,
                                    std::shared_ptr<Latch<
                                        std::shared_ptr<PromiseEndpoint>>>>(
                              connection_id, std::move(latch)));
                    }
                    std::cout << "latch create " << connection_id << "\n";
                    fflush(stdout);
                    return Race(
                        TrySeq(
                            [endpoint = endpoint, self = self,
                             connection_id]() mutable {
                              SettingsFrame frame;
                              ClientMetadataHandle metadata =
                                  GetContext<Arena>()
                                      ->MakePooled<ClientMetadata>(
                                          GetContext<Arena>());
                              metadata->Set(
                                  ChaoticGoodConnectionIdMetadata(),
                                  Slice::FromCopiedString(connection_id));
                              frame.headers = std::move(metadata);
                              auto write_buffer = frame.Serialize(
                                  self->connection_->listener_
                                      ->hpack_compressor_.get());
                              std::cout << "server write " << connection_id
                                        << "\n";
                              fflush(stdout);
                              return endpoint->Write(std::move(write_buffer));
                            },
                            [self = self, connection_id]() mutable {
                              MutexLock lock(
                                  &self->connection_->listener_->mu_);
                              return self->connection_->listener_
                                  ->connectivity_map_.find(connection_id)
                                  ->second->Wait();
                              // GPR_ASSERT(latch != nullptr);
                              // std::cout << "latch wait " << connection_id
                              //           << "\n";
                              // fflush(stdout);
                              // return latch->Wait();
                            },
                            [](std::shared_ptr<PromiseEndpoint> ret)
                                -> absl::Status {
                              GPR_ASSERT(ret != nullptr);
                              std::cout << "server get both endpoint "
                                        << "\n";
                              fflush(stdout);
                              return absl::OkStatus();
                            }),
                        // Set timeout for waiting data endpoint connect.
                        TrySeq(
                            // []() {
                            Sleep(Timestamp::Now() + Duration::Seconds(60)),
                            [self = self,
                             connection_id]() mutable -> absl::Status {
                              std::cout << "server timeout "
                                        << Timestamp::Now().ToString() << "\n";
                              fflush(stdout);
                              MutexLock lock(
                                  &self->connection_->listener_->mu_);
                              // Delete connection id from map when timeout;
                              self->connection_->listener_->connectivity_map_
                                  .erase(connection_id);
                              return absl::DeadlineExceededError(
                                  Timestamp::Now().ToString());
                            }));
                  },
                  // connection_type = data.
                  TrySeq([self = self, endpoint = endpoint,
                          connection_id_]() mutable {
                    auto context = GetContext<Arena>();
                    GPR_ASSERT(context != nullptr);
                    std::cout << "get context"
                              << "\n";
                    fflush(stdout);
                    // Send data endpoint setting frame
                    SettingsFrame frame;
                    ClientMetadataHandle metadata =
                        GetContext<Arena>()->MakePooled<ClientMetadata>(
                            GetContext<Arena>());
                    metadata->Set(ChaoticGoodConnectionIdMetadata(),
                                  Slice::FromCopiedString(
                                      connection_id_->as_string_view()));
                    frame.headers = std::move(metadata);
                    auto write_buffer = frame.Serialize(
                        self->connection_->listener_->hpack_compressor_.get());
                    std::cout << "server write "
                              << connection_id_->as_string_view() << "\n";
                    fflush(stdout);
                    return TrySeq(
                        endpoint->Write(std::move(write_buffer)),
                        [self = self, connection_id_,
                         endpoint = endpoint]() mutable {
                          MutexLock lock(&self->connection_->listener_->mu_);
                          // Set endpoint to latch
                          self->connection_->listener_->connectivity_map_
                              .find(
                                  std::string(connection_id_->as_string_view()))
                              ->second->Set(endpoint);

                          std::cout << "server data endpoint set "
                                    << connection_id_->as_string_view() << "\n";
                          fflush(stdout);
                          return absl::OkStatus();
                        });
                  }));
            });
      });
  auto activity = MakeActivity(
      std::move(on_receive_frames),
      EventEngineWakeupScheduler(
          grpc_event_engine::experimental::GetDefaultEventEngine()),
      [](absl::Status status) {
        std::cout << "server shutdown " << status << "\n";
        fflush(stdout);
        return absl::OkStatus();
      },
      // Hold Arena in activity for
      // GetContext<Arena> usage.
      MakeScopedArena(1024, &self->connection_->memory_allocator_),
      grpc_event_engine::experimental::GetDefaultEventEngine().get()

  );
  return activity;
}

int ChaoticGoodServerAddPort(Server* server, const char* addr,
                             const ChannelArgs& args) {
  std::cout << "start listener "
            << "\n";
  fflush(stdout);
  ExecCtx exec_ctx;
  auto* listener = new ChaoticGoodServerListener(server, args);

  auto port = listener->Bind(addr);

  if (!port.ok()) {
    gpr_log(GPR_ERROR, "%s", StatusToString(port.status()).c_str());
  }
  auto status = listener->StartListening();
  if (!status.ok()) {
    gpr_log(GPR_ERROR, "%s", StatusToString(status).c_str());
  }
  return port.value();
}
}  // namespace chaotic_good
}  // namespace grpc_core
