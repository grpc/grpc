//
//
// Copyright 2018 gRPC authors.
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
#include <grpc/grpc_security.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/channelz_service_plugin.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <memory>

#include "src/core/credentials/transport/tls/grpc_tls_certificate_provider.h"
#include "src/core/credentials/transport/tls/ssl_utils.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/env.h"
#include "src/core/util/wait_for_single_owner.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "src/proto/grpc/channelz/v2/service.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/end2end_test_utils.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/test_credentials_provider.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/memory/memory.h"

using grpc::channelz::v2::GetEntityRequest;
using grpc::channelz::v2::GetEntityResponse;
using grpc::channelz::v2::QueryEntitiesRequest;
using grpc::channelz::v2::QueryEntitiesResponse;
using grpc_core::testing::GetFileContents;

namespace grpc {
namespace testing {
namespace {

// Proxy service supports N backends. Sends RPC to backend dictated by
// request->backend_channel_idx().
class Proxy : public grpc::testing::EchoTestService::Service {
 public:
  Proxy() {}

  void AddChannelToBackend(const std::shared_ptr<Channel>& channel) {
    stubs_.push_back(grpc::testing::EchoTestService::NewStub(channel));
    channels_.push_back(channel);
  }

  Status Echo(ServerContext* server_context, const EchoRequest* request,
              EchoResponse* response) override {
    std::unique_ptr<ClientContext> client_context =
        ClientContext::FromServerContext(*server_context);
    size_t idx = request->param().backend_channel_idx();
    CHECK_LT(idx, stubs_.size());
    return stubs_[idx]->Echo(client_context.get(), *request, response);
  }

  Status BidiStream(ServerContext* server_context,
                    ServerReaderWriter<EchoResponse, EchoRequest>*
                        stream_from_client) override {
    EchoRequest request;
    EchoResponse response;
    std::unique_ptr<ClientContext> client_context =
        ClientContext::FromServerContext(*server_context);

    // always use the first proxy for streaming
    auto stream_to_backend = stubs_[0]->BidiStream(client_context.get());
    while (stream_from_client->Read(&request)) {
      stream_to_backend->Write(request);
      stream_to_backend->Read(&response);
      stream_from_client->Write(response);
    }

    stream_to_backend->WritesDone();
    return stream_to_backend->Finish();
  }

  std::shared_ptr<Channel> channel(int i) {
    if (i < 0 || i >= channels_.size()) return nullptr;
    return channels_[i];
  }

 private:
  std::vector<std::unique_ptr<grpc::testing::EchoTestService::Stub>> stubs_;
  std::vector<std::shared_ptr<Channel>> channels_;
};

enum class CredentialsType {
  kInsecure = 0,
  kTls = 1,
  kMtls = 2,
};

constexpr char kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
constexpr char kServerCertPath[] = "src/core/tsi/test_creds/server1.pem";
constexpr char kServerKeyPath[] = "src/core/tsi/test_creds/server1.key";
constexpr char kClientCertPath[] = "src/core/tsi/test_creds/client.pem";
constexpr char kClientKeyPath[] = "src/core/tsi/test_creds/client.key";

std::shared_ptr<grpc::ChannelCredentials> GetChannelCredentials(
    CredentialsType type, ChannelArguments* args) {
  if (type == CredentialsType::kInsecure) {
    return InsecureChannelCredentials();
  }
  args->SetSslTargetNameOverride("foo.test.google.fr");
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs = {
      {GetFileContents(kClientKeyPath), GetFileContents(kClientCertPath)}};
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(
      std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
          GetFileContents(kCaCertPath), identity_key_cert_pairs));
  if (type == CredentialsType::kMtls) {
    options.watch_identity_key_cert_pairs();
  }
  options.watch_root_certs();
  return grpc::experimental::TlsCredentials(options);
}

std::shared_ptr<grpc::ServerCredentials> GetServerCredentials(
    CredentialsType type) {
  if (type == CredentialsType::kInsecure) {
    return InsecureServerCredentials();
  }
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs = {
      {GetFileContents(kServerKeyPath), GetFileContents(kServerCertPath)}};
  auto certificate_provider =
      std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
          GetFileContents(kCaCertPath), identity_key_cert_pairs);
  grpc::experimental::TlsServerCredentialsOptions options(certificate_provider);
  options.watch_root_certs();
  options.watch_identity_key_cert_pairs();
  options.set_cert_request_type(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
  return grpc::experimental::TlsServerCredentials(options);
}

std::string RemoveWhitespaces(std::string input) {
  input.erase(remove_if(input.begin(), input.end(), isspace), input.end());
  return input;
}

class ChannelzServerTest : public ::testing::TestWithParam<CredentialsType> {
 public:
  ChannelzServerTest() {}
  static void SetUpTestSuite() {}
  void SetUp() override {
    grpc_init();

    // ensure channel server is brought up on all severs we build.
    grpc::channelz::experimental::InitChannelzService();

    // We set up a proxy server with channelz enabled.
    proxy_port_ = grpc_pick_unused_port_or_die();
    ServerBuilder proxy_builder;
    std::string proxy_server_address = grpc_core::LocalIpAndPort(proxy_port_);
    proxy_builder.AddListeningPort(proxy_server_address,
                                   GetServerCredentials(GetParam()));
    // forces channelz and channel tracing to be enabled.
    proxy_builder.AddChannelArgument(GRPC_ARG_ENABLE_CHANNELZ, 1);
    proxy_builder.AddChannelArgument(
        GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE, 1024);
    proxy_service_ = std::make_unique<Proxy>();
    proxy_builder.RegisterService(proxy_service_.get());
    proxy_server_ = proxy_builder.BuildAndStart();
  }

  void TearDown() override {
    for (auto& backend : backends_) {
      backend.server->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
    }
    proxy_server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
    grpc_shutdown();
    proxy_server_.reset();
    echo_stub_.reset();
    channelz_channel_.reset();
    channelz_stub_.reset();
    backends_.clear();
    proxy_service_.reset();
    // Ensure all pending callbacks are handled before finishing the test
    // to ensure hygiene between test cases.
    // (requires any grpc-object-holding values be cleared out first).
    grpc_core::WaitForSingleOwner(
        grpc_event_engine::experimental::GetDefaultEventEngine());
  }

  // Sets the proxy up to have an arbitrary number of backends.
  void ConfigureProxy(size_t num_backends) {
    backends_.resize(num_backends);
    for (size_t i = 0; i < num_backends; ++i) {
      // create a new backend.
      backends_[i].port = grpc_pick_unused_port_or_die();
      ServerBuilder backend_builder;
      std::string backend_server_address =
          grpc_core::LocalIpAndPort(backends_[i].port);
      backend_builder.AddListeningPort(backend_server_address,
                                       GetServerCredentials(GetParam()));
      backends_[i].service = std::make_unique<TestServiceImpl>();
      // ensure that the backend itself has channelz disabled.
      backend_builder.AddChannelArgument(GRPC_ARG_ENABLE_CHANNELZ, 0);
      backend_builder.RegisterService(backends_[i].service.get());
      backends_[i].server = backend_builder.BuildAndStart();
      // set up a channel to the backend. We ensure that this channel has
      // channelz enabled since these channels (proxy outbound to backends)
      // are the ones that our test will actually be validating.
      ChannelArguments args;
      ApplyCommonChannelArguments(args);
      args.SetInt(GRPC_ARG_ENABLE_CHANNELZ, 1);
      args.SetInt(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE, 1024);
      std::shared_ptr<Channel> channel_to_backend = grpc::CreateCustomChannel(
          backend_server_address, GetChannelCredentials(GetParam(), &args),
          args);
      proxy_service_->AddChannelToBackend(channel_to_backend);
    }
  }

  void ResetStubs() {
    string target =
        absl::StrCat("dns:", grpc_core::LocalIp(), ":", proxy_port_);
    ChannelArguments args;
    ApplyCommonChannelArguments(args);
    // disable channelz. We only want to focus on proxy to backend outbound.
    args.SetInt(GRPC_ARG_ENABLE_CHANNELZ, 0);
    channelz_channel_ = grpc::CreateCustomChannel(
        target, GetChannelCredentials(GetParam(), &args), args);
    channelz_stub_ = grpc::channelz::v2::Channelz::NewStub(channelz_channel_);
    echo_stub_ = grpc::testing::EchoTestService::NewStub(channelz_channel_);
  }

  std::unique_ptr<grpc::testing::EchoTestService::Stub> NewEchoStub() {
    string target =
        absl::StrCat("dns:", grpc_core::LocalIp(), ":", proxy_port_);
    ChannelArguments args;
    ApplyCommonChannelArguments(args);
    // disable channelz. We only want to focus on proxy to backend outbound.
    args.SetInt(GRPC_ARG_ENABLE_CHANNELZ, 0);
    // This ensures that gRPC will not do connection sharing.
    args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, true);
    std::shared_ptr<Channel> channel = grpc::CreateCustomChannel(
        target, GetChannelCredentials(GetParam(), &args), args);
    return grpc::testing::EchoTestService::NewStub(channel);
  }

  void SendSuccessfulEcho(int channel_idx) {
    EchoRequest request;
    EchoResponse response;
    request.set_message("Hello channelz");
    request.mutable_param()->set_backend_channel_idx(channel_idx);
    ClientContext context;
    Status s = echo_stub_->Echo(&context, request, &response);
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
    if (s.ok()) EXPECT_EQ(response.message(), request.message());
  }

  void SendSuccessfulStream(int num_messages) {
    EchoRequest request;
    EchoResponse response;
    request.set_message("Hello channelz");
    ClientContext context;
    auto stream_to_proxy = echo_stub_->BidiStream(&context);
    for (int i = 0; i < num_messages; ++i) {
      EXPECT_TRUE(stream_to_proxy->Write(request));
      EXPECT_TRUE(stream_to_proxy->Read(&response));
    }
    stream_to_proxy->WritesDone();
    Status s = stream_to_proxy->Finish();
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  }

  void SendFailedEcho(int channel_idx) {
    EchoRequest request;
    EchoResponse response;
    request.set_message("Hello channelz");
    request.mutable_param()->set_backend_channel_idx(channel_idx);
    auto* error = request.mutable_param()->mutable_expected_error();
    error->set_code(13);  // INTERNAL
    error->set_error_message("error");
    ClientContext context;
    Status s = echo_stub_->Echo(&context, request, &response);
    EXPECT_FALSE(s.ok());
  }

  static string to_string(const int number) {
    std::stringstream strs;
    strs << number;
    return strs.str();
  }

 protected:
  // package of data needed for each backend server.
  struct BackendData {
    std::unique_ptr<Server> server;
    int port;
    std::unique_ptr<TestServiceImpl> service;
  };

  std::shared_ptr<Channel> channelz_channel_;
  std::unique_ptr<grpc::channelz::v2::Channelz::Stub> channelz_stub_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> echo_stub_;

  // proxy server to ping with channelz requests.
  std::unique_ptr<Server> proxy_server_;
  int proxy_port_;
  std::unique_ptr<Proxy> proxy_service_;

  // backends. All implement the echo service.
  std::vector<BackendData> backends_;
};

TEST_P(ChannelzServerTest, BasicTest) {
  ResetStubs();
  ConfigureProxy(1);
  QueryEntitiesRequest request;
  QueryEntitiesResponse response;
  request.set_kind("channel");
  ClientContext context;
  Status s = channelz_stub_->QueryEntities(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.entities_size(), 1);
}

TEST_P(ChannelzServerTest, NamedChannelTest) {
  ResetStubs();
  ConfigureProxy(1);
  // Channel created without channelz
  EXPECT_EQ(experimental::ChannelGetChannelzUuid(channelz_channel_.get()), 0);
  int64_t proxy_uuid =
      experimental::ChannelGetChannelzUuid(proxy_service_->channel(0).get());
  ASSERT_NE(proxy_uuid, 0);
  GetEntityRequest request;
  GetEntityResponse response;
  request.set_id(proxy_uuid);
  ClientContext context;
  Status s = channelz_stub_->GetEntity(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.entity().kind(), "channel");
}

TEST_P(ChannelzServerTest, HighStartId) {
  ResetStubs();
  ConfigureProxy(1);
  QueryEntitiesRequest request;
  QueryEntitiesResponse response;
  request.set_kind("channel");
  request.set_start_entity_id(10000);
  ClientContext context;
  Status s = channelz_stub_->QueryEntities(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.entities_size(), 0);
}

TEST_P(ChannelzServerTest, ManyChannels) {
  ResetStubs();
  const int kNumChannels = 4;
  ConfigureProxy(kNumChannels);
  QueryEntitiesRequest request;
  QueryEntitiesResponse response;
  request.set_kind("channel");
  ClientContext context;
  Status s = channelz_stub_->QueryEntities(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.entities_size(), kNumChannels);
}

TEST_P(ChannelzServerTest, BasicServerTest) {
  ResetStubs();
  ConfigureProxy(1);
  QueryEntitiesRequest request;
  QueryEntitiesResponse response;
  request.set_kind("server");
  ClientContext context;
  Status s = channelz_stub_->QueryEntities(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.entities_size(), 1);
}

TEST_P(ChannelzServerTest, BasicGetServerTest) {
  ResetStubs();
  ConfigureProxy(1);
  QueryEntitiesRequest get_servers_request;
  QueryEntitiesResponse get_servers_response;
  get_servers_request.set_kind("server");
  ClientContext get_servers_context;
  Status s = channelz_stub_->QueryEntities(
      &get_servers_context, get_servers_request, &get_servers_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_servers_response.entities_size(), 1);
  GetEntityRequest get_server_request;
  GetEntityResponse get_server_response;
  get_server_request.set_id(get_servers_response.entities(0).id());
  ClientContext get_server_context;
  s = channelz_stub_->GetEntity(&get_server_context, get_server_request,
                                &get_server_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_servers_response.entities(0).id(),
            get_server_response.entity().id());
}

INSTANTIATE_TEST_SUITE_P(ChannelzServer, ChannelzServerTest,
                         ::testing::ValuesIn(std::vector<CredentialsType>(
                             {CredentialsType::kInsecure, CredentialsType::kTls,
                              CredentialsType::kMtls})));

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
