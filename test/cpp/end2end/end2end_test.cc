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

#include <mutex>
#include <thread>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/resource_quota.h>
#include <grpc++/security/auth_metadata_processor.h>
#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/support/env.h"
#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
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

bool CheckIsLocalhost(const grpc::string& addr) {
  const grpc::string kIpv6("ipv6:[::1]:");
  const grpc::string kIpv4MappedIpv6("ipv6:[::ffff:127.0.0.1]:");
  const grpc::string kIpv4("ipv4:127.0.0.1:");
  return addr.substr(0, kIpv4.size()) == kIpv4 ||
         addr.substr(0, kIpv4MappedIpv6.size()) == kIpv4MappedIpv6 ||
         addr.substr(0, kIpv6.size()) == kIpv6;
}

const char kTestCredsPluginErrorMsg[] = "Could not find plugin metadata.";

class TestMetadataCredentialsPlugin : public MetadataCredentialsPlugin {
 public:
  static const char kGoodMetadataKey[];
  static const char kBadMetadataKey[];

  TestMetadataCredentialsPlugin(grpc::string_ref metadata_key,
                                grpc::string_ref metadata_value,
                                bool is_blocking, bool is_successful)
      : metadata_key_(metadata_key.data(), metadata_key.length()),
        metadata_value_(metadata_value.data(), metadata_value.length()),
        is_blocking_(is_blocking),
        is_successful_(is_successful) {}

  bool IsBlocking() const override { return is_blocking_; }

  Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const grpc::AuthContext& channel_auth_context,
      std::multimap<grpc::string, grpc::string>* metadata) override {
    EXPECT_GT(service_url.length(), 0UL);
    EXPECT_GT(method_name.length(), 0UL);
    EXPECT_TRUE(channel_auth_context.IsPeerAuthenticated());
    EXPECT_TRUE(metadata != nullptr);
    if (is_successful_) {
      metadata->insert(std::make_pair(metadata_key_, metadata_value_));
      return Status::OK;
    } else {
      return Status(StatusCode::NOT_FOUND, kTestCredsPluginErrorMsg);
    }
  }

 private:
  grpc::string metadata_key_;
  grpc::string metadata_value_;
  bool is_blocking_;
  bool is_successful_;
};

const char TestMetadataCredentialsPlugin::kBadMetadataKey[] =
    "TestPluginMetadata";
const char TestMetadataCredentialsPlugin::kGoodMetadataKey[] =
    "test-plugin-metadata";

class TestAuthMetadataProcessor : public AuthMetadataProcessor {
 public:
  static const char kGoodGuy[];

  TestAuthMetadataProcessor(bool is_blocking) : is_blocking_(is_blocking) {}

  std::shared_ptr<CallCredentials> GetCompatibleClientCreds() {
    return MetadataCredentialsFromPlugin(
        std::unique_ptr<MetadataCredentialsPlugin>(
            new TestMetadataCredentialsPlugin(
                TestMetadataCredentialsPlugin::kGoodMetadataKey, kGoodGuy,
                is_blocking_, true)));
  }

  std::shared_ptr<CallCredentials> GetIncompatibleClientCreds() {
    return MetadataCredentialsFromPlugin(
        std::unique_ptr<MetadataCredentialsPlugin>(
            new TestMetadataCredentialsPlugin(
                TestMetadataCredentialsPlugin::kGoodMetadataKey, "Mr Hyde",
                is_blocking_, true)));
  }

  // Interface implementation
  bool IsBlocking() const override { return is_blocking_; }

  Status Process(const InputMetadata& auth_metadata, AuthContext* context,
                 OutputMetadata* consumed_auth_metadata,
                 OutputMetadata* response_metadata) override {
    EXPECT_TRUE(consumed_auth_metadata != nullptr);
    EXPECT_TRUE(context != nullptr);
    EXPECT_TRUE(response_metadata != nullptr);
    auto auth_md =
        auth_metadata.find(TestMetadataCredentialsPlugin::kGoodMetadataKey);
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

class Proxy : public ::grpc::testing::EchoTestService::Service {
 public:
  Proxy(std::shared_ptr<Channel> channel)
      : stub_(grpc::testing::EchoTestService::NewStub(channel)) {}

  Status Echo(ServerContext* server_context, const EchoRequest* request,
              EchoResponse* response) override {
    std::unique_ptr<ClientContext> client_context =
        ClientContext::FromServerContext(*server_context);
    return stub_->Echo(client_context.get(), *request, response);
  }

 private:
  std::unique_ptr< ::grpc::testing::EchoTestService::Stub> stub_;
};

class TestServiceImplDupPkg
    : public ::grpc::testing::duplicate::EchoTestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    response->set_message("no package");
    return Status::OK;
  }
};

class TestScenario {
 public:
  TestScenario(bool proxy, bool inproc_stub, const grpc::string& creds_type)
      : use_proxy(proxy), inproc(inproc_stub), credentials_type(creds_type) {}
  void Log() const;
  bool use_proxy;
  bool inproc;
  const grpc::string credentials_type;
};

static std::ostream& operator<<(std::ostream& out,
                                const TestScenario& scenario) {
  return out << "TestScenario{use_proxy="
             << (scenario.use_proxy ? "true" : "false")
             << ", inproc=" << (scenario.inproc ? "true" : "false")
             << ", credentials='" << scenario.credentials_type << "'}";
}

void TestScenario::Log() const {
  std::ostringstream out;
  out << *this;
  gpr_log(GPR_DEBUG, "%s", out.str().c_str());
}

class End2endTest : public ::testing::TestWithParam<TestScenario> {
 protected:
  End2endTest()
      : is_server_started_(false),
        kMaxMessageSize_(8192),
        special_service_("special"),
        first_picked_port_(0) {
    GetParam().Log();
  }

  void TearDown() override {
    if (is_server_started_) {
      server_->Shutdown();
      if (proxy_server_) proxy_server_->Shutdown();
    }
    if (first_picked_port_ > 0) {
      grpc_recycle_unused_port(first_picked_port_);
    }
  }

  void StartServer(const std::shared_ptr<AuthMetadataProcessor>& processor) {
    int port = grpc_pick_unused_port_or_die();
    first_picked_port_ = port;
    server_address_ << "127.0.0.1:" << port;
    // Setup server
    BuildAndStartServer(processor);
  }

  void RestartServer(const std::shared_ptr<AuthMetadataProcessor>& processor) {
    if (is_server_started_) {
      server_->Shutdown();
      BuildAndStartServer(processor);
    }
  }

  void BuildAndStartServer(
      const std::shared_ptr<AuthMetadataProcessor>& processor) {
    ServerBuilder builder;
    ConfigureServerBuilder(&builder);
    auto server_creds = GetCredentialsProvider()->GetServerCredentials(
        GetParam().credentials_type);
    if (GetParam().credentials_type != kInsecureCredentialsType) {
      server_creds->SetAuthMetadataProcessor(processor);
    }
    builder.AddListeningPort(server_address_.str(), server_creds);
    builder.RegisterService(&service_);
    builder.RegisterService("foo.test.youtube.com", &special_service_);
    builder.RegisterService(&dup_pkg_service_);

    builder.SetSyncServerOption(ServerBuilder::SyncServerOption::NUM_CQS, 4);
    builder.SetSyncServerOption(
        ServerBuilder::SyncServerOption::CQ_TIMEOUT_MSEC, 10);

    server_ = builder.BuildAndStart();
    is_server_started_ = true;
  }

  virtual void ConfigureServerBuilder(ServerBuilder* builder) {
    builder->SetMaxMessageSize(
        kMaxMessageSize_);  // For testing max message size.
  }

  void ResetChannel() {
    if (!is_server_started_) {
      StartServer(std::shared_ptr<AuthMetadataProcessor>());
    }
    EXPECT_TRUE(is_server_started_);
    ChannelArguments args;
    auto channel_creds = GetCredentialsProvider()->GetChannelCredentials(
        GetParam().credentials_type, &args);
    if (!user_agent_prefix_.empty()) {
      args.SetUserAgentPrefix(user_agent_prefix_);
    }
    args.SetString(GRPC_ARG_SECONDARY_USER_AGENT_STRING, "end2end_test");

    if (!GetParam().inproc) {
      channel_ =
          CreateCustomChannel(server_address_.str(), channel_creds, args);
    } else {
      channel_ = server_->InProcessChannel(args);
    }
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

      builder.SetSyncServerOption(ServerBuilder::SyncServerOption::NUM_CQS, 4);
      builder.SetSyncServerOption(
          ServerBuilder::SyncServerOption::CQ_TIMEOUT_MSEC, 10);

      proxy_server_ = builder.BuildAndStart();

      channel_ = CreateChannel(proxyaddr.str(), InsecureChannelCredentials());
    }

    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  bool is_server_started_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<Server> proxy_server_;
  std::unique_ptr<Proxy> proxy_service_;
  std::ostringstream server_address_;
  const int kMaxMessageSize_;
  TestServiceImpl service_;
  TestServiceImpl special_service_;
  TestServiceImplDupPkg dup_pkg_service_;
  grpc::string user_agent_prefix_;
  int first_picked_port_;
};

static void SendRpc(grpc::testing::EchoTestService::Stub* stub, int num_rpcs,
                    bool with_binary_metadata) {
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello hello hello hello");

  for (int i = 0; i < num_rpcs; ++i) {
    ClientContext context;
    if (with_binary_metadata) {
      char bytes[8] = {'\0', '\1', '\2', '\3', '\4', '\5', '\6', (char)i};
      context.AddMetadata("custom-bin", grpc::string(bytes, 8));
    }
    context.set_compression_algorithm(GRPC_COMPRESS_GZIP);
    Status s = stub->Echo(&context, request, &response);
    EXPECT_EQ(response.message(), request.message());
    EXPECT_TRUE(s.ok());
  }
}

// This class is for testing scenarios where RPCs are cancelled on the server
// by calling ServerContext::TryCancel()
class End2endServerTryCancelTest : public End2endTest {
 protected:
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
  //   the messages from the client
  //
  // NOTE: Do not call this function with server_try_cancel == DO_NOT_CANCEL.
  void TestRequestStreamServerCancel(
      ServerTryCancelRequestPhase server_try_cancel, int num_msgs_to_send) {
    ResetStub();
    EchoRequest request;
    EchoResponse response;
    ClientContext context;

    // Send server_try_cancel value in the client metadata
    context.AddMetadata(kServerTryCancelRequest,
                        grpc::to_string(server_try_cancel));

    auto stream = stub_->RequestStream(&context, &response);

    int num_msgs_sent = 0;
    while (num_msgs_sent < num_msgs_to_send) {
      request.set_message("hello");
      if (!stream->Write(request)) {
        break;
      }
      num_msgs_sent++;
    }
    gpr_log(GPR_INFO, "Sent %d messages", num_msgs_sent);

    stream->WritesDone();
    Status s = stream->Finish();

    // At this point, we know for sure that RPC was cancelled by the server
    // since we passed server_try_cancel value in the metadata. Depending on the
    // value of server_try_cancel, the RPC might have been cancelled by the
    // server at different stages. The following validates our expectations of
    // number of messages sent in various cancellation scenarios:

    switch (server_try_cancel) {
      case CANCEL_BEFORE_PROCESSING:
      case CANCEL_DURING_PROCESSING:
        // If the RPC is cancelled by server before / during messages from the
        // client, it means that the client most likely did not get a chance to
        // send all the messages it wanted to send. i.e num_msgs_sent <=
        // num_msgs_to_send
        EXPECT_LE(num_msgs_sent, num_msgs_to_send);
        break;

      case CANCEL_AFTER_PROCESSING:
        // If the RPC was cancelled after all messages were read by the server,
        // the client did get a chance to send all its messages
        EXPECT_EQ(num_msgs_sent, num_msgs_to_send);
        break;

      default:
        gpr_log(GPR_ERROR, "Invalid server_try_cancel value: %d",
                server_try_cancel);
        EXPECT_TRUE(server_try_cancel > DO_NOT_CANCEL &&
                    server_try_cancel <= CANCEL_AFTER_PROCESSING);
        break;
    }

    EXPECT_FALSE(s.ok());
    EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
  }

  // Helper for testing server-streaming RPCs which are cancelled on the server.
  // Depending on the value of server_try_cancel parameter, this will test one
  // of the following three scenarios:
  //   CANCEL_BEFORE_PROCESSING: Rpc is cancelled by the server before writing
  //   any messages to the client
  //
  //   CANCEL_DURING_PROCESSING: Rpc is cancelled by the server while writing
  //   messages to the client
  //
  //   CANCEL_AFTER PROCESSING: Rpc is cancelled by server after writing all
  //   the messages to the client
  //
  // NOTE: Do not call this function with server_try_cancel == DO_NOT_CANCEL.
  void TestResponseStreamServerCancel(
      ServerTryCancelRequestPhase server_try_cancel) {
    ResetStub();
    EchoRequest request;
    EchoResponse response;
    ClientContext context;

    // Send server_try_cancel in the client metadata
    context.AddMetadata(kServerTryCancelRequest,
                        grpc::to_string(server_try_cancel));

    request.set_message("hello");
    auto stream = stub_->ResponseStream(&context, request);

    int num_msgs_read = 0;
    while (num_msgs_read < kServerDefaultResponseStreamsToSend) {
      if (!stream->Read(&response)) {
        break;
      }
      EXPECT_EQ(response.message(),
                request.message() + grpc::to_string(num_msgs_read));
      num_msgs_read++;
    }
    gpr_log(GPR_INFO, "Read %d messages", num_msgs_read);

    Status s = stream->Finish();

    // Depending on the value of server_try_cancel, the RPC might have been
    // cancelled by the server at different stages. The following validates our
    // expectations of number of messages read in various cancellation
    // scenarios:
    switch (server_try_cancel) {
      case CANCEL_BEFORE_PROCESSING:
        // Server cancelled before sending any messages. Which means the client
        // wouldn't have read any
        EXPECT_EQ(num_msgs_read, 0);
        break;

      case CANCEL_DURING_PROCESSING:
        // Server cancelled while writing messages. Client must have read less
        // than or equal to the expected number of messages
        EXPECT_LE(num_msgs_read, kServerDefaultResponseStreamsToSend);
        break;

      case CANCEL_AFTER_PROCESSING:
        // Even though the Server cancelled after writing all messages, the RPC
        // may be cancelled before the Client got a chance to read all the
        // messages.
        EXPECT_LE(num_msgs_read, kServerDefaultResponseStreamsToSend);
        break;

      default: {
        gpr_log(GPR_ERROR, "Invalid server_try_cancel value: %d",
                server_try_cancel);
        EXPECT_TRUE(server_try_cancel > DO_NOT_CANCEL &&
                    server_try_cancel <= CANCEL_AFTER_PROCESSING);
        break;
      }
    }

    EXPECT_FALSE(s.ok());
    EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
  }

  // Helper for testing bidirectional-streaming RPCs which are cancelled on the
  // server. Depending on the value of server_try_cancel parameter, this will
  // test one of the following three scenarios:
  //   CANCEL_BEFORE_PROCESSING: Rpc is cancelled by the server before reading/
  //   writing any messages from/to the client
  //
  //   CANCEL_DURING_PROCESSING: Rpc is cancelled by the server while reading/
  //   writing messages from/to the client
  //
  //   CANCEL_AFTER PROCESSING: Rpc is cancelled by server after reading/writing
  //   all the messages from/to the client
  //
  // NOTE: Do not call this function with server_try_cancel == DO_NOT_CANCEL.
  void TestBidiStreamServerCancel(ServerTryCancelRequestPhase server_try_cancel,
                                  int num_messages) {
    ResetStub();
    EchoRequest request;
    EchoResponse response;
    ClientContext context;

    // Send server_try_cancel in the client metadata
    context.AddMetadata(kServerTryCancelRequest,
                        grpc::to_string(server_try_cancel));

    auto stream = stub_->BidiStream(&context);

    int num_msgs_read = 0;
    int num_msgs_sent = 0;
    while (num_msgs_sent < num_messages) {
      request.set_message("hello " + grpc::to_string(num_msgs_sent));
      if (!stream->Write(request)) {
        break;
      }
      num_msgs_sent++;

      if (!stream->Read(&response)) {
        break;
      }
      num_msgs_read++;

      EXPECT_EQ(response.message(), request.message());
    }
    gpr_log(GPR_INFO, "Sent %d messages", num_msgs_sent);
    gpr_log(GPR_INFO, "Read %d messages", num_msgs_read);

    stream->WritesDone();
    Status s = stream->Finish();

    // Depending on the value of server_try_cancel, the RPC might have been
    // cancelled by the server at different stages. The following validates our
    // expectations of number of messages read in various cancellation
    // scenarios:
    switch (server_try_cancel) {
      case CANCEL_BEFORE_PROCESSING:
        EXPECT_EQ(num_msgs_read, 0);
        break;

      case CANCEL_DURING_PROCESSING:
        EXPECT_LE(num_msgs_sent, num_messages);
        EXPECT_LE(num_msgs_read, num_msgs_sent);
        break;

      case CANCEL_AFTER_PROCESSING:
        EXPECT_EQ(num_msgs_sent, num_messages);

        // The Server cancelled after reading the last message and after writing
        // the message to the client. However, the RPC cancellation might have
        // taken effect before the client actually read the response.
        EXPECT_LE(num_msgs_read, num_msgs_sent);
        break;

      default:
        gpr_log(GPR_ERROR, "Invalid server_try_cancel value: %d",
                server_try_cancel);
        EXPECT_TRUE(server_try_cancel > DO_NOT_CANCEL &&
                    server_try_cancel <= CANCEL_AFTER_PROCESSING);
        break;
    }

    EXPECT_FALSE(s.ok());
    EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
  }
};

TEST_P(End2endServerTryCancelTest, RequestEchoServerCancel) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  context.AddMetadata(kServerTryCancelRequest,
                      grpc::to_string(CANCEL_BEFORE_PROCESSING));
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
}

// Server to cancel before doing reading the request
TEST_P(End2endServerTryCancelTest, RequestStreamServerCancelBeforeReads) {
  TestRequestStreamServerCancel(CANCEL_BEFORE_PROCESSING, 1);
}

// Server to cancel while reading a request from the stream in parallel
TEST_P(End2endServerTryCancelTest, RequestStreamServerCancelDuringRead) {
  TestRequestStreamServerCancel(CANCEL_DURING_PROCESSING, 10);
}

// Server to cancel after reading all the requests but before returning to the
// client
TEST_P(End2endServerTryCancelTest, RequestStreamServerCancelAfterReads) {
  TestRequestStreamServerCancel(CANCEL_AFTER_PROCESSING, 4);
}

// Server to cancel before sending any response messages
TEST_P(End2endServerTryCancelTest, ResponseStreamServerCancelBefore) {
  TestResponseStreamServerCancel(CANCEL_BEFORE_PROCESSING);
}

// Server to cancel while writing a response to the stream in parallel
TEST_P(End2endServerTryCancelTest, ResponseStreamServerCancelDuring) {
  TestResponseStreamServerCancel(CANCEL_DURING_PROCESSING);
}

// Server to cancel after writing all the respones to the stream but before
// returning to the client
TEST_P(End2endServerTryCancelTest, ResponseStreamServerCancelAfter) {
  TestResponseStreamServerCancel(CANCEL_AFTER_PROCESSING);
}

// Server to cancel before reading/writing any requests/responses on the stream
TEST_P(End2endServerTryCancelTest, BidiStreamServerCancelBefore) {
  TestBidiStreamServerCancel(CANCEL_BEFORE_PROCESSING, 2);
}

// Server to cancel while reading/writing requests/responses on the stream in
// parallel
TEST_P(End2endServerTryCancelTest, BidiStreamServerCancelDuring) {
  TestBidiStreamServerCancel(CANCEL_DURING_PROCESSING, 10);
}

// Server to cancel after reading/writing all requests/responses on the stream
// but before returning to the client
TEST_P(End2endServerTryCancelTest, BidiStreamServerCancelAfter) {
  TestBidiStreamServerCancel(CANCEL_AFTER_PROCESSING, 5);
}

TEST_P(End2endTest, SimpleRpcWithCustomUserAgentPrefix) {
  // User-Agent is an HTTP header for HTTP transports only
  if (GetParam().inproc) {
    return;
  }
  user_agent_prefix_ = "custom_prefix";
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello hello hello hello");
  request.mutable_param()->set_echo_metadata(true);

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());
  const auto& trailing_metadata = context.GetServerTrailingMetadata();
  auto iter = trailing_metadata.find("user-agent");
  EXPECT_TRUE(iter != trailing_metadata.end());
  grpc::string expected_prefix = user_agent_prefix_ + " grpc-c++/";
  EXPECT_TRUE(iter->second.starts_with(expected_prefix)) << iter->second;
}

TEST_P(End2endTest, MultipleRpcsWithVariedBinaryMetadataValue) {
  ResetStub();
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back(SendRpc, stub_.get(), 10, true);
  }
  for (int i = 0; i < 10; ++i) {
    threads[i].join();
  }
}

TEST_P(End2endTest, MultipleRpcs) {
  ResetStub();
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back(SendRpc, stub_.get(), 10, false);
  }
  for (int i = 0; i < 10; ++i) {
    threads[i].join();
  }
}

TEST_P(End2endTest, ReconnectChannel) {
  if (GetParam().inproc) {
    return;
  }
  gpr_setenv("GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS", "200");
  int poller_slowdown_factor = 1;
  // It needs 2 pollset_works to reconnect the channel with polling engine
  // "poll"
  char* s = gpr_getenv("GRPC_POLL_STRATEGY");
  if (s != nullptr && 0 == strcmp(s, "poll")) {
    poller_slowdown_factor = 2;
  }
  gpr_free(s);
  ResetStub();
  SendRpc(stub_.get(), 1, false);
  RestartServer(std::shared_ptr<AuthMetadataProcessor>());
  // It needs more than GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS time to
  // reconnect the channel.
  gpr_sleep_until(gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME),
      gpr_time_from_millis(
          300 * poller_slowdown_factor * grpc_test_slowdown_factor(),
          GPR_TIMESPAN)));
  SendRpc(stub_.get(), 1, false);
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

TEST_P(End2endTest, RequestStreamOneRequestWithCoalescingApi) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  context.set_initial_metadata_corked(true);
  auto stream = stub_->RequestStream(&context, &response);
  request.set_message("hello");
  stream->WriteLast(request, WriteOptions());
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

TEST_P(End2endTest, RequestStreamTwoRequestsWithWriteThrough) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  auto stream = stub_->RequestStream(&context, &response);
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request, WriteOptions().set_write_through()));
  EXPECT_TRUE(stream->Write(request, WriteOptions().set_write_through()));
  stream->WritesDone();
  Status s = stream->Finish();
  EXPECT_EQ(response.message(), "hellohello");
  EXPECT_TRUE(s.ok());
}

TEST_P(End2endTest, RequestStreamTwoRequestsWithCoalescingApi) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  context.set_initial_metadata_corked(true);
  auto stream = stub_->RequestStream(&context, &response);
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  stream->WriteLast(request, WriteOptions());
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
  for (int i = 0; i < kServerDefaultResponseStreamsToSend; ++i) {
    EXPECT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(), request.message() + grpc::to_string(i));
  }
  EXPECT_FALSE(stream->Read(&response));

  Status s = stream->Finish();
  EXPECT_TRUE(s.ok());
}

TEST_P(End2endTest, ResponseStreamWithCoalescingApi) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");
  context.AddMetadata(kServerUseCoalescingApi, "1");

  auto stream = stub_->ResponseStream(&context, request);
  for (int i = 0; i < kServerDefaultResponseStreamsToSend; ++i) {
    EXPECT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(), request.message() + grpc::to_string(i));
  }
  EXPECT_FALSE(stream->Read(&response));

  Status s = stream->Finish();
  EXPECT_TRUE(s.ok());
}

// This was added to prevent regression from issue:
// https://github.com/grpc/grpc/issues/11546
TEST_P(End2endTest, ResponseStreamWithEverythingCoalesced) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");
  context.AddMetadata(kServerUseCoalescingApi, "1");
  // We will only send one message, forcing everything (init metadata, message,
  // trailing) to be coalesced together.
  context.AddMetadata(kServerResponseStreamsToSend, "1");

  auto stream = stub_->ResponseStream(&context, request);
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "0");

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

  for (int i = 0; i < kServerDefaultResponseStreamsToSend; ++i) {
    request.set_message(msg + grpc::to_string(i));
    EXPECT_TRUE(stream->Write(request));
    EXPECT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(), request.message());
  }

  stream->WritesDone();
  EXPECT_FALSE(stream->Read(&response));
  EXPECT_FALSE(stream->Read(&response));

  Status s = stream->Finish();
  EXPECT_TRUE(s.ok());
}

TEST_P(End2endTest, BidiStreamWithCoalescingApi) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.AddMetadata(kServerFinishAfterNReads, "3");
  context.set_initial_metadata_corked(true);
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
  stream->WriteLast(request, WriteOptions());
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message());

  EXPECT_FALSE(stream->Read(&response));
  EXPECT_FALSE(stream->Read(&response));

  Status s = stream->Finish();
  EXPECT_TRUE(s.ok());
}

// This was added to prevent regression from issue:
// https://github.com/grpc/grpc/issues/11546
TEST_P(End2endTest, BidiStreamWithEverythingCoalesced) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.AddMetadata(kServerFinishAfterNReads, "1");
  context.set_initial_metadata_corked(true);
  grpc::string msg("hello");

  auto stream = stub_->BidiStream(&context);

  request.set_message(msg + "0");
  stream->WriteLast(request, WriteOptions());
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message());

  EXPECT_FALSE(stream->Read(&response));
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

  std::unique_ptr<grpc::testing::duplicate::EchoTestService::Stub> dup_pkg_stub(
      grpc::testing::duplicate::EchoTestService::NewStub(channel_));
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
  request.mutable_param()->set_server_die(true);

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
  reader_thread.join();
  Status s = stream->Finish();
  EXPECT_TRUE(s.ok());
}

TEST_P(End2endTest, ChannelState) {
  if (GetParam().inproc) {
    return;
  }

  ResetStub();
  // Start IDLE
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(false));

  // Did not ask to connect, no state change.
  CompletionQueue cq;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::milliseconds(10);
  channel_->NotifyOnStateChange(GRPC_CHANNEL_IDLE, deadline, &cq, nullptr);
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
  if ((GetParam().credentials_type != kInsecureCredentialsType) ||
      GetParam().inproc) {
    return;
  }
  int port = grpc_pick_unused_port_or_die();
  std::ostringstream server_address;
  server_address << "127.0.0.1:" << port;
  // Channel to non-existing server
  auto channel =
      CreateChannel(server_address.str(), InsecureChannelCredentials());
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
  std::unique_ptr<grpc::testing::UnimplementedEchoService::Stub> stub;
  stub = grpc::testing::UnimplementedEchoService::NewStub(channel_);

  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  Status s = stub->Unimplemented(&context, request, &response);
  EXPECT_EQ(StatusCode::UNIMPLEMENTED, s.error_code());
  EXPECT_EQ("", s.error_message());
}

// Ask the server to send back a serialized proto in trailer.
// This is an example of setting error details.
TEST_P(End2endTest, BinaryTrailerTest) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  request.mutable_param()->set_echo_metadata(true);
  DebugInfo* info = request.mutable_param()->mutable_debug_info();
  info->add_stack_entries("stack_entry_1");
  info->add_stack_entries("stack_entry_2");
  info->add_stack_entries("stack_entry_3");
  info->set_detail("detailed debug info");
  grpc::string expected_string = info->SerializeAsString();
  request.set_message("Hello");

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
  auto trailers = context.GetServerTrailingMetadata();
  EXPECT_EQ(1u, trailers.count(kDebugInfoTrailerKey));
  auto iter = trailers.find(kDebugInfoTrailerKey);
  EXPECT_EQ(expected_string, iter->second);
  // Parse the returned trailer into a DebugInfo proto.
  DebugInfo returned_info;
  EXPECT_TRUE(returned_info.ParseFromString(ToString(iter->second)));
}

TEST_P(End2endTest, ExpectErrorTest) {
  ResetStub();

  std::vector<ErrorStatus> expected_status;
  expected_status.emplace_back();
  expected_status.back().set_code(13);  // INTERNAL
  expected_status.back().set_error_message("text error message");
  expected_status.back().set_binary_error_details("text error details");
  expected_status.emplace_back();
  expected_status.back().set_code(13);  // INTERNAL
  expected_status.back().set_error_message("text error message");
  expected_status.back().set_binary_error_details(
      "\x0\x1\x2\x3\x4\x5\x6\x8\x9\xA\xB");

  for (auto iter = expected_status.begin(); iter != expected_status.end();
       ++iter) {
    EchoRequest request;
    EchoResponse response;
    ClientContext context;
    request.set_message("Hello");
    auto* error = request.mutable_param()->mutable_expected_error();
    error->set_code(iter->code());
    error->set_error_message(iter->error_message());
    error->set_binary_error_details(iter->binary_error_details());

    Status s = stub_->Echo(&context, request, &response);
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(iter->code(), s.error_code());
    EXPECT_EQ(iter->error_message(), s.error_message());
    EXPECT_EQ(iter->binary_error_details(), s.error_details());
  }
}

//////////////////////////////////////////////////////////////////////////
// Test with and without a proxy.
class ProxyEnd2endTest : public End2endTest {
 protected:
};

TEST_P(ProxyEnd2endTest, SimpleRpc) {
  ResetStub();
  SendRpc(stub_.get(), 1, false);
}

TEST_P(ProxyEnd2endTest, SimpleRpcWithEmptyMessages) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(s.ok());
}

TEST_P(ProxyEnd2endTest, MultipleRpcs) {
  ResetStub();
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back(SendRpc, stub_.get(), 10, false);
  }
  for (int i = 0; i < 10; ++i) {
    threads[i].join();
  }
}

// Set a 10us deadline and make sure proper error is returned.
TEST_P(ProxyEnd2endTest, RpcDeadlineExpires) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_skip_cancelled_check(true);
  // Let server sleep for 2 ms first to guarantee expiry
  request.mutable_param()->set_server_sleep_us(2 * 1000);

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
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(20);
  context.set_deadline(deadline);
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(kResponseSize, response.message().size());
  EXPECT_TRUE(s.ok());
}

TEST_P(ProxyEnd2endTest, Peer) {
  // Peer is not meaningful for inproc
  if (GetParam().inproc) {
    return;
  }
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
    GPR_ASSERT(GetParam().credentials_type != kInsecureCredentialsType);
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
  request.mutable_param()->set_expected_transport_security_type(
      GetParam().credentials_type);

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
  std::shared_ptr<CallCredentials> creds =
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

TEST_P(SecureEnd2endTest, OverridePerCallCredentials) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  std::shared_ptr<CallCredentials> creds1 =
      GoogleIAMCredentials("fake_token1", "fake_selector1");
  context.set_credentials(creds1);
  std::shared_ptr<CallCredentials> creds2 =
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

TEST_P(SecureEnd2endTest, AuthMetadataPluginKeyFailure) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_credentials(
      MetadataCredentialsFromPlugin(std::unique_ptr<MetadataCredentialsPlugin>(
          new TestMetadataCredentialsPlugin(
              TestMetadataCredentialsPlugin::kBadMetadataKey,
              "Does not matter, will fail the key is invalid.", false, true))));
  request.set_message("Hello");

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.error_code(), StatusCode::UNAUTHENTICATED);
}

TEST_P(SecureEnd2endTest, AuthMetadataPluginValueFailure) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_credentials(
      MetadataCredentialsFromPlugin(std::unique_ptr<MetadataCredentialsPlugin>(
          new TestMetadataCredentialsPlugin(
              TestMetadataCredentialsPlugin::kGoodMetadataKey,
              "With illegal \n value.", false, true))));
  request.set_message("Hello");

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.error_code(), StatusCode::UNAUTHENTICATED);
}

TEST_P(SecureEnd2endTest, NonBlockingAuthMetadataPluginFailure) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_credentials(
      MetadataCredentialsFromPlugin(std::unique_ptr<MetadataCredentialsPlugin>(
          new TestMetadataCredentialsPlugin(
              TestMetadataCredentialsPlugin::kGoodMetadataKey,
              "Does not matter, will fail anyway (see 3rd param)", false,
              false))));
  request.set_message("Hello");

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.error_code(), StatusCode::UNAUTHENTICATED);
  EXPECT_EQ(s.error_message(),
            grpc::string("Getting metadata from plugin failed with error: ") +
                kTestCredsPluginErrorMsg);
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
  request.mutable_param()->set_expected_transport_security_type(
      GetParam().credentials_type);

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
              TestMetadataCredentialsPlugin::kGoodMetadataKey,
              "Does not matter, will fail anyway (see 3rd param)", true,
              false))));
  request.set_message("Hello");

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.error_code(), StatusCode::UNAUTHENTICATED);
  EXPECT_EQ(s.error_message(),
            grpc::string("Getting metadata from plugin failed with error: ") +
                kTestCredsPluginErrorMsg);
}

TEST_P(SecureEnd2endTest, CompositeCallCreds) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  const char kMetadataKey1[] = "call-creds-key1";
  const char kMetadataKey2[] = "call-creds-key2";
  const char kMetadataVal1[] = "call-creds-val1";
  const char kMetadataVal2[] = "call-creds-val2";

  context.set_credentials(CompositeCallCredentials(
      MetadataCredentialsFromPlugin(std::unique_ptr<MetadataCredentialsPlugin>(
          new TestMetadataCredentialsPlugin(kMetadataKey1, kMetadataVal1, true,
                                            true))),
      MetadataCredentialsFromPlugin(std::unique_ptr<MetadataCredentialsPlugin>(
          new TestMetadataCredentialsPlugin(kMetadataKey2, kMetadataVal2, true,
                                            true)))));
  request.set_message("Hello");
  request.mutable_param()->set_echo_metadata(true);

  Status s = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(s.ok());
  EXPECT_TRUE(MetadataContains(context.GetServerTrailingMetadata(),
                               kMetadataKey1, kMetadataVal1));
  EXPECT_TRUE(MetadataContains(context.GetServerTrailingMetadata(),
                               kMetadataKey2, kMetadataVal2));
}

TEST_P(SecureEnd2endTest, ClientAuthContext) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_check_auth_context(GetParam().credentials_type ==
                                                  kTlsCredentialsType);
  request.mutable_param()->set_expected_transport_security_type(
      GetParam().credentials_type);
  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());

  std::shared_ptr<const AuthContext> auth_ctx = context.auth_context();
  std::vector<grpc::string_ref> tst =
      auth_ctx->FindPropertyValues("transport_security_type");
  ASSERT_EQ(1u, tst.size());
  EXPECT_EQ(GetParam().credentials_type, ToString(tst[0]));
  if (GetParam().credentials_type == kTlsCredentialsType) {
    EXPECT_EQ("x509_subject_alternative_name",
              auth_ctx->GetPeerIdentityPropertyName());
    EXPECT_EQ(4u, auth_ctx->GetPeerIdentity().size());
    EXPECT_EQ("*.test.google.fr", ToString(auth_ctx->GetPeerIdentity()[0]));
    EXPECT_EQ("waterzooi.test.google.be",
              ToString(auth_ctx->GetPeerIdentity()[1]));
    EXPECT_EQ("*.test.youtube.com", ToString(auth_ctx->GetPeerIdentity()[2]));
    EXPECT_EQ("192.168.1.3", ToString(auth_ctx->GetPeerIdentity()[3]));
  }
}

class ResourceQuotaEnd2endTest : public End2endTest {
 public:
  ResourceQuotaEnd2endTest()
      : server_resource_quota_("server_resource_quota") {}

  virtual void ConfigureServerBuilder(ServerBuilder* builder) override {
    builder->SetResourceQuota(server_resource_quota_);
  }

 private:
  ResourceQuota server_resource_quota_;
};

TEST_P(ResourceQuotaEnd2endTest, SimpleRequest) {
  ResetStub();

  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());
}

std::vector<TestScenario> CreateTestScenarios(bool use_proxy,
                                              bool test_insecure,
                                              bool test_secure,
                                              bool test_inproc) {
  std::vector<TestScenario> scenarios;
  std::vector<grpc::string> credentials_types;
  if (test_secure) {
    credentials_types =
        GetCredentialsProvider()->GetSecureCredentialsTypeList();
  }
  auto insec_ok = [] {
    // Only allow insecure credentials type when it is registered with the
    // provider. User may create providers that do not have insecure.
    return GetCredentialsProvider()->GetChannelCredentials(
               kInsecureCredentialsType, nullptr) != nullptr;
  };
  if (test_insecure && insec_ok()) {
    credentials_types.push_back(kInsecureCredentialsType);
  }
  GPR_ASSERT(!credentials_types.empty());
  for (const auto& cred : credentials_types) {
    scenarios.emplace_back(false, false, cred);
    if (use_proxy) {
      scenarios.emplace_back(true, false, cred);
    }
  }
  if (test_inproc && insec_ok()) {
    scenarios.emplace_back(false, true, kInsecureCredentialsType);
  }
  return scenarios;
}

INSTANTIATE_TEST_CASE_P(End2end, End2endTest,
                        ::testing::ValuesIn(CreateTestScenarios(false, true,
                                                                true, true)));

INSTANTIATE_TEST_CASE_P(End2endServerTryCancel, End2endServerTryCancelTest,
                        ::testing::ValuesIn(CreateTestScenarios(false, true,
                                                                true, true)));

INSTANTIATE_TEST_CASE_P(ProxyEnd2end, ProxyEnd2endTest,
                        ::testing::ValuesIn(CreateTestScenarios(true, true,
                                                                true, true)));

INSTANTIATE_TEST_CASE_P(SecureEnd2end, SecureEnd2endTest,
                        ::testing::ValuesIn(CreateTestScenarios(false, false,
                                                                true, false)));

INSTANTIATE_TEST_CASE_P(ResourceQuotaEnd2end, ResourceQuotaEnd2endTest,
                        ::testing::ValuesIn(CreateTestScenarios(false, true,
                                                                true, true)));

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
