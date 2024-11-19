//
//
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
//
//

#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/core/ext/transport/chttp2/server/chttp2_server.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/security/credentials/insecure/insecure_credentials.h"
#include "src/core/server/server.h"
#include "src/core/util/host_port.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/test_util/mock_endpoint.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

using grpc_event_engine::experimental::EventEngine;

namespace grpc_core {
namespace testing {

class Chttp2ServerListenerTestPeer {
 public:
  explicit Chttp2ServerListenerTestPeer(NewChttp2ServerListener* listener)
      : listener_(listener) {}

  static OrphanablePtr<NewChttp2ServerListener> MakeListener(
      const ChannelArgs& args) {
    return MakeOrphanable<NewChttp2ServerListener>(args);
  }

  void OnAccept(grpc_endpoint* tcp, grpc_pollset* accepting_pollset,
                grpc_tcp_server_acceptor* server_acceptor) {
    NewChttp2ServerListener::OnAccept(listener_, tcp, accepting_pollset,
                                      server_acceptor);
  }

  RefCountedPtr<NewChttp2ServerListener> Ref() {
    return listener_->RefAsSubclass<NewChttp2ServerListener>();
  }

 private:
  NewChttp2ServerListener* listener_;
};

class ActiveConnectionTestPeer {
 public:
  explicit ActiveConnectionTestPeer(
      NewChttp2ServerListener::ActiveConnection* connection)
      : connection_(connection) {}

  void OnClose() {
    NewChttp2ServerListener::ActiveConnection::OnClose(connection_,
                                                       absl::OkStatus());
  }

 private:
  NewChttp2ServerListener::ActiveConnection* connection_;
};

class ServerTestPeer {
 public:
  explicit ServerTestPeer(Server* server) : server_(server) {}

  const std::list<RefCountedPtr<Server::ListenerState>>& listener_states()
      const {
    return server_->listener_states_;
  }

 private:
  Server* server_;
};

class Chttp2ServerListenerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    args_ = CoreConfiguration::Get()
                .channel_args_preconditioning()
                .PreconditionChannelArgs(nullptr);
    server_ = MakeOrphanable<Server>(args_);
    auto creds = MakeRefCounted<InsecureServerCredentials>();
    grpc_server_add_http2_port(
        server_->c_ptr(),
        JoinHostPort("localhost", grpc_pick_unused_port_or_die()).c_str(),
        creds.get());
    cq_ = grpc_completion_queue_create_for_next(/*reserved=*/nullptr);
    server_->RegisterCompletionQueue(cq_);
    grpc_server_start(server_->c_ptr());
    listener_state_ =
        ServerTestPeer(server_.get()).listener_states().front().get();
    listener_ = DownCast<NewChttp2ServerListener*>(listener_state_->listener());
  }

  void TearDown() override {
    CqVerifier cqv(cq_);
    grpc_server_shutdown_and_notify(server_->c_ptr(), cq_, CqVerifier::tag(-1));
    cqv.Expect(CqVerifier::tag(-1), true);
    cqv.Verify();
    server_.reset();
    grpc_completion_queue_destroy(cq_);
  }

  ChannelArgs args_;
  OrphanablePtr<Server> server_;
  Server::ListenerState* listener_state_;
  NewChttp2ServerListener* listener_;
  grpc_completion_queue* cq_ = nullptr;
};

TEST_F(Chttp2ServerListenerTest, Basic) {
  listener_state_->connection_quota()->SetMaxIncomingConnections(10);
  auto mock_endpoint_controller =
      grpc_event_engine::experimental::MockEndpointController::Create(
          args_.GetObjectRef<EventEngine>());
  Chttp2ServerListenerTestPeer(listener_).OnAccept(
      /*tcp=*/mock_endpoint_controller->TakeCEndpoint(),
      /*accepting_pollset=*/nullptr,
      /*server_acceptor=*/nullptr);
  EXPECT_EQ(
      listener_state_->connection_quota()->TestOnlyActiveIncomingConnections(),
      1);
}

TEST_F(Chttp2ServerListenerTest, NoConnectionQuota) {
  listener_state_->connection_quota()->SetMaxIncomingConnections(0);
  auto mock_endpoint_controller =
      grpc_event_engine::experimental::MockEndpointController::Create(
          args_.GetObjectRef<EventEngine>());
  Chttp2ServerListenerTestPeer(listener_).OnAccept(
      /*tcp=*/mock_endpoint_controller->TakeCEndpoint(),
      /*accepting_pollset=*/nullptr,
      /*server_acceptor=*/nullptr);
  EXPECT_EQ(
      listener_state_->connection_quota()->TestOnlyActiveIncomingConnections(),
      0);
}

TEST_F(Chttp2ServerListenerTest, ConnectionRefusedAfterShutdown) {
  listener_state_->connection_quota()->SetMaxIncomingConnections(10);
  // Take ref on listener to prevent destruction of listener
  RefCountedPtr<NewChttp2ServerListener> listener_ref =
      Chttp2ServerListenerTestPeer(listener_).Ref();
  grpc_server_shutdown_and_notify(server_->c_ptr(), cq_, CqVerifier::tag(1));
  auto mock_endpoint_controller =
      grpc_event_engine::experimental::MockEndpointController::Create(
          args_.GetObjectRef<EventEngine>());
  Chttp2ServerListenerTestPeer(listener_).OnAccept(
      /*tcp=*/mock_endpoint_controller->TakeCEndpoint(),
      /*accepting_pollset=*/nullptr,
      /*server_acceptor=*/nullptr);
  EXPECT_EQ(
      listener_state_->connection_quota()->TestOnlyActiveIncomingConnections(),
      0);
  // Let go of the ref to allow server shutdown to complete.
  {
    // TODO(yashykt): Remove ExecCtx when we are no longer using it for shutdown
    // notification.
    ExecCtx exec_ctx;
    listener_ref.reset();
  }
  CqVerifier cqv(cq_);
  cqv.Expect(CqVerifier::tag(1), true);
  cqv.Verify();
}

using Chttp2ActiveConnectionTest = Chttp2ServerListenerTest;

TEST_F(Chttp2ActiveConnectionTest, CloseReducesConnectionCount) {
  listener_state_->connection_quota()->SetMaxIncomingConnections(10);
  // Add a connection
  ASSERT_TRUE(listener_state_->connection_quota()->AllowIncomingConnection(
      listener_state_->memory_quota(), "peer"));
  auto connection = MakeOrphanable<NewChttp2ServerListener::ActiveConnection>(
      listener_state_->Ref(), /*tcp_server=*/nullptr,
      /*accepting_pollset=*/nullptr,
      /*acceptor=*/nullptr, args_,
      listener_state_->memory_quota()->CreateMemoryOwner(), nullptr);
  EXPECT_EQ(
      listener_state_->connection_quota()->TestOnlyActiveIncomingConnections(),
      1);
  connection->RefAsSubclass<NewChttp2ServerListener::ActiveConnection>()
      .release();  // Ref for OnClose
  // On close, the connection count should go back to 0.
  ActiveConnectionTestPeer(connection.get()).OnClose();
  EXPECT_EQ(
      listener_state_->connection_quota()->TestOnlyActiveIncomingConnections(),
      0);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_core::ForceEnableExperiment("work_serializer_dispatch", true);
  grpc_core::ForceEnableExperiment("server_listener", true);
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
