/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <mutex>
#include <thread>

#include "src/core/security/credentials.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/echo_duplicate.grpc.pb.h"
#include "test/cpp/util/echo.grpc.pb.h"
#include "test/cpp/util/fake_credentials.h"
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>
#include <grpc++/fixed_size_thread_pool.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include <grpc++/time.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;
using std::chrono::system_clock;

namespace grpc {
namespace testing {

namespace {

const char* kServerCancelAfterReads = "cancel_after_reads";

// When echo_deadline is requested, deadline seen in the ServerContext is set in
// the response in seconds.
void MaybeEchoDeadline(ServerContext* context, const EchoRequest* request,
                       EchoResponse* response) {
  if (request->has_param() && request->param().echo_deadline()) {
    gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
    if (context->deadline() != system_clock::time_point::max()) {
      Timepoint2Timespec(context->deadline(), &deadline);
    }
    response->mutable_param()->set_request_deadline(deadline.tv_sec);
  }
}

template <typename T>
void CheckAuthContext(T* context) {
  std::shared_ptr<const AuthContext> auth_ctx = context->auth_context();
  std::vector<grpc::string> fake =
      auth_ctx->FindPropertyValues("transport_security_type");
  EXPECT_EQ(1u, fake.size());
  EXPECT_EQ("fake", fake[0]);
  EXPECT_TRUE(auth_ctx->GetPeerIdentityPropertyName().empty());
  EXPECT_TRUE(auth_ctx->GetPeerIdentity().empty());
}

}  // namespace

class TestServiceImpl : public ::grpc::cpp::test::util::TestService::Service {
 public:
  TestServiceImpl() : signal_client_(false), host_() {}
  explicit TestServiceImpl(const grpc::string& host)
      : signal_client_(false), host_(new grpc::string(host)) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    response->set_message(request->message());
    MaybeEchoDeadline(context, request, response);
    if (host_) {
      response->mutable_param()->set_host(*host_);
    }
    if (request->has_param() && request->param().client_cancel_after_us()) {
      {
        std::unique_lock<std::mutex> lock(mu_);
        signal_client_ = true;
      }
      while (!context->IsCancelled()) {
        gpr_sleep_until(gpr_time_add(
            gpr_now(GPR_CLOCK_REALTIME),
            gpr_time_from_micros(request->param().client_cancel_after_us(),
                                 GPR_TIMESPAN)));
      }
      return Status::CANCELLED;
    } else if (request->has_param() &&
               request->param().server_cancel_after_us()) {
      gpr_sleep_until(gpr_time_add(
          gpr_now(GPR_CLOCK_REALTIME),
          gpr_time_from_micros(request->param().server_cancel_after_us(),
                               GPR_TIMESPAN)));
      return Status::CANCELLED;
    } else {
      EXPECT_FALSE(context->IsCancelled());
    }

    if (request->has_param() && request->param().echo_metadata()) {
      const std::multimap<grpc::string, grpc::string>& client_metadata =
          context->client_metadata();
      for (std::multimap<grpc::string, grpc::string>::const_iterator iter =
               client_metadata.begin();
           iter != client_metadata.end(); ++iter) {
        context->AddTrailingMetadata((*iter).first, (*iter).second);
      }
    }
    if (request->has_param() && request->param().check_auth_context()) {
      CheckAuthContext(context);
    }
    return Status::OK;
  }

  // Unimplemented is left unimplemented to test the returned error.

  Status RequestStream(ServerContext* context,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) GRPC_OVERRIDE {
    EchoRequest request;
    response->set_message("");
    int cancel_after_reads = 0;
    const std::multimap<grpc::string, grpc::string> client_initial_metadata =
        context->client_metadata();
    if (client_initial_metadata.find(kServerCancelAfterReads) !=
        client_initial_metadata.end()) {
      std::istringstream iss(
          client_initial_metadata.find(kServerCancelAfterReads)->second);
      iss >> cancel_after_reads;
      gpr_log(GPR_INFO, "cancel_after_reads %d", cancel_after_reads);
    }
    while (reader->Read(&request)) {
      if (cancel_after_reads == 1) {
        gpr_log(GPR_INFO, "return cancel status");
        return Status::CANCELLED;
      } else if (cancel_after_reads > 0) {
        cancel_after_reads--;
      }
      response->mutable_message()->append(request.message());
    }
    return Status::OK;
  }

  // Return 3 messages.
  // TODO(yangg) make it generic by adding a parameter into EchoRequest
  Status ResponseStream(ServerContext* context, const EchoRequest* request,
                        ServerWriter<EchoResponse>* writer) GRPC_OVERRIDE {
    EchoResponse response;
    response.set_message(request->message() + "0");
    writer->Write(response);
    response.set_message(request->message() + "1");
    writer->Write(response);
    response.set_message(request->message() + "2");
    writer->Write(response);

    return Status::OK;
  }

  Status BidiStream(ServerContext* context,
                    ServerReaderWriter<EchoResponse, EchoRequest>* stream)
      GRPC_OVERRIDE {
    EchoRequest request;
    EchoResponse response;
    while (stream->Read(&request)) {
      gpr_log(GPR_INFO, "recv msg %s", request.message().c_str());
      response.set_message(request.message());
      stream->Write(response);
    }
    return Status::OK;
  }

  bool signal_client() {
    std::unique_lock<std::mutex> lock(mu_);
    return signal_client_;
  }

 private:
  bool signal_client_;
  std::mutex mu_;
  std::unique_ptr<grpc::string> host_;
};

class TestServiceImplDupPkg
    : public ::grpc::cpp::test::util::duplicate::TestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    response->set_message("no package");
    return Status::OK;
  }
};

class End2endTest : public ::testing::Test {
 protected:
  End2endTest()
      : kMaxMessageSize_(8192), special_service_("special"), thread_pool_(2) {}

  void SetUp() GRPC_OVERRIDE {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             FakeTransportSecurityServerCredentials());
    builder.RegisterService(&service_);
    builder.RegisterService("special", &special_service_);
    builder.SetMaxMessageSize(
        kMaxMessageSize_);  // For testing max message size.
    builder.RegisterService(&dup_pkg_service_);
    builder.SetThreadPool(&thread_pool_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() GRPC_OVERRIDE { server_->Shutdown(); }

  void ResetStub() {
    ChannelArguments args;
    args.SetString(GRPC_ARG_SECONDARY_USER_AGENT_STRING, "end2end_test");
    std::shared_ptr<ChannelInterface> channel = CreateChannel(
        server_address_.str(), FakeTransportSecurityCredentials(), args);
    stub_ = std::move(grpc::cpp::test::util::TestService::NewStub(channel));
  }

  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  const int kMaxMessageSize_;
  TestServiceImpl service_;
  TestServiceImpl special_service_;
  TestServiceImplDupPkg dup_pkg_service_;
  FixedSizeThreadPool thread_pool_;
};

static void SendRpc(grpc::cpp::test::util::TestService::Stub* stub,
                    int num_rpcs) {
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello hello hello hello");

  for (int i = 0; i < num_rpcs; ++i) {
    ClientContext context;
    context.set_compression_algorithm(GRPC_COMPRESS_GZIP);
    Status s = stub->Echo(&context, request, &response);
    EXPECT_EQ(response.message(), request.message());
    EXPECT_TRUE(s.ok());
  }
}

TEST_F(End2endTest, SimpleRpcWithHost) {
  ResetStub();

  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  context.set_authority("special");
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(response.has_param());
  EXPECT_EQ(response.param().host(), "special");
  EXPECT_TRUE(s.ok());
}

TEST_F(End2endTest, SimpleRpc) {
  ResetStub();
  SendRpc(stub_.get(), 1);
}

TEST_F(End2endTest, MultipleRpcs) {
  ResetStub();
  std::vector<std::thread*> threads;
  for (int i = 0; i < 10; ++i) {
    threads.push_back(new std::thread(SendRpc, stub_.get(), 10));
  }
  for (int i = 0; i < 10; ++i) {
    threads[i]->join();
    delete threads[i];
  }
}

// Set a 10us deadline and make sure proper error is returned.
TEST_F(End2endTest, RpcDeadlineExpires) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::microseconds(10);
  context.set_deadline(deadline);
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, s.error_code());
}

// Set a long but finite deadline.
TEST_F(End2endTest, RpcLongDeadline) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::hours(1);
  context.set_deadline(deadline);
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());
}

// Ask server to echo back the deadline it sees.
TEST_F(End2endTest, EchoDeadline) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_echo_deadline(true);

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(100);
  context.set_deadline(deadline);
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());
  gpr_timespec sent_deadline;
  Timepoint2Timespec(deadline, &sent_deadline);
  // Allow 1 second error.
  EXPECT_LE(response.param().request_deadline() - sent_deadline.tv_sec, 1);
  EXPECT_GE(response.param().request_deadline() - sent_deadline.tv_sec, -1);
}

// Ask server to echo back the deadline it sees. The rpc has no deadline.
TEST_F(End2endTest, EchoDeadlineForNoDeadlineRpc) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_echo_deadline(true);

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(response.param().request_deadline(),
            gpr_inf_future(GPR_CLOCK_REALTIME).tv_sec);
}

TEST_F(End2endTest, UnimplementedRpc) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  Status s = stub_->Unimplemented(&context, request, &response);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.error_code(), grpc::StatusCode::UNIMPLEMENTED);
  EXPECT_EQ(s.error_message(), "");
  EXPECT_EQ(response.message(), "");
}

TEST_F(End2endTest, RequestStreamOneRequest) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  auto stream = stub_->RequestStream(&context, &response);
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  stream->WritesDone();
  Status s = stream->Finish();
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());
}

TEST_F(End2endTest, RequestStreamTwoRequests) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  auto stream = stub_->RequestStream(&context, &response);
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Write(request));
  stream->WritesDone();
  Status s = stream->Finish();
  EXPECT_EQ(response.message(), "hellohello");
  EXPECT_TRUE(s.ok());
}

TEST_F(End2endTest, ResponseStream) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");

  auto stream = stub_->ResponseStream(&context, request);
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "0");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "1");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "2");
  EXPECT_FALSE(stream->Read(&response));

  Status s = stream->Finish();
  EXPECT_TRUE(s.ok());
}

TEST_F(End2endTest, BidiStream) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  grpc::string msg("hello");

  auto stream = stub_->BidiStream(&context);

  request.set_message(msg + "0");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message());

  request.set_message(msg + "1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message());

  request.set_message(msg + "2");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message());

  stream->WritesDone();
  EXPECT_FALSE(stream->Read(&response));

  Status s = stream->Finish();
  EXPECT_TRUE(s.ok());
}

// Talk to the two services with the same name but different package names.
// The two stubs are created on the same channel.
TEST_F(End2endTest, DiffPackageServices) {
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), FakeTransportSecurityCredentials(),
                    ChannelArguments());

  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub(
      grpc::cpp::test::util::TestService::NewStub(channel));
  ClientContext context;
  Status s = stub->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());

  std::unique_ptr<grpc::cpp::test::util::duplicate::TestService::Stub>
      dup_pkg_stub(
          grpc::cpp::test::util::duplicate::TestService::NewStub(channel));
  ClientContext context2;
  s = dup_pkg_stub->Echo(&context2, request, &response);
  EXPECT_EQ("no package", response.message());
  EXPECT_TRUE(s.ok());
}

// rpc and stream should fail on bad credentials.
TEST_F(End2endTest, BadCredentials) {
  std::shared_ptr<Credentials> bad_creds = ServiceAccountCredentials("", "", 1);
  EXPECT_EQ(static_cast<Credentials*>(nullptr), bad_creds.get());
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), bad_creds, ChannelArguments());
  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub(
      grpc::cpp::test::util::TestService::NewStub(channel));
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("Hello");

  Status s = stub->Echo(&context, request, &response);
  EXPECT_EQ("", response.message());
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(StatusCode::UNKNOWN, s.error_code());
  EXPECT_EQ("Rpc sent on a lame channel.", s.error_message());

  ClientContext context2;
  auto stream = stub->BidiStream(&context2);
  s = stream->Finish();
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(StatusCode::UNKNOWN, s.error_code());
  EXPECT_EQ("Rpc sent on a lame channel.", s.error_message());
}

void CancelRpc(ClientContext* context, int delay_us, TestServiceImpl* service) {
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_micros(delay_us, GPR_TIMESPAN)));
  while (!service->signal_client()) {
  }
  context->TryCancel();
}

// Client cancels rpc after 10ms
TEST_F(End2endTest, ClientCancelsRpc) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  const int kCancelDelayUs = 10 * 1000;
  request.mutable_param()->set_client_cancel_after_us(kCancelDelayUs);

  ClientContext context;
  std::thread cancel_thread(CancelRpc, &context, kCancelDelayUs, &service_);
  Status s = stub_->Echo(&context, request, &response);
  cancel_thread.join();
  EXPECT_EQ(StatusCode::CANCELLED, s.error_code());
  EXPECT_EQ(s.error_message(), "Cancelled");
}

// Server cancels rpc after 1ms
TEST_F(End2endTest, ServerCancelsRpc) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_server_cancel_after_us(1000);

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(StatusCode::CANCELLED, s.error_code());
  EXPECT_TRUE(s.error_message().empty());
}

// Client cancels request stream after sending two messages
TEST_F(End2endTest, ClientCancelsRequestStream) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");

  auto stream = stub_->RequestStream(&context, &response);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Write(request));

  context.TryCancel();

  Status s = stream->Finish();
  EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());

  EXPECT_EQ(response.message(), "");
}

// Client cancels server stream after sending some messages
TEST_F(End2endTest, ClientCancelsResponseStream) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");

  auto stream = stub_->ResponseStream(&context, request);

  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "0");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "1");

  context.TryCancel();

  // The cancellation races with responses, so there might be zero or
  // one responses pending, read till failure

  if (stream->Read(&response)) {
    EXPECT_EQ(response.message(), request.message() + "2");
    // Since we have cancelled, we expect the next attempt to read to fail
    EXPECT_FALSE(stream->Read(&response));
  }

  Status s = stream->Finish();
  // The final status could be either of CANCELLED or OK depending on
  // who won the race.
  EXPECT_GE(grpc::StatusCode::CANCELLED, s.error_code());
}

// Client cancels bidi stream after sending some messages
TEST_F(End2endTest, ClientCancelsBidi) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  grpc::string msg("hello");

  auto stream = stub_->BidiStream(&context);

  request.set_message(msg + "0");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message());

  request.set_message(msg + "1");
  EXPECT_TRUE(stream->Write(request));

  context.TryCancel();

  // The cancellation races with responses, so there might be zero or
  // one responses pending, read till failure

  if (stream->Read(&response)) {
    EXPECT_EQ(response.message(), request.message());
    // Since we have cancelled, we expect the next attempt to read to fail
    EXPECT_FALSE(stream->Read(&response));
  }

  Status s = stream->Finish();
  EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
}

TEST_F(End2endTest, RpcMaxMessageSize) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message(string(kMaxMessageSize_ * 2, 'a'));

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
}

bool MetadataContains(const std::multimap<grpc::string, grpc::string>& metadata,
                      const grpc::string& key, const grpc::string& value) {
  int count = 0;

  for (std::multimap<grpc::string, grpc::string>::const_iterator iter =
           metadata.begin();
       iter != metadata.end(); ++iter) {
    if ((*iter).first == key && (*iter).second == value) {
      count++;
    }
  }
  return count == 1;
}

TEST_F(End2endTest, SetPerCallCredentials) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  std::shared_ptr<Credentials> creds =
      IAMCredentials("fake_token", "fake_selector");
  context.set_credentials(creds);
  request.set_message("Hello");
  request.mutable_param()->set_echo_metadata(true);

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(request.message(), response.message());
  EXPECT_TRUE(s.ok());
  EXPECT_TRUE(MetadataContains(context.GetServerTrailingMetadata(),
                               GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
                               "fake_token"));
  EXPECT_TRUE(MetadataContains(context.GetServerTrailingMetadata(),
                               GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
                               "fake_selector"));
}

TEST_F(End2endTest, InsecurePerCallCredentials) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  std::shared_ptr<Credentials> creds = InsecureCredentials();
  context.set_credentials(creds);
  request.set_message("Hello");
  request.mutable_param()->set_echo_metadata(true);

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(StatusCode::CANCELLED, s.error_code());
  EXPECT_EQ("Failed to set credentials to rpc.", s.error_message());
}

TEST_F(End2endTest, OverridePerCallCredentials) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  std::shared_ptr<Credentials> creds1 =
      IAMCredentials("fake_token1", "fake_selector1");
  context.set_credentials(creds1);
  std::shared_ptr<Credentials> creds2 =
      IAMCredentials("fake_token2", "fake_selector2");
  context.set_credentials(creds2);
  request.set_message("Hello");
  request.mutable_param()->set_echo_metadata(true);

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(MetadataContains(context.GetServerTrailingMetadata(),
                               GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
                               "fake_token2"));
  EXPECT_TRUE(MetadataContains(context.GetServerTrailingMetadata(),
                               GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
                               "fake_selector2"));
  EXPECT_FALSE(MetadataContains(context.GetServerTrailingMetadata(),
                                GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
                                "fake_token1"));
  EXPECT_FALSE(MetadataContains(context.GetServerTrailingMetadata(),
                                GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
                                "fake_selector1"));
  EXPECT_EQ(request.message(), response.message());
  EXPECT_TRUE(s.ok());
}

// Client sends 20 requests and the server returns CANCELLED status after
// reading 10 requests.
TEST_F(End2endTest, RequestStreamServerEarlyCancelTest) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  context.AddMetadata(kServerCancelAfterReads, "10");
  auto stream = stub_->RequestStream(&context, &response);
  request.set_message("hello");
  int send_messages = 20;
  while (send_messages > 0) {
    EXPECT_TRUE(stream->Write(request));
    send_messages--;
  }
  stream->WritesDone();
  Status s = stream->Finish();
  EXPECT_EQ(s.error_code(), StatusCode::CANCELLED);
}

TEST_F(End2endTest, ClientAuthContext) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_check_auth_context(true);

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());

  CheckAuthContext(&context);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
