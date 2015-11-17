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

#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/auth_metadata_processor.h>
#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <gtest/gtest.h>

#include "src/core/security/credentials.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/echo_duplicate.grpc.pb.h"
#include "test/cpp/util/echo.grpc.pb.h"
#include "test/cpp/util/string_ref_helper.h"

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

void CheckServerAuthContext(const ServerContext* context,
                            const grpc::string& expected_client_identity) {
  std::shared_ptr<const AuthContext> auth_ctx = context->auth_context();
  std::vector<grpc::string_ref> ssl =
      auth_ctx->FindPropertyValues("transport_security_type");
  EXPECT_EQ(1u, ssl.size());
  EXPECT_EQ("ssl", ToString(ssl[0]));
  if (expected_client_identity.length() == 0) {
    EXPECT_TRUE(auth_ctx->GetPeerIdentityPropertyName().empty());
    EXPECT_TRUE(auth_ctx->GetPeerIdentity().empty());
    EXPECT_FALSE(auth_ctx->IsPeerAuthenticated());
  } else {
    auto identity = auth_ctx->GetPeerIdentity();
    EXPECT_TRUE(auth_ctx->IsPeerAuthenticated());
    EXPECT_EQ(1u, identity.size());
    EXPECT_EQ(expected_client_identity, identity[0]);
  }
}

bool CheckIsLocalhost(const grpc::string& addr) {
  const grpc::string kIpv6("ipv6:[::1]:");
  const grpc::string kIpv4MappedIpv6("ipv6:[::ffff:127.0.0.1]:");
  const grpc::string kIpv4("ipv4:127.0.0.1:");
  return addr.substr(0, kIpv4.size()) == kIpv4 ||
         addr.substr(0, kIpv4MappedIpv6.size()) == kIpv4MappedIpv6 ||
         addr.substr(0, kIpv6.size()) == kIpv6;
}

class TestMetadataCredentialsPlugin : public MetadataCredentialsPlugin {
 public:
  static const char kMetadataKey[];

  TestMetadataCredentialsPlugin(grpc::string_ref metadata_value,
                                bool is_blocking, bool is_successful)
      : metadata_value_(metadata_value.data(), metadata_value.length()),
        is_blocking_(is_blocking),
        is_successful_(is_successful) {}

  bool IsBlocking() const GRPC_OVERRIDE { return is_blocking_; }

  Status GetMetadata(grpc::string_ref service_url,
                     std::multimap<grpc::string, grpc::string>* metadata)
      GRPC_OVERRIDE {
    EXPECT_GT(service_url.length(), 0UL);
    EXPECT_TRUE(metadata != nullptr);
    if (is_successful_) {
      metadata->insert(std::make_pair(kMetadataKey, metadata_value_));
      return Status::OK;
    } else {
      return Status(StatusCode::NOT_FOUND, "Could not find plugin metadata.");
    }
  }

 private:
  grpc::string metadata_value_;
  bool is_blocking_;
  bool is_successful_;
};

const char TestMetadataCredentialsPlugin::kMetadataKey[] = "TestPluginMetadata";

class TestAuthMetadataProcessor : public AuthMetadataProcessor {
 public:
  static const char kGoodGuy[];

  TestAuthMetadataProcessor(bool is_blocking) : is_blocking_(is_blocking) {}

  std::shared_ptr<Credentials> GetCompatibleClientCreds() {
    return MetadataCredentialsFromPlugin(
        std::unique_ptr<MetadataCredentialsPlugin>(
            new TestMetadataCredentialsPlugin(kGoodGuy, is_blocking_, true)));
  }

  std::shared_ptr<Credentials> GetIncompatibleClientCreds() {
    return MetadataCredentialsFromPlugin(
        std::unique_ptr<MetadataCredentialsPlugin>(
            new TestMetadataCredentialsPlugin("Mr Hyde", is_blocking_, true)));
  }

  // Interface implementation
  bool IsBlocking() const GRPC_OVERRIDE { return is_blocking_; }

  Status Process(const InputMetadata& auth_metadata, AuthContext* context,
                 OutputMetadata* consumed_auth_metadata,
                 OutputMetadata* response_metadata) GRPC_OVERRIDE {
    EXPECT_TRUE(consumed_auth_metadata != nullptr);
    EXPECT_TRUE(context != nullptr);
    EXPECT_TRUE(response_metadata != nullptr);
    auto auth_md =
        auth_metadata.find(TestMetadataCredentialsPlugin::kMetadataKey);
    EXPECT_NE(auth_md, auth_metadata.end());
    string_ref auth_md_value = auth_md->second;
    if (auth_md_value == kGoodGuy) {
      context->AddProperty(kIdentityPropName, kGoodGuy);
      context->SetPeerIdentityPropertyName(kIdentityPropName);
      consumed_auth_metadata->insert(std::make_pair(
          string(auth_md->first.data(), auth_md->first.length()),
          string(auth_md->second.data(), auth_md->second.length())));
      return Status::OK;
    } else {
      return Status(StatusCode::UNAUTHENTICATED,
                    string("Invalid principal: ") +
                        string(auth_md_value.data(), auth_md_value.length()));
    }
  }

 private:
  static const char kIdentityPropName[];
  bool is_blocking_;
};

const char TestAuthMetadataProcessor::kGoodGuy[] = "Dr Jekyll";
const char TestAuthMetadataProcessor::kIdentityPropName[] = "novel identity";

class Proxy : public ::grpc::cpp::test::util::TestService::Service {
 public:
  Proxy(std::shared_ptr<Channel> channel)
      : stub_(grpc::cpp::test::util::TestService::NewStub(channel)) {}

  Status Echo(ServerContext* server_context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    std::unique_ptr<ClientContext> client_context =
        ClientContext::FromServerContext(*server_context);
    return stub_->Echo(client_context.get(), *request, response);
  }

 private:
  std::unique_ptr< ::grpc::cpp::test::util::TestService::Stub> stub_;
};

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
      const std::multimap<grpc::string_ref, grpc::string_ref>& client_metadata =
          context->client_metadata();
      for (std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator
               iter = client_metadata.begin();
           iter != client_metadata.end(); ++iter) {
        context->AddTrailingMetadata(ToString(iter->first),
                                     ToString(iter->second));
      }
    }
    if (request->has_param() &&
        (request->param().expected_client_identity().length() > 0 ||
         request->param().check_auth_context())) {
      CheckServerAuthContext(context,
                             request->param().expected_client_identity());
    }
    if (request->has_param() &&
        request->param().response_message_length() > 0) {
      response->set_message(
          grpc::string(request->param().response_message_length(), '\0'));
    }
    if (request->has_param() && request->param().echo_peer()) {
      response->mutable_param()->set_peer(context->peer());
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
    const std::multimap<grpc::string_ref, grpc::string_ref>&
        client_initial_metadata = context->client_metadata();
    if (client_initial_metadata.find(kServerCancelAfterReads) !=
        client_initial_metadata.end()) {
      std::istringstream iss(ToString(
          client_initial_metadata.find(kServerCancelAfterReads)->second));
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

class TestScenario {
 public:
  TestScenario(bool proxy, bool tls) : use_proxy(proxy), use_tls(tls) {}
  void Log() const {
    gpr_log(GPR_INFO, "Scenario: proxy %d, tls %d", use_proxy, use_tls);
  }
  bool use_proxy;
  bool use_tls;
};

class End2endTest : public ::testing::TestWithParam<TestScenario> {
 protected:
  End2endTest()
      : is_server_started_(false),
        kMaxMessageSize_(8192),
        special_service_("special") {
    GetParam().Log();
  }

  void TearDown() GRPC_OVERRIDE {
    if (is_server_started_) {
      server_->Shutdown();
      if (proxy_server_) proxy_server_->Shutdown();
    }
  }

  void StartServer(const std::shared_ptr<AuthMetadataProcessor>& processor) {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "127.0.0.1:" << port;
    // Setup server
    ServerBuilder builder;
    auto server_creds = InsecureServerCredentials();
    if (GetParam().use_tls) {
      SslServerCredentialsOptions::PemKeyCertPair pkcp = {test_server1_key,
                                                          test_server1_cert};
      SslServerCredentialsOptions ssl_opts;
      ssl_opts.pem_root_certs = "";
      ssl_opts.pem_key_cert_pairs.push_back(pkcp);
      server_creds = SslServerCredentials(ssl_opts);
      server_creds->SetAuthMetadataProcessor(processor);
    }
    builder.AddListeningPort(server_address_.str(), server_creds);
    builder.RegisterService(&service_);
    builder.RegisterService("foo.test.youtube.com", &special_service_);
    builder.SetMaxMessageSize(
        kMaxMessageSize_);  // For testing max message size.
    builder.RegisterService(&dup_pkg_service_);
    server_ = builder.BuildAndStart();
    is_server_started_ = true;
  }

  void ResetChannel() {
    if (!is_server_started_) {
      StartServer(std::shared_ptr<AuthMetadataProcessor>());
    }
    EXPECT_TRUE(is_server_started_);
    ChannelArguments args;
    auto channel_creds = InsecureCredentials();
    if (GetParam().use_tls) {
      SslCredentialsOptions ssl_opts = {test_root_cert, "", ""};
      args.SetSslTargetNameOverride("foo.test.google.fr");
      channel_creds = SslCredentials(ssl_opts);
    }
    args.SetString(GRPC_ARG_SECONDARY_USER_AGENT_STRING, "end2end_test");
    channel_ = CreateCustomChannel(server_address_.str(), channel_creds, args);
  }

  void ResetStub() {
    ResetChannel();
    if (GetParam().use_proxy) {
      proxy_service_.reset(new Proxy(channel_));
      int port = grpc_pick_unused_port_or_die();
      std::ostringstream proxyaddr;
      proxyaddr << "localhost:" << port;
      ServerBuilder builder;
      builder.AddListeningPort(proxyaddr.str(), InsecureServerCredentials());
      builder.RegisterService(proxy_service_.get());
      proxy_server_ = builder.BuildAndStart();

      channel_ = CreateChannel(proxyaddr.str(), InsecureCredentials());
    }

    stub_ = grpc::cpp::test::util::TestService::NewStub(channel_);
  }

  bool is_server_started_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<Server> proxy_server_;
  std::unique_ptr<Proxy> proxy_service_;
  std::ostringstream server_address_;
  const int kMaxMessageSize_;
  TestServiceImpl service_;
  TestServiceImpl special_service_;
  TestServiceImplDupPkg dup_pkg_service_;
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

TEST_P(End2endTest, RequestStreamOneRequest) {
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

TEST_P(End2endTest, RequestStreamTwoRequests) {
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

TEST_P(End2endTest, ResponseStream) {
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

TEST_P(End2endTest, BidiStream) {
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
TEST_P(End2endTest, DiffPackageServices) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());

  std::unique_ptr<grpc::cpp::test::util::duplicate::TestService::Stub>
      dup_pkg_stub(
          grpc::cpp::test::util::duplicate::TestService::NewStub(channel_));
  ClientContext context2;
  s = dup_pkg_stub->Echo(&context2, request, &response);
  EXPECT_EQ("no package", response.message());
  EXPECT_TRUE(s.ok());
}

void CancelRpc(ClientContext* context, int delay_us, TestServiceImpl* service) {
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_micros(delay_us, GPR_TIMESPAN)));
  while (!service->signal_client()) {
  }
  context->TryCancel();
}

TEST_P(End2endTest, CancelRpcBeforeStart) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");
  context.TryCancel();
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ("", response.message());
  EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
}

// Client cancels request stream after sending two messages
TEST_P(End2endTest, ClientCancelsRequestStream) {
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
TEST_P(End2endTest, ClientCancelsResponseStream) {
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
TEST_P(End2endTest, ClientCancelsBidi) {
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

TEST_P(End2endTest, RpcMaxMessageSize) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message(string(kMaxMessageSize_ * 2, 'a'));

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
}

// Client sends 20 requests and the server returns CANCELLED status after
// reading 10 requests.
TEST_P(End2endTest, RequestStreamServerEarlyCancelTest) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  context.AddMetadata(kServerCancelAfterReads, "10");
  auto stream = stub_->RequestStream(&context, &response);
  request.set_message("hello");
  int send_messages = 20;
  while (send_messages > 10) {
    EXPECT_TRUE(stream->Write(request));
    send_messages--;
  }
  while (send_messages > 0) {
    stream->Write(request);
    send_messages--;
  }
  stream->WritesDone();
  Status s = stream->Finish();
  EXPECT_EQ(s.error_code(), StatusCode::CANCELLED);
}

void ReaderThreadFunc(ClientReaderWriter<EchoRequest, EchoResponse>* stream,
                      gpr_event* ev) {
  EchoResponse resp;
  gpr_event_set(ev, (void*)1);
  while (stream->Read(&resp)) {
    gpr_log(GPR_INFO, "Read message");
  }
}

// Run a Read and a WritesDone simultaneously.
TEST_P(End2endTest, SimultaneousReadWritesDone) {
  ResetStub();
  ClientContext context;
  gpr_event ev;
  gpr_event_init(&ev);
  auto stream = stub_->BidiStream(&context);
  std::thread reader_thread(ReaderThreadFunc, stream.get(), &ev);
  gpr_event_wait(&ev, gpr_inf_future(GPR_CLOCK_REALTIME));
  stream->WritesDone();
  Status s = stream->Finish();
  EXPECT_TRUE(s.ok());
  reader_thread.join();
}

TEST_P(End2endTest, ChannelState) {
  ResetStub();
  // Start IDLE
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(false));

  // Did not ask to connect, no state change.
  CompletionQueue cq;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::milliseconds(10);
  channel_->NotifyOnStateChange(GRPC_CHANNEL_IDLE, deadline, &cq, NULL);
  void* tag;
  bool ok = true;
  cq.Next(&tag, &ok);
  EXPECT_FALSE(ok);

  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(true));
  EXPECT_TRUE(channel_->WaitForStateChange(GRPC_CHANNEL_IDLE,
                                           gpr_inf_future(GPR_CLOCK_REALTIME)));
  auto state = channel_->GetState(false);
  EXPECT_TRUE(state == GRPC_CHANNEL_CONNECTING || state == GRPC_CHANNEL_READY);
}

// Takes 10s.
TEST_P(End2endTest, ChannelStateTimeout) {
  if (GetParam().use_tls) {
    return;
  }
  int port = grpc_pick_unused_port_or_die();
  std::ostringstream server_address;
  server_address << "127.0.0.1:" << port;
  // Channel to non-existing server
  auto channel = CreateChannel(server_address.str(), InsecureCredentials());
  // Start IDLE
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel->GetState(true));

  auto state = GRPC_CHANNEL_IDLE;
  for (int i = 0; i < 10; i++) {
    channel->WaitForStateChange(
        state, std::chrono::system_clock::now() + std::chrono::seconds(1));
    state = channel->GetState(false);
  }
}

// Talking to a non-existing service.
TEST_P(End2endTest, NonExistingService) {
  ResetChannel();
  std::unique_ptr<grpc::cpp::test::util::UnimplementedService::Stub> stub;
  stub = grpc::cpp::test::util::UnimplementedService::NewStub(channel_);

  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  Status s = stub->Unimplemented(&context, request, &response);
  EXPECT_EQ(StatusCode::UNIMPLEMENTED, s.error_code());
  EXPECT_EQ("", s.error_message());
}

//////////////////////////////////////////////////////////////////////////
// Test with and without a proxy.
class ProxyEnd2endTest : public End2endTest {
 protected:
};

TEST_P(ProxyEnd2endTest, SimpleRpc) {
  ResetStub();
  SendRpc(stub_.get(), 1);
}

TEST_P(ProxyEnd2endTest, MultipleRpcs) {
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
TEST_P(ProxyEnd2endTest, RpcDeadlineExpires) {
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
TEST_P(ProxyEnd2endTest, RpcLongDeadline) {
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
TEST_P(ProxyEnd2endTest, EchoDeadline) {
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
TEST_P(ProxyEnd2endTest, EchoDeadlineForNoDeadlineRpc) {
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

TEST_P(ProxyEnd2endTest, UnimplementedRpc) {
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

// Client cancels rpc after 10ms
TEST_P(ProxyEnd2endTest, ClientCancelsRpc) {
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
TEST_P(ProxyEnd2endTest, ServerCancelsRpc) {
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

// Make the response larger than the flow control window.
TEST_P(ProxyEnd2endTest, HugeResponse) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("huge response");
  const size_t kResponseSize = 1024 * (1024 + 10);
  request.mutable_param()->set_response_message_length(kResponseSize);

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(kResponseSize, response.message().size());
  EXPECT_TRUE(s.ok());
}

TEST_P(ProxyEnd2endTest, Peer) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("hello");
  request.mutable_param()->set_echo_peer(true);

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());
  EXPECT_TRUE(CheckIsLocalhost(response.param().peer()));
  EXPECT_TRUE(CheckIsLocalhost(context.peer()));
}

//////////////////////////////////////////////////////////////////////////
class SecureEnd2endTest : public End2endTest {
 protected:
  SecureEnd2endTest() {
    GPR_ASSERT(!GetParam().use_proxy);
    GPR_ASSERT(GetParam().use_tls);
  }
};

TEST_P(SecureEnd2endTest, SimpleRpcWithHost) {
  ResetStub();

  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  context.set_authority("foo.test.youtube.com");
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(response.has_param());
  EXPECT_EQ("special", response.param().host());
  EXPECT_TRUE(s.ok());
}

// rpc and stream should fail on bad credentials.
TEST_P(SecureEnd2endTest, BadCredentials) {
  std::shared_ptr<Credentials> bad_creds = GoogleRefreshTokenCredentials("");
  EXPECT_EQ(static_cast<Credentials*>(nullptr), bad_creds.get());
  std::shared_ptr<Channel> channel =
      CreateChannel(server_address_.str(), bad_creds);
  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub(
      grpc::cpp::test::util::TestService::NewStub(channel));
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("Hello");

  Status s = stub->Echo(&context, request, &response);
  EXPECT_EQ("", response.message());
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(StatusCode::INVALID_ARGUMENT, s.error_code());
  EXPECT_EQ("Invalid credentials.", s.error_message());

  ClientContext context2;
  auto stream = stub->BidiStream(&context2);
  s = stream->Finish();
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(StatusCode::INVALID_ARGUMENT, s.error_code());
  EXPECT_EQ("Invalid credentials.", s.error_message());
}

bool MetadataContains(
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    const grpc::string& key, const grpc::string& value) {
  int count = 0;

  for (std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator iter =
           metadata.begin();
       iter != metadata.end(); ++iter) {
    if (ToString(iter->first) == key && ToString(iter->second) == value) {
      count++;
    }
  }
  return count == 1;
}

TEST_P(SecureEnd2endTest, BlockingAuthMetadataPluginAndProcessorSuccess) {
  auto* processor = new TestAuthMetadataProcessor(true);
  StartServer(std::shared_ptr<AuthMetadataProcessor>(processor));
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_credentials(processor->GetCompatibleClientCreds());
  request.set_message("Hello");
  request.mutable_param()->set_echo_metadata(true);
  request.mutable_param()->set_expected_client_identity(
      TestAuthMetadataProcessor::kGoodGuy);

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(request.message(), response.message());
  EXPECT_TRUE(s.ok());

  // Metadata should have been consumed by the processor.
  EXPECT_FALSE(MetadataContains(
      context.GetServerTrailingMetadata(), GRPC_AUTHORIZATION_METADATA_KEY,
      grpc::string("Bearer ") + TestAuthMetadataProcessor::kGoodGuy));
}

TEST_P(SecureEnd2endTest, BlockingAuthMetadataPluginAndProcessorFailure) {
  auto* processor = new TestAuthMetadataProcessor(true);
  StartServer(std::shared_ptr<AuthMetadataProcessor>(processor));
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_credentials(processor->GetIncompatibleClientCreds());
  request.set_message("Hello");

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.error_code(), StatusCode::UNAUTHENTICATED);
}
TEST_P(SecureEnd2endTest, SetPerCallCredentials) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  std::shared_ptr<Credentials> creds =
      GoogleIAMCredentials("fake_token", "fake_selector");
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

TEST_P(SecureEnd2endTest, InsecurePerCallCredentials) {
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

TEST_P(SecureEnd2endTest, OverridePerCallCredentials) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  std::shared_ptr<Credentials> creds1 =
      GoogleIAMCredentials("fake_token1", "fake_selector1");
  context.set_credentials(creds1);
  std::shared_ptr<Credentials> creds2 =
      GoogleIAMCredentials("fake_token2", "fake_selector2");
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

TEST_P(SecureEnd2endTest, NonBlockingAuthMetadataPluginFailure) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_credentials(
      MetadataCredentialsFromPlugin(std::unique_ptr<MetadataCredentialsPlugin>(
          new TestMetadataCredentialsPlugin(
              "Does not matter, will fail anyway (see 3rd param)", false,
              false))));
  request.set_message("Hello");

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.error_code(), StatusCode::UNAUTHENTICATED);
}

TEST_P(SecureEnd2endTest, NonBlockingAuthMetadataPluginAndProcessorSuccess) {
  auto* processor = new TestAuthMetadataProcessor(false);
  StartServer(std::shared_ptr<AuthMetadataProcessor>(processor));
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_credentials(processor->GetCompatibleClientCreds());
  request.set_message("Hello");
  request.mutable_param()->set_echo_metadata(true);
  request.mutable_param()->set_expected_client_identity(
      TestAuthMetadataProcessor::kGoodGuy);

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(request.message(), response.message());
  EXPECT_TRUE(s.ok());

  // Metadata should have been consumed by the processor.
  EXPECT_FALSE(MetadataContains(
      context.GetServerTrailingMetadata(), GRPC_AUTHORIZATION_METADATA_KEY,
      grpc::string("Bearer ") + TestAuthMetadataProcessor::kGoodGuy));
}

TEST_P(SecureEnd2endTest, NonBlockingAuthMetadataPluginAndProcessorFailure) {
  auto* processor = new TestAuthMetadataProcessor(false);
  StartServer(std::shared_ptr<AuthMetadataProcessor>(processor));
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_credentials(processor->GetIncompatibleClientCreds());
  request.set_message("Hello");

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.error_code(), StatusCode::UNAUTHENTICATED);
}

TEST_P(SecureEnd2endTest, BlockingAuthMetadataPluginFailure) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_credentials(
      MetadataCredentialsFromPlugin(std::unique_ptr<MetadataCredentialsPlugin>(
          new TestMetadataCredentialsPlugin(
              "Does not matter, will fail anyway (see 3rd param)", true,
              false))));
  request.set_message("Hello");

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.error_code(), StatusCode::UNAUTHENTICATED);
}

TEST_P(SecureEnd2endTest, ClientAuthContext) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_check_auth_context(true);

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());

  std::shared_ptr<const AuthContext> auth_ctx = context.auth_context();
  std::vector<grpc::string_ref> ssl =
      auth_ctx->FindPropertyValues("transport_security_type");
  EXPECT_EQ(1u, ssl.size());
  EXPECT_EQ("ssl", ToString(ssl[0]));
  EXPECT_EQ("x509_subject_alternative_name",
            auth_ctx->GetPeerIdentityPropertyName());
  EXPECT_EQ(3u, auth_ctx->GetPeerIdentity().size());
  EXPECT_EQ("*.test.google.fr", ToString(auth_ctx->GetPeerIdentity()[0]));
  EXPECT_EQ("waterzooi.test.google.be",
            ToString(auth_ctx->GetPeerIdentity()[1]));
  EXPECT_EQ("*.test.youtube.com", ToString(auth_ctx->GetPeerIdentity()[2]));
}

INSTANTIATE_TEST_CASE_P(End2end, End2endTest,
                        ::testing::Values(TestScenario(false, true),
                                          TestScenario(false, false)));

INSTANTIATE_TEST_CASE_P(ProxyEnd2end, ProxyEnd2endTest,
                        ::testing::Values(TestScenario(true, true),
                                          TestScenario(true, false),
                                          TestScenario(false, true),
                                          TestScenario(false, false)));

INSTANTIATE_TEST_CASE_P(SecureEnd2end, SecureEnd2endTest,
                        ::testing::Values(TestScenario(false, true)));

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
