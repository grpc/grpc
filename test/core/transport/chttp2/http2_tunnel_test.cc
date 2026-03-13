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
#include <grpcpp/alarm.h>
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

#include "grpcpp/impl/server_callback_handlers.h"
#include "grpcpp/support/server_callback.h"
#include "src/core/lib/channel/channel_args.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/core/transport/chttp2/tunnel.grpc.pb.h"
#include "test/core/transport/chttp2/tunnel.pb.h"
#include "gtest/gtest.h"
#include "absl/synchronization/notification.h"

namespace grpc {
namespace testing {
namespace {

class InnerTunnelService : public grpc::testing::TunnelService::Service {
 public:
  Status Exec(ServerContext* /*context*/, const TunnelMsg* request,
              TunnelMsg* response) override {
    response->set_data(request->data());
    return Status::OK;
  }

  Status ExecStream(ServerContext* /*context*/,
                    ServerReaderWriter<TunnelMsg, TunnelMsg>* stream) override {
    TunnelMsg request;
    while (stream->Read(&request)) {
      stream->Write(request);
    }
    return Status::OK;
  }
};

class TunnelSessionReactor : public grpc::ServerSessionReactor {
 public:
  TunnelSessionReactor() {
    StartSendInitialMetadata();
    // Keep the session open for 10 second to allow Exec calls to succeed.
    alarm_.Set(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                            gpr_time_from_seconds(10, GPR_TIMESPAN)),
               [this](bool ok) {
                 if (ok) {
                   Finish(Status::OK);
                 }
               });
  }

  void OnSendInitialMetadataDone(bool ok) override {}

  void OnCancel() override {}

  void OnDone() override { delete this; }

 private:
  grpc::Alarm alarm_;
};

class TestSessionReactor : public grpc::ClientSessionReactor {
 public:
  explicit TestSessionReactor(
      std::function<void(std::shared_ptr<grpc::Channel>)> on_ready)
      : on_ready_(std::move(on_ready)) {}

  void OnSessionReady(std::shared_ptr<grpc::Channel> channel) override {
    if (on_ready_) {
      on_ready_(std::move(channel));
    }
  }

  void OnDone(const grpc::Status& /*s*/) override { delete this; }

 private:
  std::function<void(std::shared_ptr<grpc::Channel>)> on_ready_;
};

class OuterTunnelService : public grpc::testing::TunnelService::Service {
 public:
  explicit OuterTunnelService(grpc::Server* inner_server)
      : inner_server_(inner_server) {
    auto* method = new grpc::internal::RpcServiceMethod(
        "/grpc.testing.TunnelService/Connect",
        grpc::internal::RpcMethod::SESSION_RPC,
        new grpc::internal::CallbackSessionHandler<grpc::testing::TunnelMsg>(
            [](grpc::CallbackServerContext* /*context*/,
               const grpc::testing::TunnelMsg* /*request*/) {
              return new TunnelSessionReactor();
            },
            inner_server_));
    method->SetServerApiType(
        grpc::internal::RpcServiceMethod::ApiType::CALL_BACK);
    AddMethod(method);
  }

 private:
  grpc::Server* inner_server_;
};

class Http2TunnelTest : public ::testing::Test {
 protected:
  Http2TunnelTest() {
    ServerBuilder inner_builder;
    inner_builder.RegisterService(&inner_service_);
    inner_server_ = inner_builder.BuildAndStart();

    int port = grpc_pick_unused_port_or_die();
    server_address_ = "localhost:" + std::to_string(port);
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, InsecureServerCredentials());
    outer_service_ = std::make_unique<OuterTunnelService>(inner_server_.get());
    builder.RegisterService(outer_service_.get());
    server_ = builder.BuildAndStart();
  }

  ~Http2TunnelTest() override {
    if (server_) server_->Shutdown();
    if (inner_server_) inner_server_->Shutdown();
  }

  std::string server_address_;
  InnerTunnelService inner_service_;
  std::unique_ptr<OuterTunnelService> outer_service_;
  std::unique_ptr<Server> inner_server_;
  std::unique_ptr<Server> server_;
};

class ExecStreamReactor : public grpc::ClientBidiReactor<TunnelMsg, TunnelMsg> {
 public:
  explicit ExecStreamReactor(int count, absl::Notification* done)
      : count_(count), done_(done) {
    request_.set_data("request");
  }

  void Start() {
    StartRead(&response_);
    StartWrite(&request_);
  }

  void OnWriteDone(bool ok) override {
    if (!ok) return;
    writes_complete_++;
    if (writes_complete_ < count_) {
      StartWrite(&request_);
    } else {
      StartWritesDone();
    }
  }

  void OnReadDone(bool ok) override {
    if (!ok) return;
    EXPECT_EQ(response_.data(), "request");
    reads_complete_++;
    if (reads_complete_ < count_) {
      StartRead(&response_);
    }
  }

  void OnDone(const grpc::Status& s) override {
    EXPECT_TRUE(s.ok()) << s.error_message();
    EXPECT_EQ(reads_complete_, count_);
    EXPECT_EQ(writes_complete_, count_);
    done_->Notify();
    delete this;
  }

 private:
  int count_;
  absl::Notification* done_;
  TunnelMsg request_;
  TunnelMsg response_;
  int writes_complete_ = 0;
  int reads_complete_ = 0;
};

TEST_F(Http2TunnelTest, ExecStream) {
  ChannelArguments args;
  auto channel = grpc::CreateCustomChannel(server_address_,
                                           InsecureChannelCredentials(), args);

  // Trigger connection establishment
  EXPECT_TRUE(channel->WaitForConnected(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_seconds(5, GPR_TIMESPAN))));

  // Start the tunnel stream

  absl::Notification done;
  ClientContext context;
  TunnelMsg request;
  request.set_data("outer request");
  {
    grpc::internal::GenericStubSession<TunnelMsg, Empty> session_stub(channel);
    std::shared_ptr<grpc::Channel> session_channel;
    absl::Notification session_ready;
    auto* session_reactor = new TestSessionReactor(
        [&session_channel, &session_ready](std::shared_ptr<grpc::Channel> c) {
          session_channel = std::move(c);
          session_ready.Notify();
        });

    session_stub.PrepareSessionCall(
        &context, "/grpc.testing.TunnelService/Connect", {}, &request,
        session_reactor, [&done](grpc::Status s) { done.Notify(); });
    session_reactor->StartCall();
    // Wait for the session channel to be ready.
    session_ready.WaitForNotification();

    auto vstub =
        std::make_unique<grpc::TemplatedGenericStub<TunnelMsg, TunnelMsg>>(
            session_channel);
    absl::Notification vdone;
    ClientContext vcontext;
    auto* reactor = new ExecStreamReactor(5, &vdone);
    vstub->PrepareBidiStreamingCall(
        &vcontext, "/grpc.testing.TunnelService/ExecStream", {}, reactor);
    reactor->StartCall();
    reactor->Start();
    vdone.WaitForNotificationWithTimeout(absl::Seconds(15));
  }

  done.WaitForNotification();
}

TEST_F(Http2TunnelTest, ControlStreamLifetime) {
  ChannelArguments args;
  auto channel = grpc::CreateCustomChannel(server_address_,
                                           InsecureChannelCredentials(), args);

  // Trigger connection establishment
  EXPECT_TRUE(channel->WaitForConnected(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_seconds(5, GPR_TIMESPAN))));

  // Start the tunnel stream

  absl::Notification done;
  ClientContext context;
  TunnelMsg request;
  request.set_data("outer request");
  {
    grpc::internal::GenericStubSession<TunnelMsg, Empty> session_stub(channel);
    std::shared_ptr<grpc::Channel> session_channel;
    absl::Notification session_ready;
    auto* session_reactor = new TestSessionReactor(
        [&session_channel, &session_ready](std::shared_ptr<grpc::Channel> c) {
          session_channel = std::move(c);
          session_ready.Notify();
        });

    session_stub.PrepareSessionCall(
        &context, "/grpc.testing.TunnelService/Connect", {}, &request,
        session_reactor, [&done](grpc::Status s) { done.Notify(); });
    session_reactor->StartCall();
    // Wait for the session channel to be ready.
    session_ready.WaitForNotification();

    auto vstub =
        std::make_unique<grpc::TemplatedGenericStub<TunnelMsg, TunnelMsg>>(
            session_channel);
    absl::Notification vdone;
    ClientContext vcontext;
    TunnelMsg vrequest, vresponse;
    vrequest.set_data("inner request");
    vstub->UnaryCall(&vcontext, "/grpc.testing.TunnelService/Exec", {},
                     &vrequest, &vresponse,
                     [&vdone, &vresponse](grpc::Status s) {
                       EXPECT_TRUE(s.ok()) << s.error_message();
                       EXPECT_EQ(vresponse.data(), "inner request");
                       vdone.Notify();
                     });
    vdone.WaitForNotificationWithTimeout(absl::Seconds(15));
  }

  done.WaitForNotification();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
