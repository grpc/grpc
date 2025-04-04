//
//
// Copyright 2017 gRPC authors.
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

#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <chrono>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_create.h"
#include "src/core/server/server.h"
#include "src/core/telemetry/stats.h"
#include "src/core/util/notification.h"
#include "src/cpp/client/create_channel_internal.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace testing {

namespace {
using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::ThreadedFuzzingEventEngine;
using grpc_event_engine::experimental::URIToResolvedAddress;

void* tag(intptr_t x) { return reinterpret_cast<void*>(x); }

constexpr int kIterations = 1000;
constexpr int kSnapshotEvery = kIterations / 10;
}  // namespace

class InProcessCHTTP2 {
 public:
  InProcessCHTTP2(Service* service, std::string& addr,
                  ThreadedFuzzingEventEngine* fuzzing_engine) {
    // TODO(hork): move to a endpoint pair helper
    // Creating the listener
    grpc_core::Notification listener_started;
    std::unique_ptr<EventEngine::Endpoint> listener_endpoint;
    grpc_core::ChannelArgs args;
    grpc_event_engine::experimental::ChannelArgsEndpointConfig config(args);
    auto listener = fuzzing_engine->CreateListener(
        [&](std::unique_ptr<EventEngine::Endpoint> ep,
            grpc_core::MemoryAllocator) {
          listener_endpoint = std::move(ep);
          listener_started.Notify();
        },
        [](absl::Status status) { CHECK(status.ok()); }, config,
        std::make_unique<grpc_core::MemoryQuota>("foo"));
    if (!listener.ok()) {
      grpc_core::Crash(absl::StrCat("failed to start listener: ",
                                    listener.status().ToString()));
    }
    auto target_addr = URIToResolvedAddress(addr);
    CHECK(target_addr.ok());
    CHECK((*listener)->Bind(*target_addr).ok());
    CHECK((*listener)->Start().ok());
    // Creating the client
    std::unique_ptr<EventEngine::Endpoint> client_endpoint;
    grpc_core::Notification client_connected;
    auto client_memory_quota =
        std::make_unique<grpc_core::MemoryQuota>("client");
    std::ignore = fuzzing_engine->Connect(
        [&](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> endpoint) {
          CHECK(endpoint.ok());
          client_endpoint = std::move(*endpoint);
          client_connected.Notify();
        },
        *target_addr, config,
        client_memory_quota->CreateMemoryAllocator("conn-1"),
        grpc_core::Duration::Infinity());
    client_connected.WaitForNotification();
    listener_started.WaitForNotification();
    ServerBuilder b;
    cq_ = b.AddCompletionQueue(true);
    b.RegisterService(service);
    b.SetMaxReceiveMessageSize(INT_MAX);
    b.SetMaxSendMessageSize(INT_MAX);
    server_ = b.BuildAndStart();
    grpc_core::ExecCtx exec_ctx;
    // add server endpoint to server_
    {
      grpc_core::Server* core_server =
          grpc_core::Server::FromC(server_->c_server());
      grpc_core::OrphanablePtr<grpc_endpoint> iomgr_server_endpoint(
          grpc_event_engine_endpoint_create(std::move(listener_endpoint)));
      for (grpc_pollset* pollset : core_server->pollsets()) {
        grpc_endpoint_add_to_pollset(iomgr_server_endpoint.get(), pollset);
      }
      grpc_core::Transport* transport = grpc_create_chttp2_transport(
          core_server->channel_args(), std::move(iomgr_server_endpoint),
          /*is_client=*/false);
      CHECK(GRPC_LOG_IF_ERROR(
          "SetupTransport",
          core_server->SetupTransport(transport, nullptr,
                                      core_server->channel_args(), nullptr)));
      grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr,
                                          nullptr);
    }
    // create channel
    {
      grpc_core::ChannelArgs args =
          grpc_core::CoreConfiguration::Get()
              .channel_args_preconditioning()
              .PreconditionChannelArgs(nullptr)
              .Set(GRPC_ARG_DEFAULT_AUTHORITY, "test.authority");
      args = args.Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, INT_MAX)
                 .Set(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, INT_MAX)
                 .Set(GRPC_ARG_HTTP2_BDP_PROBE, 0);
      grpc_core::OrphanablePtr<grpc_endpoint> endpoint(
          grpc_event_engine_endpoint_create(std::move(client_endpoint)));
      grpc_core::Transport* transport = grpc_create_chttp2_transport(
          args, std::move(endpoint), /*is_client=*/true);
      CHECK(transport);
      grpc_channel* channel =
          grpc_core::ChannelCreate("target", args, GRPC_CLIENT_DIRECT_CHANNEL,
                                   transport)
              ->release()
              ->c_ptr();
      grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr,
                                          nullptr);
      channel_ = grpc::CreateChannelInternal(
          "", channel,
          std::vector<std::unique_ptr<
              experimental::ClientInterceptorFactoryInterface>>());
    }
  }

  virtual ~InProcessCHTTP2() {
    server_->Shutdown();
    cq_->Shutdown();
    void* tag;
    bool ok;
    while (cq_->Next(&tag, &ok)) {
    }
  }

  ServerCompletionQueue* cq() { return cq_.get(); }
  std::shared_ptr<Channel> channel() { return channel_; }

 private:
  std::unique_ptr<Server> server_;
  std::unique_ptr<ServerCompletionQueue> cq_;
  std::shared_ptr<Channel> channel_;
};

static double UnaryPingPong(ThreadedFuzzingEventEngine* fuzzing_engine,
                            int request_size, int response_size) {
  EchoTestService::AsyncService service;
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  std::unique_ptr<InProcessCHTTP2> fixture(
      new InProcessCHTTP2(&service, target_addr, fuzzing_engine));
  EchoRequest send_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  if (request_size > 0) {
    send_request.set_message(std::string(request_size, 'a'));
  }
  if (response_size > 0) {
    send_response.set_message(std::string(response_size, 'a'));
  }
  Status recv_status;
  struct ServerEnv {
    ServerContext ctx;
    EchoRequest recv_request;
    grpc::ServerAsyncResponseWriter<EchoResponse> response_writer;
    ServerEnv() : response_writer(&ctx) {}
  };
  uint8_t server_env_buffer[2 * sizeof(ServerEnv)];
  ServerEnv* server_env[2] = {
      reinterpret_cast<ServerEnv*>(server_env_buffer),
      reinterpret_cast<ServerEnv*>(server_env_buffer + sizeof(ServerEnv))};
  new (server_env[0]) ServerEnv;
  new (server_env[1]) ServerEnv;
  service.RequestEcho(&server_env[0]->ctx, &server_env[0]->recv_request,
                      &server_env[0]->response_writer, fixture->cq(),
                      fixture->cq(), tag(0));
  service.RequestEcho(&server_env[1]->ctx, &server_env[1]->recv_request,
                      &server_env[1]->response_writer, fixture->cq(),
                      fixture->cq(), tag(1));
  std::unique_ptr<EchoTestService::Stub> stub(
      EchoTestService::NewStub(fixture->channel()));
  auto baseline = grpc_core::global_stats().Collect();
  auto snapshot = grpc_core::global_stats().Collect();
  auto last_snapshot = absl::Now();
  for (int iteration = 0; iteration < kIterations; iteration++) {
    if (iteration > 0 && iteration % kSnapshotEvery == 0) {
      auto new_snapshot = grpc_core::global_stats().Collect();
      auto diff = new_snapshot->Diff(*snapshot);
      auto now = absl::Now();
      LOG(ERROR) << "  SNAPSHOT: UnaryPingPong(" << request_size << ", "
                 << response_size << "): writes_per_iteration="
                 << static_cast<double>(diff->syscall_write) /
                        static_cast<double>(kSnapshotEvery)
                 << " (total=" << diff->syscall_write << ", i=" << iteration
                 << ") pings=" << diff->http2_pings_sent
                 << "; duration=" << now - last_snapshot;
      last_snapshot = now;
      snapshot = std::move(new_snapshot);
    }
    recv_response.Clear();
    ClientContext cli_ctx;
    std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
        stub->AsyncEcho(&cli_ctx, send_request, fixture->cq()));
    void* t;
    bool ok;
    response_reader->Finish(&recv_response, &recv_status, tag(4));
    CHECK(fixture->cq()->Next(&t, &ok));
    CHECK(ok);
    CHECK(t == tag(0) || t == tag(1)) << "Found unexpected tag " << t;
    intptr_t slot = reinterpret_cast<intptr_t>(t);
    ServerEnv* senv = server_env[slot];
    senv->response_writer.Finish(send_response, Status::OK, tag(3));
    for (int i = (1 << 3) | (1 << 4); i != 0;) {
      CHECK(fixture->cq()->Next(&t, &ok));
      CHECK(ok);
      int tagnum = static_cast<int>(reinterpret_cast<intptr_t>(t));
      CHECK(i & (1 << tagnum));
      i -= 1 << tagnum;
    }
    CHECK(recv_status.ok());

    senv->~ServerEnv();
    senv = new (senv) ServerEnv();
    service.RequestEcho(&senv->ctx, &senv->recv_request, &senv->response_writer,
                        fixture->cq(), fixture->cq(), tag(slot));
  }
  auto end_stats = grpc_core::global_stats().Collect()->Diff(*baseline);
  double writes_per_iteration =
      end_stats->syscall_write / static_cast<double>(kIterations);
  VLOG(2) << "UnaryPingPong(" << request_size << ", " << response_size
          << "): writes_per_iteration=" << writes_per_iteration
          << " (total=" << end_stats->syscall_write << ")";

  fixture.reset();
  server_env[0]->~ServerEnv();
  server_env[1]->~ServerEnv();

  return writes_per_iteration;
}

TEST(WritesPerRpcTest, UnaryPingPong) {
  auto fuzzing_engine = std::dynamic_pointer_cast<
      grpc_event_engine::experimental::ThreadedFuzzingEventEngine>(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  EXPECT_LT(UnaryPingPong(fuzzing_engine.get(), 0, 0), 2.2);
  EXPECT_LT(UnaryPingPong(fuzzing_engine.get(), 1, 0), 2.2);
  EXPECT_LT(UnaryPingPong(fuzzing_engine.get(), 0, 1), 2.2);
  EXPECT_LT(UnaryPingPong(fuzzing_engine.get(), 4096, 0), 2.5);
  EXPECT_LT(UnaryPingPong(fuzzing_engine.get(), 0, 4096), 2.5);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_event_engine::experimental::DefaultEventEngineScope engine_scope(
      std::make_shared<
          grpc_event_engine::experimental::ThreadedFuzzingEventEngine>(
          std::chrono::milliseconds(1)));
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
