// Copyright 2026 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/generic_stub_session.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/core/transport/chttp2/tunnel.grpc.pb.h"
#include "test/core/transport/chttp2/tunnel.pb.h"
#include "gtest/gtest.h"
#include "absl/synchronization/notification.h"

namespace grpc {
namespace testing {
namespace {

class TestTunnelService : public grpc::testing::TunnelService::Service {
 public:
  Status Connect(ServerContext* /*context*/,
                 ServerReaderWriter<TunnelMsg, TunnelMsg>* stream) override {
    {
      std::unique_lock<std::mutex> lock(mu_);
      connected_ = true;
      cv_.notify_all();
    }

    // Acknowledge the connection (send initial metadata)
    stream->SendInitialMetadata();

    TunnelMsg msg;
    // Read until the client closes the stream
    while (stream->Read(&msg)) {
      std::cout << "Received message: " << msg.data() << std::endl;
      stream->Write(msg);
    }

    // Client closed the stream.
    return Status::OK;
  }

  bool WaitForConnection() {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, std::chrono::seconds(5),
                        [this] { return connected_; });
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  bool connected_ = false;
};

class Http2TunnelTest : public ::testing::Test {
 protected:
  Http2TunnelTest() {
    int port = grpc_pick_unused_port_or_die();
    server_address_ = "localhost:" + std::to_string(port);
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, InsecureServerCredentials());
    builder.RegisterService(&tunnel_service_);
    server_ = builder.BuildAndStart();
  }

  ~Http2TunnelTest() override {
    if (server_) server_->Shutdown();
  }

  std::string server_address_;
  TestTunnelService tunnel_service_;
  std::unique_ptr<Server> server_;
};

class MySessionReactor : public grpc::ClientSessionReactor {
 public:
  void OnSessionReady(std::shared_ptr<grpc::Channel> channel) override {
    channel_ = std::move(channel);
    channel_ready_.Notify();
  }

  void OnSessionAcknowledged(bool ok) override {
    EXPECT_TRUE(ok);
    ready_.Notify();
  }

  void OnDone(const grpc::Status& s) override {
    status_ = s;
    done_.Notify();
  }

  void WaitForReady() {
    ready_.WaitForNotification();
    channel_ready_.WaitForNotification();
  }
  void WaitForDone() { done_.WaitForNotification(); }
  std::shared_ptr<grpc::Channel> channel() { return channel_; }

 private:
  absl::Notification ready_;
  absl::Notification channel_ready_;
  absl::Notification done_;
  std::shared_ptr<grpc::Channel> channel_;
  grpc::Status status_;
};

TEST_F(Http2TunnelTest, CallbackSessionCallTest) {
  ChannelArguments args;
  auto channel = grpc::CreateCustomChannel(server_address_,
                                           InsecureChannelCredentials(), args);

  EXPECT_TRUE(channel->WaitForConnected(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_seconds(5, GPR_TIMESPAN))));

  ClientContext context;
  TunnelMsg request;
  request.set_data("outer request");

  MySessionReactor reactor;
  grpc::internal::GenericStubSession<TunnelMsg, TunnelMsg> session_stub(
      channel);
  session_stub.PrepareSessionCall(
      &context, "/grpc.testing.TunnelService/Connect", {}, &request, &reactor,
      [](grpc::Status /*s*/) {});

  reactor.StartCall();

  reactor.WaitForReady();
  EXPECT_TRUE(tunnel_service_.WaitForConnection());

  auto session_channel = reactor.channel();
  EXPECT_NE(session_channel, nullptr);

  {
    auto vstub =
        std::make_unique<grpc::TemplatedGenericStub<TunnelMsg, TunnelMsg>>(
            session_channel);
    absl::Notification vdone;
    ClientContext vcontext;
    TunnelMsg vrequest, vresponse;
    vrequest.set_data("inner request");
    vstub->UnaryCall(&vcontext, "/grpc.testing.TunnelService/Connect", {},
                     &vrequest, &vresponse,
                     [&vdone](grpc::Status s) { vdone.Notify(); });
    EXPECT_TRUE(vdone.WaitForNotificationWithTimeout(absl::Seconds(15)));
  }

  context.TryCancel();
  reactor.WaitForDone();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
