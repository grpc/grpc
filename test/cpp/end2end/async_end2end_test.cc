/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <cinttypes>
#include <memory>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/health_check_service_server_builder_option.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/tls.h"
#include "src/core/lib/iomgr/port.h"
#include "src/proto/grpc/health/v1/health.grpc.pb.h"
#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/string_ref_helper.h"
#include "test/cpp/util/test_credentials_provider.h"

#include <gtest/gtest.h>

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;
using grpc::testing::kTlsCredentialsType;
using std::chrono::system_clock;

namespace grpc {
namespace testing {

namespace {

void* tag(int i) { return (void*)static_cast<intptr_t>(i); }
int detag(void* p) { return static_cast<int>(reinterpret_cast<intptr_t>(p)); }

class Verifier {
 public:
  Verifier() : lambda_run_(false) {}
  // Expect sets the expected ok value for a specific tag
  Verifier& Expect(int i, bool expect_ok) {
    return ExpectUnless(i, expect_ok, false);
  }
  // ExpectUnless sets the expected ok value for a specific tag
  // unless the tag was already marked seen (as a result of ExpectMaybe)
  Verifier& ExpectUnless(int i, bool expect_ok, bool seen) {
    if (!seen) {
      expectations_[tag(i)] = expect_ok;
    }
    return *this;
  }
  // ExpectMaybe sets the expected ok value for a specific tag, but does not
  // require it to appear
  // If it does, sets *seen to true
  Verifier& ExpectMaybe(int i, bool expect_ok, bool* seen) {
    if (!*seen) {
      maybe_expectations_[tag(i)] = MaybeExpect{expect_ok, seen};
    }
    return *this;
  }

  // Next waits for 1 async tag to complete, checks its
  // expectations, and returns the tag
  int Next(CompletionQueue* cq, bool ignore_ok) {
    bool ok;
    void* got_tag;
    EXPECT_TRUE(cq->Next(&got_tag, &ok));
    GotTag(got_tag, ok, ignore_ok);
    return detag(got_tag);
  }

  template <typename T>
  CompletionQueue::NextStatus DoOnceThenAsyncNext(
      CompletionQueue* cq, void** got_tag, bool* ok, T deadline,
      std::function<void(void)> lambda) {
    if (lambda_run_) {
      return cq->AsyncNext(got_tag, ok, deadline);
    } else {
      lambda_run_ = true;
      return cq->DoThenAsyncNext(lambda, got_tag, ok, deadline);
    }
  }

  // Verify keeps calling Next until all currently set
  // expected tags are complete
  void Verify(CompletionQueue* cq) { Verify(cq, false); }

  // This version of Verify allows optionally ignoring the
  // outcome of the expectation
  void Verify(CompletionQueue* cq, bool ignore_ok) {
    GPR_ASSERT(!expectations_.empty() || !maybe_expectations_.empty());
    while (!expectations_.empty()) {
      Next(cq, ignore_ok);
    }
  }

  // This version of Verify stops after a certain deadline
  void Verify(CompletionQueue* cq,
              std::chrono::system_clock::time_point deadline) {
    if (expectations_.empty()) {
      bool ok;
      void* got_tag;
      EXPECT_EQ(cq->AsyncNext(&got_tag, &ok, deadline),
                CompletionQueue::TIMEOUT);
    } else {
      while (!expectations_.empty()) {
        bool ok;
        void* got_tag;
        EXPECT_EQ(cq->AsyncNext(&got_tag, &ok, deadline),
                  CompletionQueue::GOT_EVENT);
        GotTag(got_tag, ok, false);
      }
    }
  }

  // This version of Verify stops after a certain deadline, and uses the
  // DoThenAsyncNext API
  // to call the lambda
  void Verify(CompletionQueue* cq,
              std::chrono::system_clock::time_point deadline,
              std::function<void(void)> lambda) {
    if (expectations_.empty()) {
      bool ok;
      void* got_tag;
      EXPECT_EQ(DoOnceThenAsyncNext(cq, &got_tag, &ok, deadline, lambda),
                CompletionQueue::TIMEOUT);
    } else {
      while (!expectations_.empty()) {
        bool ok;
        void* got_tag;
        EXPECT_EQ(DoOnceThenAsyncNext(cq, &got_tag, &ok, deadline, lambda),
                  CompletionQueue::GOT_EVENT);
        GotTag(got_tag, ok, false);
      }
    }
  }

 private:
  void GotTag(void* got_tag, bool ok, bool ignore_ok) {
    auto it = expectations_.find(got_tag);
    if (it != expectations_.end()) {
      if (!ignore_ok) {
        EXPECT_EQ(it->second, ok);
      }
      expectations_.erase(it);
    } else {
      auto it2 = maybe_expectations_.find(got_tag);
      if (it2 != maybe_expectations_.end()) {
        if (it2->second.seen != nullptr) {
          EXPECT_FALSE(*it2->second.seen);
          *it2->second.seen = true;
        }
        if (!ignore_ok) {
          EXPECT_EQ(it2->second.ok, ok);
        }
      } else {
        gpr_log(GPR_ERROR, "Unexpected tag: %p", tag);
        abort();
      }
    }
  }

  struct MaybeExpect {
    bool ok;
    bool* seen;
  };

  std::map<void*, bool> expectations_;
  std::map<void*, MaybeExpect> maybe_expectations_;
  bool lambda_run_;
};

bool plugin_has_sync_methods(std::unique_ptr<ServerBuilderPlugin>& plugin) {
  return plugin->has_sync_methods();
}

// This class disables the server builder plugins that may add sync services to
// the server. If there are sync services, UnimplementedRpc test will triger
// the sync unknown rpc routine on the server side, rather than the async one
// that needs to be tested here.
class ServerBuilderSyncPluginDisabler : public ::grpc::ServerBuilderOption {
 public:
  void UpdateArguments(ChannelArguments* arg) override {}

  void UpdatePlugins(
      std::vector<std::unique_ptr<ServerBuilderPlugin>>* plugins) override {
    plugins->erase(std::remove_if(plugins->begin(), plugins->end(),
                                  plugin_has_sync_methods),
                   plugins->end());
  }
};

class TestScenario {
 public:
  TestScenario(bool inproc_stub, const grpc::string& creds_type, bool hcs,
               const grpc::string& content)
      : inproc(inproc_stub),
        health_check_service(hcs),
        credentials_type(creds_type),
        message_content(content) {}
  void Log() const;
  bool inproc;
  bool health_check_service;
  const grpc::string credentials_type;
  const grpc::string message_content;
};

static std::ostream& operator<<(std::ostream& out,
                                const TestScenario& scenario) {
  return out << "TestScenario{inproc=" << (scenario.inproc ? "true" : "false")
             << ", credentials='" << scenario.credentials_type
             << ", health_check_service="
             << (scenario.health_check_service ? "true" : "false")
             << "', message_size=" << scenario.message_content.size() << "}";
}

void TestScenario::Log() const {
  std::ostringstream out;
  out << *this;
  gpr_log(GPR_DEBUG, "%s", out.str().c_str());
}

class HealthCheck : public health::v1::Health::Service {};

class AsyncEnd2endTest : public ::testing::TestWithParam<TestScenario> {
 protected:
  AsyncEnd2endTest() { GetParam().Log(); }

  void SetUp() override {
    port_ = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port_;

    // Setup server
    BuildAndStartServer();
  }

  void TearDown() override {
    server_->Shutdown();
    void* ignored_tag;
    bool ignored_ok;
    cq_->Shutdown();
    while (cq_->Next(&ignored_tag, &ignored_ok))
      ;
    stub_.reset();
    grpc_recycle_unused_port(port_);
  }

  void BuildAndStartServer() {
    ServerBuilder builder;
    auto server_creds = GetCredentialsProvider()->GetServerCredentials(
        GetParam().credentials_type);
    builder.AddListeningPort(server_address_.str(), server_creds);
    service_.reset(new grpc::testing::EchoTestService::AsyncService());
    builder.RegisterService(service_.get());
    if (GetParam().health_check_service) {
      builder.RegisterService(&health_check_);
    }
    cq_ = builder.AddCompletionQueue();

    // TODO(zyc): make a test option to choose wheather sync plugins should be
    // deleted
    std::unique_ptr<ServerBuilderOption> sync_plugin_disabler(
        new ServerBuilderSyncPluginDisabler());
    builder.SetOption(move(sync_plugin_disabler));
    server_ = builder.BuildAndStart();
  }

  void ResetStub() {
    ChannelArguments args;
    auto channel_creds = GetCredentialsProvider()->GetChannelCredentials(
        GetParam().credentials_type, &args);
    std::shared_ptr<Channel> channel =
        !(GetParam().inproc)
            ? CreateCustomChannel(server_address_.str(), channel_creds, args)
            : server_->InProcessChannel(args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

  void SendRpc(int num_rpcs) {
    for (int i = 0; i < num_rpcs; i++) {
      EchoRequest send_request;
      EchoRequest recv_request;
      EchoResponse send_response;
      EchoResponse recv_response;
      Status recv_status;

      ClientContext cli_ctx;
      ServerContext srv_ctx;
      grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

      send_request.set_message(GetParam().message_content);
      std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
          stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));

      service_->RequestEcho(&srv_ctx, &recv_request, &response_writer,
                            cq_.get(), cq_.get(), tag(2));

      response_reader->Finish(&recv_response, &recv_status, tag(4));

      Verifier().Expect(2, true).Verify(cq_.get());
      EXPECT_EQ(send_request.message(), recv_request.message());

      send_response.set_message(recv_request.message());
      response_writer.Finish(send_response, Status::OK, tag(3));
      Verifier().Expect(3, true).Expect(4, true).Verify(cq_.get());

      EXPECT_EQ(send_response.message(), recv_response.message());
      EXPECT_TRUE(recv_status.ok());
    }
  }

  std::unique_ptr<ServerCompletionQueue> cq_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<grpc::testing::EchoTestService::AsyncService> service_;
  HealthCheck health_check_;
  std::ostringstream server_address_;
  int port_;
};

TEST_P(AsyncEnd2endTest, SimpleRpc) {
  ResetStub();
  SendRpc(1);
}

TEST_P(AsyncEnd2endTest, SequentialRpcs) {
  ResetStub();
  SendRpc(10);
}

TEST_P(AsyncEnd2endTest, ReconnectChannel) {
  // GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS is set to 100ms in main()
  if (GetParam().inproc) {
    return;
  }
  int poller_slowdown_factor = 1;
  // It needs 2 pollset_works to reconnect the channel with polling engine
  // "poll"
  char* s = gpr_getenv("GRPC_POLL_STRATEGY");
  if (s != nullptr && 0 == strcmp(s, "poll")) {
    poller_slowdown_factor = 2;
  }
  gpr_free(s);
  ResetStub();
  SendRpc(1);
  server_->Shutdown();
  void* ignored_tag;
  bool ignored_ok;
  cq_->Shutdown();
  while (cq_->Next(&ignored_tag, &ignored_ok))
    ;
  BuildAndStartServer();
  // It needs more than GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS time to
  // reconnect the channel.
  gpr_sleep_until(gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME),
      gpr_time_from_millis(
          300 * poller_slowdown_factor * grpc_test_slowdown_factor(),
          GPR_TIMESPAN)));
  SendRpc(1);
}

// We do not need to protect notify because the use is synchronized.
void ServerWait(Server* server, int* notify) {
  server->Wait();
  *notify = 1;
}
TEST_P(AsyncEnd2endTest, WaitAndShutdownTest) {
  int notify = 0;
  std::thread wait_thread(&ServerWait, server_.get(), &notify);
  ResetStub();
  SendRpc(1);
  EXPECT_EQ(0, notify);
  server_->Shutdown();
  wait_thread.join();
  EXPECT_EQ(1, notify);
}

TEST_P(AsyncEnd2endTest, ShutdownThenWait) {
  ResetStub();
  SendRpc(1);
  std::thread t([this]() { server_->Shutdown(); });
  server_->Wait();
  t.join();
}

// Test a simple RPC using the async version of Next
TEST_P(AsyncEnd2endTest, AsyncNextRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));

  std::chrono::system_clock::time_point time_now(
      std::chrono::system_clock::now());
  std::chrono::system_clock::time_point time_limit(
      std::chrono::system_clock::now() + std::chrono::seconds(10));
  Verifier().Verify(cq_.get(), time_now);
  Verifier().Verify(cq_.get(), time_now);

  service_->RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                        cq_.get(), tag(2));
  response_reader->Finish(&recv_response, &recv_status, tag(4));

  Verifier().Expect(2, true).Verify(cq_.get(), time_limit);
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  response_writer.Finish(send_response, Status::OK, tag(3));
  Verifier().Expect(3, true).Expect(4, true).Verify(
      cq_.get(), std::chrono::system_clock::time_point::max());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

// Test a simple RPC using the async version of Next
TEST_P(AsyncEnd2endTest, DoThenAsyncNextRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));

  std::chrono::system_clock::time_point time_now(
      std::chrono::system_clock::now());
  std::chrono::system_clock::time_point time_limit(
      std::chrono::system_clock::now() + std::chrono::seconds(10));
  Verifier().Verify(cq_.get(), time_now);
  Verifier().Verify(cq_.get(), time_now);

  auto resp_writer_ptr = &response_writer;
  auto lambda_2 = [&, this, resp_writer_ptr]() {
    service_->RequestEcho(&srv_ctx, &recv_request, resp_writer_ptr, cq_.get(),
                          cq_.get(), tag(2));
  };
  response_reader->Finish(&recv_response, &recv_status, tag(4));

  Verifier().Expect(2, true).Verify(cq_.get(), time_limit, lambda_2);
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  auto lambda_3 = [resp_writer_ptr, send_response]() {
    resp_writer_ptr->Finish(send_response, Status::OK, tag(3));
  };
  Verifier().Expect(3, true).Expect(4, true).Verify(
      cq_.get(), std::chrono::system_clock::time_point::max(), lambda_3);

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

// Two pings and a final pong.
TEST_P(AsyncEnd2endTest, SimpleClientStreaming) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncReader<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::unique_ptr<ClientAsyncWriter<EchoRequest>> cli_stream(
      stub_->AsyncRequestStream(&cli_ctx, &recv_response, cq_.get(), tag(1)));

  service_->RequestRequestStream(&srv_ctx, &srv_stream, cq_.get(), cq_.get(),
                                 tag(2));

  Verifier().Expect(2, true).Expect(1, true).Verify(cq_.get());

  cli_stream->Write(send_request, tag(3));
  srv_stream.Read(&recv_request, tag(4));
  Verifier().Expect(3, true).Expect(4, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  cli_stream->Write(send_request, tag(5));
  srv_stream.Read(&recv_request, tag(6));
  Verifier().Expect(5, true).Expect(6, true).Verify(cq_.get());

  EXPECT_EQ(send_request.message(), recv_request.message());
  cli_stream->WritesDone(tag(7));
  srv_stream.Read(&recv_request, tag(8));
  Verifier().Expect(7, true).Expect(8, false).Verify(cq_.get());

  send_response.set_message(recv_request.message());
  srv_stream.Finish(send_response, Status::OK, tag(9));
  cli_stream->Finish(&recv_status, tag(10));
  Verifier().Expect(9, true).Expect(10, true).Verify(cq_.get());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

// Two pings and a final pong.
TEST_P(AsyncEnd2endTest, SimpleClientStreamingWithCoalescingApi) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncReader<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  cli_ctx.set_initial_metadata_corked(true);
  // tag:1 never comes up since no op is performed
  std::unique_ptr<ClientAsyncWriter<EchoRequest>> cli_stream(
      stub_->AsyncRequestStream(&cli_ctx, &recv_response, cq_.get(), tag(1)));

  service_->RequestRequestStream(&srv_ctx, &srv_stream, cq_.get(), cq_.get(),
                                 tag(2));

  cli_stream->Write(send_request, tag(3));

  bool seen3 = false;

  Verifier().Expect(2, true).ExpectMaybe(3, true, &seen3).Verify(cq_.get());

  srv_stream.Read(&recv_request, tag(4));

  Verifier().ExpectUnless(3, true, seen3).Expect(4, true).Verify(cq_.get());

  EXPECT_EQ(send_request.message(), recv_request.message());

  cli_stream->WriteLast(send_request, WriteOptions(), tag(5));
  srv_stream.Read(&recv_request, tag(6));
  Verifier().Expect(5, true).Expect(6, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  srv_stream.Read(&recv_request, tag(7));
  Verifier().Expect(7, false).Verify(cq_.get());

  send_response.set_message(recv_request.message());
  srv_stream.Finish(send_response, Status::OK, tag(8));
  cli_stream->Finish(&recv_status, tag(9));
  Verifier().Expect(8, true).Expect(9, true).Verify(cq_.get());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

// One ping, two pongs.
TEST_P(AsyncEnd2endTest, SimpleServerStreaming) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncWriter<EchoResponse> srv_stream(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::unique_ptr<ClientAsyncReader<EchoResponse>> cli_stream(
      stub_->AsyncResponseStream(&cli_ctx, send_request, cq_.get(), tag(1)));

  service_->RequestResponseStream(&srv_ctx, &recv_request, &srv_stream,
                                  cq_.get(), cq_.get(), tag(2));

  Verifier().Expect(1, true).Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  srv_stream.Write(send_response, tag(3));
  cli_stream->Read(&recv_response, tag(4));
  Verifier().Expect(3, true).Expect(4, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  srv_stream.Write(send_response, tag(5));
  cli_stream->Read(&recv_response, tag(6));
  Verifier().Expect(5, true).Expect(6, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  srv_stream.Finish(Status::OK, tag(7));
  cli_stream->Read(&recv_response, tag(8));
  Verifier().Expect(7, true).Expect(8, false).Verify(cq_.get());

  cli_stream->Finish(&recv_status, tag(9));
  Verifier().Expect(9, true).Verify(cq_.get());

  EXPECT_TRUE(recv_status.ok());
}

// One ping, two pongs. Using WriteAndFinish API
TEST_P(AsyncEnd2endTest, SimpleServerStreamingWithCoalescingApiWAF) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncWriter<EchoResponse> srv_stream(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::unique_ptr<ClientAsyncReader<EchoResponse>> cli_stream(
      stub_->AsyncResponseStream(&cli_ctx, send_request, cq_.get(), tag(1)));

  service_->RequestResponseStream(&srv_ctx, &recv_request, &srv_stream,
                                  cq_.get(), cq_.get(), tag(2));

  Verifier().Expect(1, true).Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  srv_stream.Write(send_response, tag(3));
  cli_stream->Read(&recv_response, tag(4));
  Verifier().Expect(3, true).Expect(4, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  srv_stream.WriteAndFinish(send_response, WriteOptions(), Status::OK, tag(5));
  cli_stream->Read(&recv_response, tag(6));
  Verifier().Expect(5, true).Expect(6, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  cli_stream->Read(&recv_response, tag(7));
  Verifier().Expect(7, false).Verify(cq_.get());

  cli_stream->Finish(&recv_status, tag(8));
  Verifier().Expect(8, true).Verify(cq_.get());

  EXPECT_TRUE(recv_status.ok());
}

// One ping, two pongs. Using WriteLast API
TEST_P(AsyncEnd2endTest, SimpleServerStreamingWithCoalescingApiWL) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncWriter<EchoResponse> srv_stream(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::unique_ptr<ClientAsyncReader<EchoResponse>> cli_stream(
      stub_->AsyncResponseStream(&cli_ctx, send_request, cq_.get(), tag(1)));

  service_->RequestResponseStream(&srv_ctx, &recv_request, &srv_stream,
                                  cq_.get(), cq_.get(), tag(2));

  Verifier().Expect(1, true).Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  srv_stream.Write(send_response, tag(3));
  cli_stream->Read(&recv_response, tag(4));
  Verifier().Expect(3, true).Expect(4, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  srv_stream.WriteLast(send_response, WriteOptions(), tag(5));
  cli_stream->Read(&recv_response, tag(6));
  srv_stream.Finish(Status::OK, tag(7));
  Verifier().Expect(5, true).Expect(6, true).Expect(7, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  cli_stream->Read(&recv_response, tag(8));
  Verifier().Expect(8, false).Verify(cq_.get());

  cli_stream->Finish(&recv_status, tag(9));
  Verifier().Expect(9, true).Verify(cq_.get());

  EXPECT_TRUE(recv_status.ok());
}

// One ping, one pong.
TEST_P(AsyncEnd2endTest, SimpleBidiStreaming) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncReaderWriter<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::unique_ptr<ClientAsyncReaderWriter<EchoRequest, EchoResponse>>
      cli_stream(stub_->AsyncBidiStream(&cli_ctx, cq_.get(), tag(1)));

  service_->RequestBidiStream(&srv_ctx, &srv_stream, cq_.get(), cq_.get(),
                              tag(2));

  Verifier().Expect(1, true).Expect(2, true).Verify(cq_.get());

  cli_stream->Write(send_request, tag(3));
  srv_stream.Read(&recv_request, tag(4));
  Verifier().Expect(3, true).Expect(4, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  srv_stream.Write(send_response, tag(5));
  cli_stream->Read(&recv_response, tag(6));
  Verifier().Expect(5, true).Expect(6, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  cli_stream->WritesDone(tag(7));
  srv_stream.Read(&recv_request, tag(8));
  Verifier().Expect(7, true).Expect(8, false).Verify(cq_.get());

  srv_stream.Finish(Status::OK, tag(9));
  cli_stream->Finish(&recv_status, tag(10));
  Verifier().Expect(9, true).Expect(10, true).Verify(cq_.get());

  EXPECT_TRUE(recv_status.ok());
}

// One ping, one pong. Using server:WriteAndFinish api
TEST_P(AsyncEnd2endTest, SimpleBidiStreamingWithCoalescingApiWAF) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncReaderWriter<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  cli_ctx.set_initial_metadata_corked(true);
  std::unique_ptr<ClientAsyncReaderWriter<EchoRequest, EchoResponse>>
      cli_stream(stub_->AsyncBidiStream(&cli_ctx, cq_.get(), tag(1)));

  service_->RequestBidiStream(&srv_ctx, &srv_stream, cq_.get(), cq_.get(),
                              tag(2));

  cli_stream->WriteLast(send_request, WriteOptions(), tag(3));

  bool seen3 = false;

  Verifier().Expect(2, true).ExpectMaybe(3, true, &seen3).Verify(cq_.get());

  srv_stream.Read(&recv_request, tag(4));

  Verifier().ExpectUnless(3, true, seen3).Expect(4, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  srv_stream.Read(&recv_request, tag(5));
  Verifier().Expect(5, false).Verify(cq_.get());

  send_response.set_message(recv_request.message());
  srv_stream.WriteAndFinish(send_response, WriteOptions(), Status::OK, tag(6));
  cli_stream->Read(&recv_response, tag(7));
  Verifier().Expect(6, true).Expect(7, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  cli_stream->Finish(&recv_status, tag(8));
  Verifier().Expect(8, true).Verify(cq_.get());

  EXPECT_TRUE(recv_status.ok());
}

// One ping, one pong. Using server:WriteLast api
TEST_P(AsyncEnd2endTest, SimpleBidiStreamingWithCoalescingApiWL) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncReaderWriter<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  cli_ctx.set_initial_metadata_corked(true);
  std::unique_ptr<ClientAsyncReaderWriter<EchoRequest, EchoResponse>>
      cli_stream(stub_->AsyncBidiStream(&cli_ctx, cq_.get(), tag(1)));

  service_->RequestBidiStream(&srv_ctx, &srv_stream, cq_.get(), cq_.get(),
                              tag(2));

  cli_stream->WriteLast(send_request, WriteOptions(), tag(3));

  bool seen3 = false;

  Verifier().Expect(2, true).ExpectMaybe(3, true, &seen3).Verify(cq_.get());

  srv_stream.Read(&recv_request, tag(4));

  Verifier().ExpectUnless(3, true, seen3).Expect(4, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  srv_stream.Read(&recv_request, tag(5));
  Verifier().Expect(5, false).Verify(cq_.get());

  send_response.set_message(recv_request.message());
  srv_stream.WriteLast(send_response, WriteOptions(), tag(6));
  srv_stream.Finish(Status::OK, tag(7));
  cli_stream->Read(&recv_response, tag(8));
  Verifier().Expect(6, true).Expect(7, true).Expect(8, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  cli_stream->Finish(&recv_status, tag(9));
  Verifier().Expect(9, true).Verify(cq_.get());

  EXPECT_TRUE(recv_status.ok());
}

// Metadata tests
TEST_P(AsyncEnd2endTest, ClientInitialMetadataRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::pair<grpc::string, grpc::string> meta1("key1", "val1");
  std::pair<grpc::string, grpc::string> meta2("key2", "val2");
  std::pair<grpc::string, grpc::string> meta3("g.r.d-bin", "xyz");
  cli_ctx.AddMetadata(meta1.first, meta1.second);
  cli_ctx.AddMetadata(meta2.first, meta2.second);
  cli_ctx.AddMetadata(meta3.first, meta3.second);

  std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));
  response_reader->Finish(&recv_response, &recv_status, tag(4));

  service_->RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                        cq_.get(), tag(2));
  Verifier().Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());
  auto client_initial_metadata = srv_ctx.client_metadata();
  EXPECT_EQ(meta1.second,
            ToString(client_initial_metadata.find(meta1.first)->second));
  EXPECT_EQ(meta2.second,
            ToString(client_initial_metadata.find(meta2.first)->second));
  EXPECT_EQ(meta3.second,
            ToString(client_initial_metadata.find(meta3.first)->second));
  EXPECT_GE(client_initial_metadata.size(), static_cast<size_t>(2));

  send_response.set_message(recv_request.message());
  response_writer.Finish(send_response, Status::OK, tag(3));
  Verifier().Expect(3, true).Expect(4, true).Verify(cq_.get());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

TEST_P(AsyncEnd2endTest, ServerInitialMetadataRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::pair<grpc::string, grpc::string> meta1("key1", "val1");
  std::pair<grpc::string, grpc::string> meta2("key2", "val2");

  std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));
  response_reader->ReadInitialMetadata(tag(4));

  service_->RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                        cq_.get(), tag(2));
  Verifier().Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());
  srv_ctx.AddInitialMetadata(meta1.first, meta1.second);
  srv_ctx.AddInitialMetadata(meta2.first, meta2.second);
  response_writer.SendInitialMetadata(tag(3));
  Verifier().Expect(3, true).Expect(4, true).Verify(cq_.get());
  auto server_initial_metadata = cli_ctx.GetServerInitialMetadata();
  EXPECT_EQ(meta1.second,
            ToString(server_initial_metadata.find(meta1.first)->second));
  EXPECT_EQ(meta2.second,
            ToString(server_initial_metadata.find(meta2.first)->second));
  EXPECT_EQ(static_cast<size_t>(2), server_initial_metadata.size());

  send_response.set_message(recv_request.message());
  response_writer.Finish(send_response, Status::OK, tag(5));
  response_reader->Finish(&recv_response, &recv_status, tag(6));
  Verifier().Expect(5, true).Expect(6, true).Verify(cq_.get());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

TEST_P(AsyncEnd2endTest, ServerTrailingMetadataRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::pair<grpc::string, grpc::string> meta1("key1", "val1");
  std::pair<grpc::string, grpc::string> meta2("key2", "val2");

  std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));
  response_reader->Finish(&recv_response, &recv_status, tag(5));

  service_->RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                        cq_.get(), tag(2));
  Verifier().Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());
  response_writer.SendInitialMetadata(tag(3));
  Verifier().Expect(3, true).Verify(cq_.get());

  send_response.set_message(recv_request.message());
  srv_ctx.AddTrailingMetadata(meta1.first, meta1.second);
  srv_ctx.AddTrailingMetadata(meta2.first, meta2.second);
  response_writer.Finish(send_response, Status::OK, tag(4));

  Verifier().Expect(4, true).Expect(5, true).Verify(cq_.get());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
  auto server_trailing_metadata = cli_ctx.GetServerTrailingMetadata();
  EXPECT_EQ(meta1.second,
            ToString(server_trailing_metadata.find(meta1.first)->second));
  EXPECT_EQ(meta2.second,
            ToString(server_trailing_metadata.find(meta2.first)->second));
  EXPECT_EQ(static_cast<size_t>(2), server_trailing_metadata.size());
}

TEST_P(AsyncEnd2endTest, MetadataRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::pair<grpc::string, grpc::string> meta1("key1", "val1");
  std::pair<grpc::string, grpc::string> meta2(
      "key2-bin",
      grpc::string("\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc", 13));
  std::pair<grpc::string, grpc::string> meta3("key3", "val3");
  std::pair<grpc::string, grpc::string> meta6(
      "key4-bin",
      grpc::string("\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d",
                   14));
  std::pair<grpc::string, grpc::string> meta5("key5", "val5");
  std::pair<grpc::string, grpc::string> meta4(
      "key6-bin",
      grpc::string(
          "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee", 15));

  cli_ctx.AddMetadata(meta1.first, meta1.second);
  cli_ctx.AddMetadata(meta2.first, meta2.second);

  std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));
  response_reader->ReadInitialMetadata(tag(4));

  service_->RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                        cq_.get(), tag(2));
  Verifier().Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());
  auto client_initial_metadata = srv_ctx.client_metadata();
  EXPECT_EQ(meta1.second,
            ToString(client_initial_metadata.find(meta1.first)->second));
  EXPECT_EQ(meta2.second,
            ToString(client_initial_metadata.find(meta2.first)->second));
  EXPECT_GE(client_initial_metadata.size(), static_cast<size_t>(2));

  srv_ctx.AddInitialMetadata(meta3.first, meta3.second);
  srv_ctx.AddInitialMetadata(meta4.first, meta4.second);
  response_writer.SendInitialMetadata(tag(3));
  Verifier().Expect(3, true).Expect(4, true).Verify(cq_.get());
  auto server_initial_metadata = cli_ctx.GetServerInitialMetadata();
  EXPECT_EQ(meta3.second,
            ToString(server_initial_metadata.find(meta3.first)->second));
  EXPECT_EQ(meta4.second,
            ToString(server_initial_metadata.find(meta4.first)->second));
  EXPECT_GE(server_initial_metadata.size(), static_cast<size_t>(2));

  send_response.set_message(recv_request.message());
  srv_ctx.AddTrailingMetadata(meta5.first, meta5.second);
  srv_ctx.AddTrailingMetadata(meta6.first, meta6.second);
  response_writer.Finish(send_response, Status::OK, tag(5));
  response_reader->Finish(&recv_response, &recv_status, tag(6));

  Verifier().Expect(5, true).Expect(6, true).Verify(cq_.get());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
  auto server_trailing_metadata = cli_ctx.GetServerTrailingMetadata();
  EXPECT_EQ(meta5.second,
            ToString(server_trailing_metadata.find(meta5.first)->second));
  EXPECT_EQ(meta6.second,
            ToString(server_trailing_metadata.find(meta6.first)->second));
  EXPECT_GE(server_trailing_metadata.size(), static_cast<size_t>(2));
}

// Server uses AsyncNotifyWhenDone API to check for cancellation
TEST_P(AsyncEnd2endTest, ServerCheckCancellation) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));
  response_reader->Finish(&recv_response, &recv_status, tag(4));

  srv_ctx.AsyncNotifyWhenDone(tag(5));
  service_->RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                        cq_.get(), tag(2));

  Verifier().Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  cli_ctx.TryCancel();
  Verifier().Expect(5, true).Expect(4, true).Verify(cq_.get());
  EXPECT_TRUE(srv_ctx.IsCancelled());

  EXPECT_EQ(StatusCode::CANCELLED, recv_status.error_code());
}

// Server uses AsyncNotifyWhenDone API to check for normal finish
TEST_P(AsyncEnd2endTest, ServerCheckDone) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message(GetParam().message_content);
  std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));
  response_reader->Finish(&recv_response, &recv_status, tag(4));

  srv_ctx.AsyncNotifyWhenDone(tag(5));
  service_->RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                        cq_.get(), tag(2));

  Verifier().Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  response_writer.Finish(send_response, Status::OK, tag(3));
  Verifier().Expect(3, true).Expect(4, true).Expect(5, true).Verify(cq_.get());
  EXPECT_FALSE(srv_ctx.IsCancelled());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

TEST_P(AsyncEnd2endTest, UnimplementedRpc) {
  ChannelArguments args;
  auto channel_creds = GetCredentialsProvider()->GetChannelCredentials(
      GetParam().credentials_type, &args);
  std::shared_ptr<Channel> channel =
      !(GetParam().inproc)
          ? CreateCustomChannel(server_address_.str(), channel_creds, args)
          : server_->InProcessChannel(args);
  std::unique_ptr<grpc::testing::UnimplementedEchoService::Stub> stub;
  stub = grpc::testing::UnimplementedEchoService::NewStub(channel);
  EchoRequest send_request;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  send_request.set_message(GetParam().message_content);
  std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
      stub->AsyncUnimplemented(&cli_ctx, send_request, cq_.get()));

  response_reader->Finish(&recv_response, &recv_status, tag(4));
  Verifier().Expect(4, true).Verify(cq_.get());

  EXPECT_EQ(StatusCode::UNIMPLEMENTED, recv_status.error_code());
  EXPECT_EQ("", recv_status.error_message());
}

// This class is for testing scenarios where RPCs are cancelled on the server
// by calling ServerContext::TryCancel(). Server uses AsyncNotifyWhenDone
// API to check for cancellation
class AsyncEnd2endServerTryCancelTest : public AsyncEnd2endTest {
 protected:
  typedef enum {
    DO_NOT_CANCEL = 0,
    CANCEL_BEFORE_PROCESSING,
    CANCEL_DURING_PROCESSING,
    CANCEL_AFTER_PROCESSING
  } ServerTryCancelRequestPhase;

  // Helper for testing client-streaming RPCs which are cancelled on the server.
  // Depending on the value of server_try_cancel parameter, this will test one
  // of the following three scenarios:
  //   CANCEL_BEFORE_PROCESSING: Rpc is cancelled by the server before reading
  //   any messages from the client
  //
  //   CANCEL_DURING_PROCESSING: Rpc is cancelled by the server while reading
  //   messages from the client
  //
  //   CANCEL_AFTER PROCESSING: Rpc is cancelled by server after reading all
  //   messages from the client (but before sending any status back to the
  //   client)
  void TestClientStreamingServerCancel(
      ServerTryCancelRequestPhase server_try_cancel) {
    ResetStub();

    EchoRequest recv_request;
    EchoResponse send_response;
    EchoResponse recv_response;
    Status recv_status;

    ClientContext cli_ctx;
    ServerContext srv_ctx;
    ServerAsyncReader<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

    // Initiate the 'RequestStream' call on client
    CompletionQueue cli_cq;

    std::unique_ptr<ClientAsyncWriter<EchoRequest>> cli_stream(
        stub_->AsyncRequestStream(&cli_ctx, &recv_response, &cli_cq, tag(1)));

    // On the server, request to be notified of 'RequestStream' calls
    // and receive the 'RequestStream' call just made by the client
    srv_ctx.AsyncNotifyWhenDone(tag(11));
    service_->RequestRequestStream(&srv_ctx, &srv_stream, cq_.get(), cq_.get(),
                                   tag(2));
    std::thread t1([&cli_cq] { Verifier().Expect(1, true).Verify(&cli_cq); });
    Verifier().Expect(2, true).Verify(cq_.get());
    t1.join();

    bool expected_server_cq_result = true;
    bool expected_client_cq_result = true;

    if (server_try_cancel == CANCEL_BEFORE_PROCESSING) {
      srv_ctx.TryCancel();
      Verifier().Expect(11, true).Verify(cq_.get());
      EXPECT_TRUE(srv_ctx.IsCancelled());

      // Since cancellation is done before server reads any results, we know
      // for sure that all server cq results will return false from this
      // point forward
      expected_server_cq_result = false;
      expected_client_cq_result = false;
    }

    bool ignore_client_cq_result =
        (server_try_cancel == CANCEL_DURING_PROCESSING) ||
        (server_try_cancel == CANCEL_BEFORE_PROCESSING);

    std::thread cli_thread([&cli_cq, &cli_stream, &expected_client_cq_result,
                            &ignore_client_cq_result] {
      EchoRequest send_request;
      // Client sends 3 messages (tags 3, 4 and 5)
      for (int tag_idx = 3; tag_idx <= 5; tag_idx++) {
        send_request.set_message("Ping " + grpc::to_string(tag_idx));
        cli_stream->Write(send_request, tag(tag_idx));
        Verifier()
            .Expect(tag_idx, expected_client_cq_result)
            .Verify(&cli_cq, ignore_client_cq_result);
      }
      cli_stream->WritesDone(tag(6));
      // Ignore ok on WritesDone since cancel can affect it
      Verifier()
          .Expect(6, expected_client_cq_result)
          .Verify(&cli_cq, ignore_client_cq_result);
    });

    bool ignore_cq_result = false;
    bool want_done_tag = false;
    std::thread* server_try_cancel_thd = nullptr;

    auto verif = Verifier();

    if (server_try_cancel == CANCEL_DURING_PROCESSING) {
      server_try_cancel_thd =
          new std::thread(&ServerContext::TryCancel, &srv_ctx);
      // Server will cancel the RPC in a parallel thread while reading the
      // requests from the client. Since the cancellation can happen at anytime,
      // some of the cq results (i.e those until cancellation) might be true but
      // its non deterministic. So better to ignore the cq results
      ignore_cq_result = true;
      // Expect that we might possibly see the done tag that
      // indicates cancellation completion in this case
      want_done_tag = true;
      verif.Expect(11, true);
    }

    // Server reads 3 messages (tags 6, 7 and 8)
    // But if want_done_tag is true, we might also see tag 11
    for (int tag_idx = 6; tag_idx <= 8; tag_idx++) {
      srv_stream.Read(&recv_request, tag(tag_idx));
      // Note that we'll add something to the verifier and verify that
      // something was seen, but it might be tag 11 and not what we
      // just added
      int got_tag = verif.Expect(tag_idx, expected_server_cq_result)
                        .Next(cq_.get(), ignore_cq_result);
      GPR_ASSERT((got_tag == tag_idx) || (got_tag == 11 && want_done_tag));
      if (got_tag == 11) {
        EXPECT_TRUE(srv_ctx.IsCancelled());
        want_done_tag = false;
        // Now get the other entry that we were waiting on
        EXPECT_EQ(verif.Next(cq_.get(), ignore_cq_result), tag_idx);
      }
    }

    cli_thread.join();

    if (server_try_cancel_thd != nullptr) {
      server_try_cancel_thd->join();
      delete server_try_cancel_thd;
    }

    if (server_try_cancel == CANCEL_AFTER_PROCESSING) {
      srv_ctx.TryCancel();
      want_done_tag = true;
      verif.Expect(11, true);
    }

    if (want_done_tag) {
      verif.Verify(cq_.get());
      EXPECT_TRUE(srv_ctx.IsCancelled());
      want_done_tag = false;
    }

    // The RPC has been cancelled at this point for sure (i.e irrespective of
    // the value of `server_try_cancel` is). So, from this point forward, we
    // know that cq results are supposed to return false on server.

    // Server sends the final message and cancelled status (but the RPC is
    // already cancelled at this point. So we expect the operation to fail)
    srv_stream.Finish(send_response, Status::CANCELLED, tag(9));
    Verifier().Expect(9, false).Verify(cq_.get());

    // Client will see the cancellation
    cli_stream->Finish(&recv_status, tag(10));
    Verifier().Expect(10, true).Verify(&cli_cq);
    EXPECT_FALSE(recv_status.ok());
    EXPECT_EQ(::grpc::StatusCode::CANCELLED, recv_status.error_code());

    cli_cq.Shutdown();
    void* dummy_tag;
    bool dummy_ok;
    while (cli_cq.Next(&dummy_tag, &dummy_ok)) {
    }
  }

  // Helper for testing server-streaming RPCs which are cancelled on the server.
  // Depending on the value of server_try_cancel parameter, this will test one
  // of the following three scenarios:
  //   CANCEL_BEFORE_PROCESSING: Rpc is cancelled by the server before sending
  //   any messages to the client
  //
  //   CANCEL_DURING_PROCESSING: Rpc is cancelled by the server while sending
  //   messages to the client
  //
  //   CANCEL_AFTER PROCESSING: Rpc is cancelled by server after sending all
  //   messages to the client (but before sending any status back to the
  //   client)
  void TestServerStreamingServerCancel(
      ServerTryCancelRequestPhase server_try_cancel) {
    ResetStub();

    EchoRequest send_request;
    EchoRequest recv_request;
    EchoResponse send_response;
    Status recv_status;
    ClientContext cli_ctx;
    ServerContext srv_ctx;
    ServerAsyncWriter<EchoResponse> srv_stream(&srv_ctx);

    send_request.set_message("Ping");
    // Initiate the 'ResponseStream' call on the client
    CompletionQueue cli_cq;
    std::unique_ptr<ClientAsyncReader<EchoResponse>> cli_stream(
        stub_->AsyncResponseStream(&cli_ctx, send_request, &cli_cq, tag(1)));
    // On the server, request to be notified of 'ResponseStream' calls and
    // receive the call just made by the client
    srv_ctx.AsyncNotifyWhenDone(tag(11));
    service_->RequestResponseStream(&srv_ctx, &recv_request, &srv_stream,
                                    cq_.get(), cq_.get(), tag(2));

    std::thread t1([&cli_cq] { Verifier().Expect(1, true).Verify(&cli_cq); });
    Verifier().Expect(2, true).Verify(cq_.get());
    t1.join();

    EXPECT_EQ(send_request.message(), recv_request.message());

    bool expected_cq_result = true;
    bool ignore_cq_result = false;
    bool want_done_tag = false;
    bool expected_client_cq_result = true;
    bool ignore_client_cq_result =
        (server_try_cancel != CANCEL_BEFORE_PROCESSING);

    if (server_try_cancel == CANCEL_BEFORE_PROCESSING) {
      srv_ctx.TryCancel();
      Verifier().Expect(11, true).Verify(cq_.get());
      EXPECT_TRUE(srv_ctx.IsCancelled());

      // We know for sure that all cq results will be false from this point
      // since the server cancelled the RPC
      expected_cq_result = false;
      expected_client_cq_result = false;
    }

    std::thread cli_thread([&cli_cq, &cli_stream, &expected_client_cq_result,
                            &ignore_client_cq_result] {
      // Client attempts to read the three messages from the server
      for (int tag_idx = 6; tag_idx <= 8; tag_idx++) {
        EchoResponse recv_response;
        cli_stream->Read(&recv_response, tag(tag_idx));
        Verifier()
            .Expect(tag_idx, expected_client_cq_result)
            .Verify(&cli_cq, ignore_client_cq_result);
      }
    });

    std::thread* server_try_cancel_thd = nullptr;

    auto verif = Verifier();

    if (server_try_cancel == CANCEL_DURING_PROCESSING) {
      server_try_cancel_thd =
          new std::thread(&ServerContext::TryCancel, &srv_ctx);

      // Server will cancel the RPC in a parallel thread while writing responses
      // to the client. Since the cancellation can happen at anytime, some of
      // the cq results (i.e those until cancellation) might be true but it is
      // non deterministic. So better to ignore the cq results
      ignore_cq_result = true;
      // Expect that we might possibly see the done tag that
      // indicates cancellation completion in this case
      want_done_tag = true;
      verif.Expect(11, true);
    }

    // Server sends three messages (tags 3, 4 and 5)
    // But if want_done tag is true, we might also see tag 11
    for (int tag_idx = 3; tag_idx <= 5; tag_idx++) {
      send_response.set_message("Pong " + grpc::to_string(tag_idx));
      srv_stream.Write(send_response, tag(tag_idx));
      // Note that we'll add something to the verifier and verify that
      // something was seen, but it might be tag 11 and not what we
      // just added
      int got_tag = verif.Expect(tag_idx, expected_cq_result)
                        .Next(cq_.get(), ignore_cq_result);
      GPR_ASSERT((got_tag == tag_idx) || (got_tag == 11 && want_done_tag));
      if (got_tag == 11) {
        EXPECT_TRUE(srv_ctx.IsCancelled());
        want_done_tag = false;
        // Now get the other entry that we were waiting on
        EXPECT_EQ(verif.Next(cq_.get(), ignore_cq_result), tag_idx);
      }
    }

    if (server_try_cancel_thd != nullptr) {
      server_try_cancel_thd->join();
      delete server_try_cancel_thd;
    }

    if (server_try_cancel == CANCEL_AFTER_PROCESSING) {
      srv_ctx.TryCancel();
      want_done_tag = true;
      verif.Expect(11, true);
    }

    if (want_done_tag) {
      verif.Verify(cq_.get());
      EXPECT_TRUE(srv_ctx.IsCancelled());
      want_done_tag = false;
    }

    cli_thread.join();

    // The RPC has been cancelled at this point for sure (i.e irrespective of
    // the value of `server_try_cancel` is). So, from this point forward, we
    // know that cq results are supposed to return false on server.

    // Server finishes the stream (but the RPC is already cancelled)
    srv_stream.Finish(Status::CANCELLED, tag(9));
    Verifier().Expect(9, false).Verify(cq_.get());

    // Client will see the cancellation
    cli_stream->Finish(&recv_status, tag(10));
    Verifier().Expect(10, true).Verify(&cli_cq);
    EXPECT_FALSE(recv_status.ok());
    EXPECT_EQ(::grpc::StatusCode::CANCELLED, recv_status.error_code());

    cli_cq.Shutdown();
    void* dummy_tag;
    bool dummy_ok;
    while (cli_cq.Next(&dummy_tag, &dummy_ok)) {
    }
  }

  // Helper for testing bidirectinal-streaming RPCs which are cancelled on the
  // server.
  //
  // Depending on the value of server_try_cancel parameter, this will
  // test one of the following three scenarios:
  //   CANCEL_BEFORE_PROCESSING: Rpc is cancelled by the server before reading/
  //   writing any messages from/to the client
  //
  //   CANCEL_DURING_PROCESSING: Rpc is cancelled by the server while reading
  //   messages from the client
  //
  //   CANCEL_AFTER PROCESSING: Rpc is cancelled by server after reading all
  //   messages from the client (but before sending any status back to the
  //   client)
  void TestBidiStreamingServerCancel(
      ServerTryCancelRequestPhase server_try_cancel) {
    ResetStub();

    EchoRequest send_request;
    EchoRequest recv_request;
    EchoResponse send_response;
    EchoResponse recv_response;
    Status recv_status;
    ClientContext cli_ctx;
    ServerContext srv_ctx;
    ServerAsyncReaderWriter<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

    // Initiate the call from the client side
    std::unique_ptr<ClientAsyncReaderWriter<EchoRequest, EchoResponse>>
        cli_stream(stub_->AsyncBidiStream(&cli_ctx, cq_.get(), tag(1)));

    // On the server, request to be notified of the 'BidiStream' call and
    // receive the call just made by the client
    srv_ctx.AsyncNotifyWhenDone(tag(11));
    service_->RequestBidiStream(&srv_ctx, &srv_stream, cq_.get(), cq_.get(),
                                tag(2));
    Verifier().Expect(1, true).Expect(2, true).Verify(cq_.get());

    auto verif = Verifier();

    // Client sends the first and the only message
    send_request.set_message("Ping");
    cli_stream->Write(send_request, tag(3));
    verif.Expect(3, true);

    bool expected_cq_result = true;
    bool ignore_cq_result = false;
    bool want_done_tag = false;

    int got_tag, got_tag2;
    bool tag_3_done = false;

    if (server_try_cancel == CANCEL_BEFORE_PROCESSING) {
      srv_ctx.TryCancel();
      verif.Expect(11, true);
      // We know for sure that all server cq results will be false from
      // this point since the server cancelled the RPC. However, we can't
      // say for sure about the client
      expected_cq_result = false;
      ignore_cq_result = true;

      do {
        got_tag = verif.Next(cq_.get(), ignore_cq_result);
        GPR_ASSERT(((got_tag == 3) && !tag_3_done) || (got_tag == 11));
        if (got_tag == 3) {
          tag_3_done = true;
        }
      } while (got_tag != 11);
      EXPECT_TRUE(srv_ctx.IsCancelled());
    }

    std::thread* server_try_cancel_thd = nullptr;

    if (server_try_cancel == CANCEL_DURING_PROCESSING) {
      server_try_cancel_thd =
          new std::thread(&ServerContext::TryCancel, &srv_ctx);

      // Since server is going to cancel the RPC in a parallel thread, some of
      // the cq results (i.e those until the cancellation) might be true. Since
      // that number is non-deterministic, it is better to ignore the cq results
      ignore_cq_result = true;
      // Expect that we might possibly see the done tag that
      // indicates cancellation completion in this case
      want_done_tag = true;
      verif.Expect(11, true);
    }

    srv_stream.Read(&recv_request, tag(4));
    verif.Expect(4, expected_cq_result);
    got_tag = tag_3_done ? 3 : verif.Next(cq_.get(), ignore_cq_result);
    got_tag2 = verif.Next(cq_.get(), ignore_cq_result);
    GPR_ASSERT((got_tag == 3) || (got_tag == 4) ||
               (got_tag == 11 && want_done_tag));
    GPR_ASSERT((got_tag2 == 3) || (got_tag2 == 4) ||
               (got_tag2 == 11 && want_done_tag));
    // If we get 3 and 4, we don't need to wait for 11, but if
    // we get 11, we should also clear 3 and 4
    if (got_tag + got_tag2 != 7) {
      EXPECT_TRUE(srv_ctx.IsCancelled());
      want_done_tag = false;
      got_tag = verif.Next(cq_.get(), ignore_cq_result);
      GPR_ASSERT((got_tag == 3) || (got_tag == 4));
    }

    send_response.set_message("Pong");
    srv_stream.Write(send_response, tag(5));
    verif.Expect(5, expected_cq_result);

    cli_stream->Read(&recv_response, tag(6));
    verif.Expect(6, expected_cq_result);
    got_tag = verif.Next(cq_.get(), ignore_cq_result);
    got_tag2 = verif.Next(cq_.get(), ignore_cq_result);
    GPR_ASSERT((got_tag == 5) || (got_tag == 6) ||
               (got_tag == 11 && want_done_tag));
    GPR_ASSERT((got_tag2 == 5) || (got_tag2 == 6) ||
               (got_tag2 == 11 && want_done_tag));
    // If we get 5 and 6, we don't need to wait for 11, but if
    // we get 11, we should also clear 5 and 6
    if (got_tag + got_tag2 != 11) {
      EXPECT_TRUE(srv_ctx.IsCancelled());
      want_done_tag = false;
      got_tag = verif.Next(cq_.get(), ignore_cq_result);
      GPR_ASSERT((got_tag == 5) || (got_tag == 6));
    }

    // This is expected to succeed in all cases
    cli_stream->WritesDone(tag(7));
    verif.Expect(7, true);
    // TODO(vjpai): Consider whether the following is too flexible
    // or whether it should just be reset to ignore_cq_result
    bool ignore_cq_wd_result =
        ignore_cq_result || (server_try_cancel == CANCEL_BEFORE_PROCESSING);
    got_tag = verif.Next(cq_.get(), ignore_cq_wd_result);
    GPR_ASSERT((got_tag == 7) || (got_tag == 11 && want_done_tag));
    if (got_tag == 11) {
      EXPECT_TRUE(srv_ctx.IsCancelled());
      want_done_tag = false;
      // Now get the other entry that we were waiting on
      EXPECT_EQ(verif.Next(cq_.get(), ignore_cq_wd_result), 7);
    }

    // This is expected to fail in all cases i.e for all values of
    // server_try_cancel. This is because at this point, either there are no
    // more msgs from the client (because client called WritesDone) or the RPC
    // is cancelled on the server
    srv_stream.Read(&recv_request, tag(8));
    verif.Expect(8, false);
    got_tag = verif.Next(cq_.get(), ignore_cq_result);
    GPR_ASSERT((got_tag == 8) || (got_tag == 11 && want_done_tag));
    if (got_tag == 11) {
      EXPECT_TRUE(srv_ctx.IsCancelled());
      want_done_tag = false;
      // Now get the other entry that we were waiting on
      EXPECT_EQ(verif.Next(cq_.get(), ignore_cq_result), 8);
    }

    if (server_try_cancel_thd != nullptr) {
      server_try_cancel_thd->join();
      delete server_try_cancel_thd;
    }

    if (server_try_cancel == CANCEL_AFTER_PROCESSING) {
      srv_ctx.TryCancel();
      want_done_tag = true;
      verif.Expect(11, true);
    }

    if (want_done_tag) {
      verif.Verify(cq_.get());
      EXPECT_TRUE(srv_ctx.IsCancelled());
      want_done_tag = false;
    }

    // The RPC has been cancelled at this point for sure (i.e irrespective of
    // the value of `server_try_cancel` is). So, from this point forward, we
    // know that cq results are supposed to return false on server.

    srv_stream.Finish(Status::CANCELLED, tag(9));
    Verifier().Expect(9, false).Verify(cq_.get());

    cli_stream->Finish(&recv_status, tag(10));
    Verifier().Expect(10, true).Verify(cq_.get());
    EXPECT_FALSE(recv_status.ok());
    EXPECT_EQ(grpc::StatusCode::CANCELLED, recv_status.error_code());
  }
};

TEST_P(AsyncEnd2endServerTryCancelTest, ClientStreamingServerTryCancelBefore) {
  TestClientStreamingServerCancel(CANCEL_BEFORE_PROCESSING);
}

TEST_P(AsyncEnd2endServerTryCancelTest, ClientStreamingServerTryCancelDuring) {
  TestClientStreamingServerCancel(CANCEL_DURING_PROCESSING);
}

TEST_P(AsyncEnd2endServerTryCancelTest, ClientStreamingServerTryCancelAfter) {
  TestClientStreamingServerCancel(CANCEL_AFTER_PROCESSING);
}

TEST_P(AsyncEnd2endServerTryCancelTest, ServerStreamingServerTryCancelBefore) {
  TestServerStreamingServerCancel(CANCEL_BEFORE_PROCESSING);
}

TEST_P(AsyncEnd2endServerTryCancelTest, ServerStreamingServerTryCancelDuring) {
  TestServerStreamingServerCancel(CANCEL_DURING_PROCESSING);
}

TEST_P(AsyncEnd2endServerTryCancelTest, ServerStreamingServerTryCancelAfter) {
  TestServerStreamingServerCancel(CANCEL_AFTER_PROCESSING);
}

TEST_P(AsyncEnd2endServerTryCancelTest, ServerBidiStreamingTryCancelBefore) {
  TestBidiStreamingServerCancel(CANCEL_BEFORE_PROCESSING);
}

TEST_P(AsyncEnd2endServerTryCancelTest, ServerBidiStreamingTryCancelDuring) {
  TestBidiStreamingServerCancel(CANCEL_DURING_PROCESSING);
}

TEST_P(AsyncEnd2endServerTryCancelTest, ServerBidiStreamingTryCancelAfter) {
  TestBidiStreamingServerCancel(CANCEL_AFTER_PROCESSING);
}

std::vector<TestScenario> CreateTestScenarios(bool test_secure,
                                              int test_big_limit) {
  std::vector<TestScenario> scenarios;
  std::vector<grpc::string> credentials_types;
  std::vector<grpc::string> messages;

  auto insec_ok = [] {
    // Only allow insecure credentials type when it is registered with the
    // provider. User may create providers that do not have insecure.
    return GetCredentialsProvider()->GetChannelCredentials(
               kInsecureCredentialsType, nullptr) != nullptr;
  };

  if (insec_ok()) {
    credentials_types.push_back(kInsecureCredentialsType);
  }
  auto sec_list = GetCredentialsProvider()->GetSecureCredentialsTypeList();
  for (auto sec = sec_list.begin(); sec != sec_list.end(); sec++) {
    credentials_types.push_back(*sec);
  }
  GPR_ASSERT(!credentials_types.empty());

  messages.push_back("Hello");
  for (int sz = 1; sz <= test_big_limit; sz *= 32) {
    grpc::string big_msg;
    for (int i = 0; i < sz * 1024; i++) {
      char c = 'a' + (i % 26);
      big_msg += c;
    }
    messages.push_back(big_msg);
  }

  // TODO (sreek) Renable tests with health check service after the issue
  // https://github.com/grpc/grpc/issues/11223 is resolved
  for (auto health_check_service : {false}) {
    for (auto msg = messages.begin(); msg != messages.end(); msg++) {
      for (auto cred = credentials_types.begin();
           cred != credentials_types.end(); ++cred) {
        scenarios.emplace_back(false, *cred, health_check_service, *msg);
      }
      if (insec_ok()) {
        scenarios.emplace_back(true, kInsecureCredentialsType,
                               health_check_service, *msg);
      }
    }
  }
  return scenarios;
}

INSTANTIATE_TEST_CASE_P(AsyncEnd2end, AsyncEnd2endTest,
                        ::testing::ValuesIn(CreateTestScenarios(true, 1024)));
INSTANTIATE_TEST_CASE_P(AsyncEnd2endServerTryCancel,
                        AsyncEnd2endServerTryCancelTest,
                        ::testing::ValuesIn(CreateTestScenarios(false, 0)));

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  // Change the backup poll interval from 5s to 100ms to speed up the
  // ReconnectChannel test
  gpr_setenv("GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS", "100");
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
