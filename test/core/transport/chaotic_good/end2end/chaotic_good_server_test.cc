#include "src/core/ext/transport/chaotic_good/server/chaotic_good_server.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice.h>  // IWYU pragma: keep
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpcpp/server.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/promise/wait_for_callback.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "test/core/util/port.h"

namespace grpc_core {
namespace chaotic_good {
namespace testing {
class ChaoticGoodServerTest : public ::testing::Test {
 public:
  ChaoticGoodServerTest() {
    port_ = grpc_pick_unused_port_or_die();
    addr_ = absl::StrCat("ipv6:[::1]:", port_);
    ExecCtx exec_ctx;
    server_ = grpc_server_create(nullptr, nullptr);
    event_engine_ = std::shared_ptr<EventEngine>(
        ::grpc_event_engine::experimental::CreateEventEngine());
    channel_args_ = channel_args_.SetObject(event_engine_);
    resource_quota_ = ResourceQuota::Default();
    channel_args_ = channel_args_.Set(GRPC_ARG_RESOURCE_QUOTA, resource_quota_);
    config_ = ::grpc_event_engine::experimental::ChannelArgsEndpointConfig(
        channel_args_);
    memory_quota_ = std::make_unique<MemoryQuota>("test");
    memory_allocator_ = memory_quota_->CreateMemoryAllocator("test");
    arena_ = MakeScopedArena(initial_arena_size, &memory_allocator_);
    context_ = std::make_shared<promise_detail::Context<Arena>>((arena_.get()));
    timeout_ = EventEngine::Duration(60);
    resolved_addr_ =
        ::grpc_event_engine::experimental::URIToResolvedAddress(addr_);
    GPR_ASSERT(resolved_addr_.ok());
    core_server_ = Server::FromC(server_);
    hpack_compressor_ = std::make_unique<HPackCompressor>();
    hpack_parser_ = std::make_unique<HPackParser>();
    std::cout << "server init"
              << "\n";
    fflush(stdout);
  }
  ~ChaoticGoodServerTest() {
    if (connect_activity_ != nullptr) connect_activity_.reset();
  }

 protected:
  grpc_server* server_;
  Server* core_server_;
  ChannelArgs channel_args_;
  int port_;
  std::string addr_;
  absl::StatusOr<
      ::grpc_event_engine::experimental::EventEngine::ResolvedAddress>
      resolved_addr_;
  ::grpc_event_engine::experimental::ChannelArgsEndpointConfig config_;
  EventEngine::Duration timeout_;

  std::shared_ptr<EventEngine> event_engine_;
  std::unique_ptr<MemoryQuota> memory_quota_;
  ResourceQuotaRefPtr resource_quota_;
  size_t initial_arena_size = 1024;
  MemoryAllocator memory_allocator_;
  ScopedArenaPtr arena_;
  std::shared_ptr<promise_detail::Context<Arena>> context_;
  std::shared_ptr<PromiseEndpoint> control_endpoint_;
  std::shared_ptr<PromiseEndpoint> data_endpoint_;
  ActivityPtr connect_activity_;
  std::unique_ptr<HPackCompressor> hpack_compressor_;
  std::unique_ptr<HPackParser> hpack_parser_;
};
TEST_F(ChaoticGoodServerTest, OneConnection) {
  auto result =
      ChaoticGoodServerAddPort(core_server_, addr_.c_str(), channel_args_);
  std::cout << "result: " << result << "\n";
  fflush(stdout);
  EXPECT_EQ(result, port_);
  absl::SleepFor(absl::Seconds(5));
  Notification connected;

  std::cout << "client control ednpoint address " << control_endpoint_ << "\n";
  fflush(stdout);

  auto latch_ = std::make_shared<Latch<std::shared_ptr<PromiseEndpoint>>>();
  auto w4cb_ = std::make_shared<WaitForCallback>();
  ::grpc_event_engine::experimental::EventEngine::OnConnectCallback on_connect =
      [&connected, this, latch_,
       w4cb_](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>
                  endpoint) mutable {
        std::cout << "client connect status: " << endpoint.status() << "\n";
        fflush(stdout);
        GPR_ASSERT(endpoint.ok());
        control_endpoint_ = std::make_shared<PromiseEndpoint>(
            std::move(endpoint.value()), SliceBuffer());
        std::cout << "client control ednpoint in connect address "
                  << control_endpoint_ << "\n";
        fflush(stdout);

        auto read_setting_frames = TrySeq(
            control_endpoint_->ReadSlice(FrameHeader::frame_header_size_),
            [this, latch_, w4cb_](Slice slice) mutable {
              // Parse frame header
              auto frame_header_ = std::make_shared<FrameHeader>(
                  FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
                                         GRPC_SLICE_START_PTR(slice.c_slice())))
                      .value());
              std::cout << "client control receive frame headers "
                        << frame_header_->GetFrameLength() << "\n";
              fflush(stdout);
              std::cout << "client control ednpoint in read settings address "
                        << control_endpoint_ << "\n";
              fflush(stdout);
              return TrySeq(
                  control_endpoint_->Read(frame_header_->GetFrameLength()),
                  [frame_header = frame_header_, this](SliceBuffer buffer) {
                    std::cout << "client deserialize setting frame "
                              << frame_header->header_length << "\n";
                    fflush(stdout);
                    // Read Setting frame.
                    SettingsFrame frame;
                    // Initialized to get this_cpu() info in
                    // global_stat().
                    ExecCtx exec_ctx;
                    // Deserialize frame from read buffer.
                    absl::BitGen bitgen;
                    auto status =
                        frame.Deserialize(hpack_parser_.get(), *frame_header,
                                          absl::BitGenRef(bitgen), buffer);
                    GPR_ASSERT(status.ok());
                    std::cout << "client control get setting frame "
                              << "\n";
                    fflush(stdout);
                    auto connection_id =
                        frame.headers
                            ->get_pointer(ChaoticGoodConnectionIdMetadata())
                            ->Ref();
                    std::cout << "client control get connection id "
                              << connection_id.as_string_view() << "\n";
                    fflush(stdout);
                    return connection_id;
                  },
                  [this, latch_, w4cb_](Slice connection_id) mutable {
                    // Connect to data endpoint.
                    ::grpc_event_engine::experimental::EventEngine::
                        OnConnectCallback second_connection =
                            [cb = w4cb_->MakeCallback(),
                             connection_id = std::move(connection_id),
                             latch_](absl::StatusOr<
                                     std::unique_ptr<EventEngine::Endpoint>>
                                         endpoint) mutable {
                              latch_->Set(std::make_shared<PromiseEndpoint>(
                                  std::move(endpoint.value()), SliceBuffer()));
                              std::cout << "second connection is set "
                                        << "\n";
                              fflush(stdout);
                              cb();
                            };
                    this->event_engine_->Connect(
                        std::move(second_connection), *resolved_addr_, config_,
                        memory_quota_->CreateMemoryAllocator("conn-1"),
                        timeout_);

                    return w4cb_->MakeWaitPromise();
                  },
                  Race(
                      TrySeq(
                          latch_->Wait(),
                          [this](std::shared_ptr<PromiseEndpoint>
                                     data_endpoint) mutable {
                            data_endpoint_.swap(data_endpoint);
                            auto write_promise = TrySeq(
                                [this]() mutable {
                                  GPR_ASSERT(data_endpoint_ != nullptr);
                                  std::cout
                                      << "data endpoint connect, both endpoints"
                                      << "\n";
                                  fflush(stdout);
                                  SettingsFrame frame;
                                  // frame.header set connectiion_type: control
                                  ClientMetadataHandle metadata =
                                      arena_->MakePooled<ClientMetadata>(
                                          arena_.get());
                                  metadata->Set(
                                      ChaoticGoodConnectionTypeMetadata(),
                                      Slice::FromCopiedString("data"));
                                  auto connection_type =
                                      metadata
                                          ->get_pointer(
                                              ChaoticGoodConnectionTypeMetadata())
                                          ->Ref();
                                  metadata->Set(
                                      ChaoticGoodConnectionIdMetadata(),
                                      Slice::FromCopiedString(
                                          "randome_test_string"));
                                  frame.headers = std::move(metadata);

                                  auto write_buffer =
                                      frame.Serialize(hpack_compressor_.get());
                                  auto context = std::make_shared<
                                      promise_detail::Context<Arena>>(
                                      (arena_.get()));
                                  GPR_ASSERT(context != nullptr);
                                  std::cout << "data endpoint get context"
                                            << "\n";
                                  fflush(stdout);
                                  std::cout << "client data send setting frame:"
                                            << connection_type.as_string_view()
                                            << "\n";
                                  fflush(stdout);
                                  return data_endpoint_->Write(
                                      std::move(write_buffer));
                                },
                                []() -> absl::Status {
                                  return absl::OkStatus();
                                });
                            auto read_promise = TrySeq(
                                data_endpoint_->ReadSlice(
                                    FrameHeader::frame_header_size_),
                                [this](Slice slice) mutable {
                                  // Read setting frame;
                                  // Parse frame header
                                  GPR_ASSERT(data_endpoint_ != nullptr);
                                  auto frame_header_ =
                                      std::make_shared<FrameHeader>(
                                          FrameHeader::Parse(
                                              reinterpret_cast<const uint8_t*>(
                                                  GRPC_SLICE_START_PTR(
                                                      slice.c_slice())))
                                              .value());
                                  std::cout << "client data receive frame "
                                               "headers "
                                            << frame_header_->GetFrameLength()
                                            << "\n";
                                  fflush(stdout);
                                  auto frame_header_length =
                                      frame_header_->GetFrameLength();
                                  // TODO(ladynana): add transport;
                                  return data_endpoint_->Read(
                                      frame_header_length);
                                },
                                [](SliceBuffer buffer) {
                                  std::cout << "client data receive slice "
                                            << buffer.Length() << "\n";
                                  fflush(stdout);
                                  return absl::OkStatus();
                                });
                            return TrySeq(TryJoin(std::move(write_promise),
                                                  std::move(read_promise)),
                                          []() -> absl::Status {
                                            return absl::OkStatus();
                                          });
                          }),
                      TrySeq(Sleep(Timestamp::Now() + Duration::Seconds(60)),
                             []() -> absl::Status {
                               std::cout << "client data deadline exceed "
                                         << "\n";
                               fflush(stdout);
                               return absl::DeadlineExceededError(
                                   "Data endpoint connect deadline excced.");
                             })),
                  []() {
                    std::cout << "client data endpoint connect done"
                              << "\n";
                    fflush(stdout);
                    // connected.Notify();
                    return absl::OkStatus();
                  });
            });
        auto send_setting_frames = TrySeq(
            [this]() mutable {
              SettingsFrame frame;
              // frame.header set connectiion_type: control
              ClientMetadataHandle metadata =
                  GetContext<Arena>()->MakePooled<ClientMetadata>(
                      GetContext<Arena>());
              metadata->Set(ChaoticGoodConnectionTypeMetadata(),
                            Slice::FromCopiedString("control"));
              auto connection_type =
                  metadata->get_pointer(ChaoticGoodConnectionTypeMetadata())
                      ->Ref();
              frame.headers = std::move(metadata);
              auto write_buffer = frame.Serialize(hpack_compressor_.get());
              std::cout << "client send setting frame:"
                        << connection_type.as_string_view() << "\n";
              fflush(stdout);
              return control_endpoint_->Write(std::move(write_buffer));
            },
            []() { return absl::OkStatus(); });

        connect_activity_ = MakeActivity(
            TrySeq(TryJoin(std::move(read_setting_frames),
                           std::move(send_setting_frames)),
                   []() { return absl::OkStatus(); }),
            EventEngineWakeupScheduler(
                ::grpc_event_engine::experimental::GetDefaultEventEngine()),
            [this, &connected](absl::Status status) {
              std::cout << "connect status: " << status << "\n";
              fflush(stdout);
              std::cout << "client control ednpoint in activity address "
                        << control_endpoint_ << "\n";
              fflush(stdout);
              GPR_ASSERT(control_endpoint_ != nullptr);
              std::cout << "connect.notify"
                        << "\n";
              fflush(stdout);
              connected.Notify();
            },
            this->arena_.get(), this->event_engine_.get());
      };

  event_engine_->Connect(std::move(on_connect), *resolved_addr_, config_,
                         memory_quota_->CreateMemoryAllocator("conn-1"),
                         timeout_);
  connected.WaitForNotification();
}
}  // namespace testing
}  // namespace chaotic_good
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
