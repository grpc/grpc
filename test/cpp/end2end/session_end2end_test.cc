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
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/generic_stub_session.h>
#include <grpcpp/impl/sync.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/virtual_channel.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "src/core/call/call_filters.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/strings/match.h"
#include "absl/synchronization/notification.h"

namespace grpc {
namespace testing {
namespace {

const absl::Duration kTestTimeout = absl::Seconds(20);
const gpr_timespec kShortTimeout = gpr_time_from_seconds(5, GPR_TIMESPAN);

class CustomContext {
 public:
  explicit CustomContext(std::string message) : message_(std::move(message)) {}
  const std::string& message() const { return message_; }

 private:
  std::string message_;
};

class InnerEchoService : public grpc::testing::EchoTestService::Service {
 public:
  InnerEchoService() { grpc::experimental::SetVirtualService(this); }

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    auto session_context =
        grpc::experimental::GetSessionContext<CustomContext>(context);
    if (session_context) {
      response->set_message(request->message() + " " +
                            session_context->message());
    } else {
      response->set_message(request->message());
    }
    return Status::OK;
  }

  Status BidiStream(
      ServerContext* /*context*/,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    EchoRequest request;
    while (stream->Read(&request)) {
      EchoResponse response;
      response.set_message(request.message());
      stream->Write(response);
    }
    return Status::OK;
  }
};

class SimpleSessionReactor : public grpc::experimental::ServerSessionReactor {
 public:
  SimpleSessionReactor() { StartVirtualRPCs(); }

  void OnSendInitialMetadataDone(bool /*ok*/) override {}
  void OnCancel() override {
    bool do_finish = false;
    {
      grpc::internal::MutexLock l(&mu_);
      if (!finished_) {
        finished_ = true;
        do_finish = true;
      }
    }
    if (do_finish) {
      Finish(grpc::Status::CANCELLED);
    }
  }
  void OnDone() override { delete this; }

  void Close() {
    bool do_finish = false;
    {
      grpc::internal::MutexLock l(&mu_);
      if (!finished_) {
        finished_ = true;
        do_finish = true;
      }
    }
    if (do_finish) {
      Finish(grpc::Status::OK);
    }
  }

  void TriggerGracefulShutdown() {
    InitiateGracefulShutdown([this](absl::Status status) {
      bool do_finish = false;
      grpc::Status finish_status = grpc::Status::OK;
      {
        grpc::internal::MutexLock l(&mu_);
        if (!finished_) {
          finished_ = true;
          do_finish = true;
          bool is_clean =
              status.ok() ||
              absl::StrContains(status.message(),
                                "Last stream closed after sending GOAWAY");
          finish_status = is_clean ? grpc::Status::OK : grpc::Status::CANCELLED;
        }
      }
      if (do_finish) {
        Finish(finish_status);
      }
    });
  }

  grpc::internal::Mutex mu_;
  bool finished_ ABSL_GUARDED_BY(mu_) = false;
};

class NoStartSessionReactor : public grpc::experimental::ServerSessionReactor {
 public:
  NoStartSessionReactor() { Close(); }
  void OnDone() override { delete this; }
  void Close() { Finish(grpc::Status::CANCELLED); }
};

class TestSessionReactor : public grpc::experimental::ClientSessionReactor {
 public:
  TestSessionReactor(std::function<void(grpc::internal::Call)> on_ready,
                     std::function<void(const grpc::Status&)> on_done,
                     std::function<void(bool)> on_acknowledged = nullptr)
      : on_ready_(std::move(on_ready)),
        on_done_(std::move(on_done)),
        on_acknowledged_(std::move(on_acknowledged)) {}

  void OnSessionReady(grpc::internal::Call call) override {
    if (on_ready_) on_ready_(call);
  }

  void OnSessionAcknowledged(bool ok) override {
    if (on_acknowledged_) on_acknowledged_(ok);
  }

  void OnDone(const grpc::Status& s) override {
    if (on_done_) on_done_(s);
    delete this;
  }

 private:
  std::function<void(grpc::internal::Call)> on_ready_;
  std::function<void(const grpc::Status&)> on_done_;
  std::function<void(bool)> on_acknowledged_;
};

class TestBidiReactor
    : public grpc::ClientBidiReactor<EchoRequest, EchoResponse> {
 public:
  explicit TestBidiReactor(int count, absl::Notification* done)
      : count_(count), done_(done) {
    request_.set_message("hello");
  }

  void Start() {
    StartRead(&response_);
    StartWrite(&request_);
  }

  void OnWriteDone(bool ok) override {
    EXPECT_TRUE(ok);
    writes_complete_++;
    StartWritesDone();
  }

  void OnReadDone(bool ok) override {
    EXPECT_TRUE(ok);
    EXPECT_EQ(response_.message(), "hello");
    reads_complete_++;
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
  EchoRequest request_;
  EchoResponse response_;
  int writes_complete_ = 0;
  int reads_complete_ = 0;
};

class OuterEchoService : public grpc::Service {
 public:
  explicit OuterEchoService(grpc::Service* inner_service)
      : inner_service_(inner_service) {
    auto* method = new grpc::internal::RpcServiceMethod(
        "/grpc.testing.EchoTestService/SessionRequest",
        grpc::internal::RpcMethod::SESSION_RPC,
        new grpc::experimental::internal::CallbackSessionHandler<
            grpc::testing::EchoRequest>(
            [this](grpc::CallbackServerContext* context,
                   const grpc::testing::EchoRequest* request)
                -> grpc::experimental::ServerSessionReactor* {
              if (session_context_setter_) {
                session_context_setter_(context);
              }
              if (on_session_reactor_) {
                return on_session_reactor_(request);
              }
              return new SimpleSessionReactor();
            },
            inner_service_));
    method->SetServerApiType(
        grpc::internal::RpcServiceMethod::ApiType::CALL_BACK);
    AddMethod(method);
  }

  void SetSessionContextSetter(
      std::function<void(grpc::CallbackServerContext*)> setter) {
    session_context_setter_ = std::move(setter);
  }

  void SetSessionReactorFactory(
      std::function<grpc::experimental::ServerSessionReactor*(
          const grpc::testing::EchoRequest*)>
          factory) {
    on_session_reactor_ = std::move(factory);
  }

 private:
  grpc::Service* inner_service_;
  std::function<void(grpc::CallbackServerContext*)> session_context_setter_;
  std::function<grpc::experimental::ServerSessionReactor*(
      const grpc::testing::EchoRequest*)>
      on_session_reactor_;
};

class SessionEnd2endTest : public ::testing::Test {
 protected:
  SessionEnd2endTest() = default;

  void SetUp() override {
    if (grpc_core::IsPh2ClientEnabled() || grpc_core::IsPh2ServerEnabled() ||
        grpc_core::IsPh2ClientServerEnabled()) {
      GTEST_SKIP() << "Skipped for Promise-based HTTP/2 Transport";
    }

    ServerBuilder inner_builder;
    inner_builder.RegisterService(&inner_service_);
    custom_inner_builder_setup_(&inner_builder);
    inner_server_ = inner_builder.BuildAndStart();

    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             InsecureServerCredentials());
    outer_service_ = std::make_unique<OuterEchoService>(&inner_service_);
    builder.RegisterService(outer_service_.get());
    server_ = builder.BuildAndStart();
  }

  ~SessionEnd2endTest() override {
    if (server_) server_->Shutdown();
    if (inner_server_) inner_server_->Shutdown();
  }

  void SetupVirtualChannel(
      std::function<void(const grpc::Status&)> custom_on_done = nullptr) {
    ChannelArguments args;
    channel_ = grpc::CreateCustomChannel(server_address_.str(),
                                         InsecureChannelCredentials(), args);

    EXPECT_TRUE(channel_->WaitForConnected(
        gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), kShortTimeout)));

    session_stub_ = std::make_unique<
        grpc::experimental::GenericStubSession<EchoRequest, EchoResponse>>(
        channel_);

    request_.set_message("Session request");

    auto* session_reactor = new TestSessionReactor(
        [this](grpc::internal::Call call) {
          session_channel_ = grpc::experimental::CreateVirtualChannel(call);
          session_ready_.Notify();
        },
        [this, custom_on_done](const grpc::Status& s) {
          if (custom_on_done) {
            custom_on_done(s);
          } else {
            EXPECT_TRUE(s.ok()) << s.error_message();
          }
          done_.Notify();
        },
        [this](bool ok) {
          session_acknowledged_ok_ = ok;
          session_acknowledged_.Notify();
        });

    session_stub_->PrepareSessionCall(
        &context_, "/grpc.testing.EchoTestService/SessionRequest", {},
        &request_, session_reactor);
    session_reactor->StartCall();

    session_ready_.WaitForNotification();
    session_acknowledged_.WaitForNotification();
  }

  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<
      grpc::experimental::GenericStubSession<EchoRequest, EchoResponse>>
      session_stub_;
  ClientContext context_;
  EchoRequest request_;
  absl::Notification session_ready_;
  absl::Notification session_acknowledged_;
  bool session_acknowledged_ok_ = false;
  absl::Notification done_;
  std::shared_ptr<grpc::Channel> session_channel_;

  std::ostringstream server_address_;
  InnerEchoService inner_service_;
  std::unique_ptr<OuterEchoService> outer_service_;
  std::unique_ptr<Server> inner_server_;
  std::unique_ptr<Server> server_;
  std::function<void(ServerBuilder*)> custom_inner_builder_setup_ =
      [](ServerBuilder*) {};
};

bool g_fail_virtual_channel_setup = false;

class FailFilter : public grpc_core::ImplementChannelFilter<FailFilter> {
 public:
  static const grpc_channel_filter kFilter;
  static absl::string_view TypeName() { return "fail_filter"; }
  static absl::StatusOr<std::unique_ptr<FailFilter>> Create(
      const grpc_core::ChannelArgs&, grpc_core::ChannelFilter::Args) {
    if (g_fail_virtual_channel_setup) {
      return absl::InternalError("Intentional failure for test");
    }
    return std::make_unique<FailFilter>();
  }

  class Call {
   public:
    static const grpc_core::NoInterceptor OnClientInitialMetadata;
    static const grpc_core::NoInterceptor OnServerInitialMetadata;
    static const grpc_core::NoInterceptor OnServerTrailingMetadata;
    static const grpc_core::NoInterceptor OnClientToServerMessage;
    static const grpc_core::NoInterceptor OnClientToServerHalfClose;
    static const grpc_core::NoInterceptor OnServerToClientMessage;
    static const grpc_core::NoInterceptor OnFinalize;
    grpc_core::channelz::PropertyList ChannelzProperties() {
      return grpc_core::channelz::PropertyList();
    }
  };
};

const grpc_core::NoInterceptor FailFilter::Call::OnClientInitialMetadata;
const grpc_core::NoInterceptor FailFilter::Call::OnServerInitialMetadata;
const grpc_core::NoInterceptor FailFilter::Call::OnServerTrailingMetadata;
const grpc_core::NoInterceptor FailFilter::Call::OnClientToServerMessage;
const grpc_core::NoInterceptor FailFilter::Call::OnClientToServerHalfClose;
const grpc_core::NoInterceptor FailFilter::Call::OnServerToClientMessage;
const grpc_core::NoInterceptor FailFilter::Call::OnFinalize;

const grpc_channel_filter FailFilter::kFilter =
    grpc_core::MakePromiseBasedFilter<FailFilter,
                                      grpc_core::FilterEndpoint::kServer>();

TEST_F(SessionEnd2endTest, UnaryRpcOverVirtualChannel) {
  absl::Notification server_reactor_created;
  SimpleSessionReactor* server_reactor = nullptr;
  outer_service_->SetSessionReactorFactory(
      [&](const grpc::testing::EchoRequest* /*request*/) {
        server_reactor = new SimpleSessionReactor();
        server_reactor_created.Notify();
        return server_reactor;
      });

  SetupVirtualChannel();
  server_reactor_created.WaitForNotification();

  auto vstub =
      std::make_unique<grpc::TemplatedGenericStub<EchoRequest, EchoResponse>>(
          session_channel_);
  absl::Notification vdone;
  ClientContext vcontext;
  EchoRequest vrequest;
  EchoResponse vresponse;
  vrequest.set_message("hello");
  vstub->UnaryCall(&vcontext, "/grpc.testing.EchoTestService/Echo", {},
                   &vrequest, &vresponse, [&vdone, &vresponse](grpc::Status s) {
                     EXPECT_TRUE(s.ok()) << s.error_message();
                     EXPECT_EQ("hello", vresponse.message());
                     vdone.Notify();
                   });
  vdone.WaitForNotificationWithTimeout(kTestTimeout);

  server_reactor->Close();
  done_.WaitForNotificationWithTimeout(kTestTimeout);
}

TEST_F(SessionEnd2endTest, StreamingRpcOverVirtualChannel) {
  absl::Notification server_reactor_created;
  SimpleSessionReactor* server_reactor = nullptr;
  outer_service_->SetSessionReactorFactory(
      [&](const grpc::testing::EchoRequest* /*request*/) {
        server_reactor = new SimpleSessionReactor();
        server_reactor_created.Notify();
        return server_reactor;
      });

  SetupVirtualChannel();
  server_reactor_created.WaitForNotification();

  auto vstub =
      std::make_unique<grpc::TemplatedGenericStub<EchoRequest, EchoResponse>>(
          session_channel_);
  absl::Notification vdone;
  ClientContext vcontext;
  vcontext.set_deadline(
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), kShortTimeout));

  auto* reactor = new TestBidiReactor(1, &vdone);
  vstub->PrepareBidiStreamingCall(
      &vcontext, "/grpc.testing.EchoTestService/BidiStream", {}, reactor);
  reactor->StartCall();
  reactor->Start();

  vdone.WaitForNotificationWithTimeout(kTestTimeout);

  server_reactor->Close();

  done_.WaitForNotificationWithTimeout(kTestTimeout);
}

class GracefulShutdownEchoStreamReactor
    : public grpc::ClientBidiReactor<EchoRequest, EchoResponse> {
 public:
  explicit GracefulShutdownEchoStreamReactor(absl::Notification* stream_ready,
                                             absl::Notification* done)
      : stream_ready_(stream_ready), done_(done) {
    request_.set_message("request");
  }

  void Start() {
    StartRead(&response_);
    StartWrite(&request_);
  }

  void OnWriteDone(bool ok) override { EXPECT_TRUE(ok); }

  void OnReadDone(bool ok) override {
    EXPECT_TRUE(ok);
    EXPECT_EQ(response_.message(), "request");
    stream_ready_->Notify();
  }

  void OnDone(const grpc::Status& s) override {
    EXPECT_TRUE(s.ok()) << s.error_message();
    done_->Notify();
    delete this;
  }

  void FinishWrites() { StartWritesDone(); }

 private:
  absl::Notification* stream_ready_;
  absl::Notification* done_;
  EchoRequest request_;
  EchoResponse response_;
};

TEST_F(SessionEnd2endTest, SessionGracefulShutdown) {
  absl::Notification server_reactor_created;
  SimpleSessionReactor* server_reactor = nullptr;
  outer_service_->SetSessionReactorFactory(
      [&](const grpc::testing::EchoRequest* /*request*/) {
        server_reactor = new SimpleSessionReactor();
        server_reactor_created.Notify();
        return server_reactor;
      });

  SetupVirtualChannel();
  server_reactor_created.WaitForNotification();

  auto vstub =
      std::make_unique<grpc::TemplatedGenericStub<EchoRequest, EchoResponse>>(
          session_channel_);
  absl::Notification vready;
  absl::Notification vdone;
  ClientContext vcontext;
  auto* reactor = new GracefulShutdownEchoStreamReactor(&vready, &vdone);
  vstub->PrepareBidiStreamingCall(
      &vcontext, "/grpc.testing.EchoTestService/BidiStream", {}, reactor);
  reactor->StartCall();
  reactor->Start();

  vready.WaitForNotificationWithTimeout(kTestTimeout);

  server_reactor->TriggerGracefulShutdown();

  reactor->FinishWrites();

  vdone.WaitForNotificationWithTimeout(kTestTimeout);

  done_.WaitForNotification();

  // Test that virtual RPCs started afterwards fail immediately.
  ClientContext vcontext_after;
  EchoRequest vrequest_after;
  EchoResponse vresponse_after;
  vrequest_after.set_message("hello");
  auto vstub_after =
      std::make_unique<grpc::TemplatedGenericStub<EchoRequest, EchoResponse>>(
          session_channel_);
  absl::Notification vdone_after;
  vstub_after->UnaryCall(&vcontext_after, "/grpc.testing.EchoTestService/Echo",
                         {}, &vrequest_after, &vresponse_after,
                         [&vdone_after](grpc::Status s) {
                           EXPECT_FALSE(s.ok());
                           EXPECT_EQ(StatusCode::UNAVAILABLE, s.error_code());
                           vdone_after.Notify();
                         });
  vdone_after.WaitForNotificationWithTimeout(kTestTimeout);
}

TEST_F(SessionEnd2endTest, SessionContextPropagation) {
  outer_service_->SetSessionContextSetter(
      [](grpc::CallbackServerContext* context) {
        grpc::experimental::SetSessionContext(
            context, std::make_shared<CustomContext>("from_session"));
      });

  absl::Notification server_reactor_created;
  SimpleSessionReactor* server_reactor = nullptr;
  outer_service_->SetSessionReactorFactory(
      [&](const grpc::testing::EchoRequest* /*request*/) {
        server_reactor = new SimpleSessionReactor();
        server_reactor_created.Notify();
        return server_reactor;
      });

  SetupVirtualChannel();
  server_reactor_created.WaitForNotification();

  auto vstub =
      std::make_unique<grpc::TemplatedGenericStub<EchoRequest, EchoResponse>>(
          session_channel_);
  absl::Notification vdone;
  ClientContext vcontext;
  EchoRequest vrequest;
  EchoResponse vresponse;
  vrequest.set_message("hello");
  vstub->UnaryCall(&vcontext, "/grpc.testing.EchoTestService/Echo", {},
                   &vrequest, &vresponse, [&vdone, &vresponse](grpc::Status s) {
                     EXPECT_TRUE(s.ok()) << s.error_message();
                     EXPECT_EQ("hello from_session", vresponse.message());
                     vdone.Notify();
                   });
  vdone.WaitForNotificationWithTimeout(kTestTimeout);

  server_reactor->Close();
  done_.WaitForNotificationWithTimeout(kTestTimeout);
}

class CanceledSessionEchoStreamReactor
    : public grpc::ClientBidiReactor<EchoRequest, EchoResponse> {
 public:
  explicit CanceledSessionEchoStreamReactor(absl::Notification* stream_ready,
                                            absl::Notification* done)
      : stream_ready_(stream_ready), done_(done) {
    request_.set_message("request");
  }

  void Start() {
    StartRead(&response_);
    StartWrite(&request_);
  }

  void OnWriteDone(bool /*ok*/) override {}

  void OnReadDone(bool ok) override {
    if (ok) {
      EXPECT_EQ(response_.message(), "request");
      stream_ready_->Notify();
    }
  }

  void OnDone(const grpc::Status& s) override {
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(StatusCode::UNAVAILABLE, s.error_code());
    done_->Notify();
    delete this;
  }

 private:
  absl::Notification* stream_ready_;
  absl::Notification* done_;
  EchoRequest request_;
  EchoResponse response_;
};

TEST_F(SessionEnd2endTest, ClientCancelsSessionWithOngoingVirtualRpc) {
  absl::Notification server_reactor_created;
  SimpleSessionReactor* server_reactor = nullptr;
  outer_service_->SetSessionReactorFactory(
      [&](const grpc::testing::EchoRequest* /*request*/) {
        server_reactor = new SimpleSessionReactor();
        server_reactor_created.Notify();
        return server_reactor;
      });

  SetupVirtualChannel([](const grpc::Status& s) {
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(StatusCode::CANCELLED, s.error_code());
  });
  server_reactor_created.WaitForNotification();

  auto vstub =
      std::make_unique<grpc::TemplatedGenericStub<EchoRequest, EchoResponse>>(
          session_channel_);
  absl::Notification vready;
  absl::Notification vdone;
  ClientContext vcontext;
  auto* reactor = new CanceledSessionEchoStreamReactor(&vready, &vdone);
  vstub->PrepareBidiStreamingCall(
      &vcontext, "/grpc.testing.EchoTestService/BidiStream", {}, reactor);
  reactor->StartCall();
  reactor->Start();

  vready.WaitForNotificationWithTimeout(kTestTimeout);

  // Cancel the session from the client side.
  context_.TryCancel();

  vdone.WaitForNotificationWithTimeout(kTestTimeout);
  done_.WaitForNotificationWithTimeout(kTestTimeout);
}

TEST_F(SessionEnd2endTest, SetupTransportFails) {
  g_fail_virtual_channel_setup = true;

  absl::Notification server_reactor_created;
  SimpleSessionReactor* server_reactor = nullptr;
  outer_service_->SetSessionReactorFactory(
      [&](const grpc::testing::EchoRequest* /*request*/) {
        server_reactor = new SimpleSessionReactor();
        server_reactor_created.Notify();
        return server_reactor;
      });

  SetupVirtualChannel([](const grpc::Status& s) {
    EXPECT_FALSE(s.ok());
    EXPECT_TRUE(s.error_code() == StatusCode::INTERNAL ||
                s.error_code() == StatusCode::CANCELLED)
        << "Unexpected error code: " << s.error_code() << ": "
        << s.error_message();
  });
  server_reactor_created.WaitForNotification();

  auto vstub =
      std::make_unique<grpc::TemplatedGenericStub<EchoRequest, EchoResponse>>(
          session_channel_);
  absl::Notification vdone;
  ClientContext vcontext;
  EchoRequest vrequest;
  EchoResponse vresponse;
  vrequest.set_message("hello");
  vstub->UnaryCall(&vcontext, "/grpc.testing.EchoTestService/Echo", {},
                   &vrequest, &vresponse, [&vdone](grpc::Status s) {
                     EXPECT_FALSE(s.ok());
                     // TODO(snohria): See if we should enforce a specific error
                     // message here.
                     EXPECT_EQ(s.error_code(), StatusCode::UNAVAILABLE);
                     vdone.Notify();
                   });
  EXPECT_TRUE(vdone.WaitForNotificationWithTimeout(kTestTimeout));
  EXPECT_TRUE(done_.WaitForNotificationWithTimeout(kTestTimeout));
  g_fail_virtual_channel_setup = false;
}

TEST_F(SessionEnd2endTest, ServerFinishesWithoutStartingVirtualRpcs) {
  absl::Notification server_reactor_created;
  NoStartSessionReactor* server_reactor = nullptr;
  outer_service_->SetSessionReactorFactory(
      [&](const grpc::testing::EchoRequest* /*request*/) {
        server_reactor = new NoStartSessionReactor();
        server_reactor_created.Notify();
        return server_reactor;
      });

  SetupVirtualChannel([](const grpc::Status& s) {
    EXPECT_FALSE(s.ok());
    EXPECT_TRUE(s.error_code() == StatusCode::CANCELLED)
        << "Unexpected error code: " << s.error_code() << ": "
        << s.error_message();
  });

  server_reactor_created.WaitForNotification();

  done_.WaitForNotificationWithTimeout(kTestTimeout);

  EXPECT_FALSE(session_acknowledged_ok_);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_core::CoreConfiguration::RegisterEphemeralBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->channel_init()->RegisterFilter(
            GRPC_SERVER_VIRTUAL_CHANNEL, &grpc::testing::FailFilter::kFilter);
      });
  return RUN_ALL_TESTS();
}
