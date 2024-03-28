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

#include <grpc/support/port_platform.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/memory/memory.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/channelz_service_plugin.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/channelz/channelz.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/util/port.h"
#include "test/core/util/resolve_localhost_ip46.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/test_credentials_provider.h"

using grpc::channelz::v1::Address;
using grpc::channelz::v1::GetChannelRequest;
using grpc::channelz::v1::GetChannelResponse;
using grpc::channelz::v1::GetServerRequest;
using grpc::channelz::v1::GetServerResponse;
using grpc::channelz::v1::GetServerSocketsRequest;
using grpc::channelz::v1::GetServerSocketsResponse;
using grpc::channelz::v1::GetServersRequest;
using grpc::channelz::v1::GetServersResponse;
using grpc::channelz::v1::GetSocketRequest;
using grpc::channelz::v1::GetSocketResponse;
using grpc::channelz::v1::GetSubchannelRequest;
using grpc::channelz::v1::GetSubchannelResponse;
using grpc::channelz::v1::GetTopChannelsRequest;
using grpc::channelz::v1::GetTopChannelsResponse;
using grpc_core::testing::GetFileContents;

namespace grpc {
namespace testing {
namespace {

bool ValidateAddress(const Address& address) {
  if (address.address_case() != Address::kTcpipAddress) {
    return true;
  }
  return address.tcpip_address().ip_address().size() == 4 ||
         address.tcpip_address().ip_address().size() == 16;
}

// Proxy service supports N backends. Sends RPC to backend dictated by
// request->backend_channel_idx().
class Proxy : public grpc::testing::EchoTestService::Service {
 public:
  Proxy() {}

  void AddChannelToBackend(const std::shared_ptr<Channel>& channel) {
    stubs_.push_back(grpc::testing::EchoTestService::NewStub(channel));
  }

  Status Echo(ServerContext* server_context, const EchoRequest* request,
              EchoResponse* response) override {
    std::unique_ptr<ClientContext> client_context =
        ClientContext::FromServerContext(*server_context);
    size_t idx = request->param().backend_channel_idx();
    GPR_ASSERT(idx < stubs_.size());
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

 private:
  std::vector<std::unique_ptr<grpc::testing::EchoTestService::Stub>> stubs_;
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
  static void SetUpTestSuite() {
#if TARGET_OS_IPHONE
    // Workaround Apple CFStream bug
    grpc_core::SetEnv("grpc_cfstream", "0");
#endif
  }
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
    channelz_stub_.reset();
    backends_.clear();
    proxy_service_.reset();
    // Ensure all pending callbacks are handled before finishing the test
    // to ensure hygene between test cases.
    // (requires any grpc-object-holding values be cleared out first).
    grpc_event_engine::experimental::WaitForSingleOwner(
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
    // disable channelz. We only want to focus on proxy to backend outbound.
    args.SetInt(GRPC_ARG_ENABLE_CHANNELZ, 0);
    std::shared_ptr<Channel> channel = grpc::CreateCustomChannel(
        target, GetChannelCredentials(GetParam(), &args), args);
    channelz_stub_ = grpc::channelz::v1::Channelz::NewStub(channel);
    echo_stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

  std::unique_ptr<grpc::testing::EchoTestService::Stub> NewEchoStub() {
    string target =
        absl::StrCat("dns:", grpc_core::LocalIp(), ":", proxy_port_);
    ChannelArguments args;
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

  // Uses GetTopChannels to return the channel_id of a particular channel,
  // so that the unit tests may test GetChannel call.
  intptr_t GetChannelId(int channel_idx) {
    GetTopChannelsRequest request;
    GetTopChannelsResponse response;
    request.set_start_channel_id(0);
    ClientContext context;
    Status s = channelz_stub_->GetTopChannels(&context, request, &response);
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
    EXPECT_GT(response.channel_size(), channel_idx);
    return response.channel(channel_idx).ref().channel_id();
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

  std::unique_ptr<grpc::channelz::v1::Channelz::Stub> channelz_stub_;
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
  GetTopChannelsRequest request;
  GetTopChannelsResponse response;
  request.set_start_channel_id(0);
  ClientContext context;
  Status s = channelz_stub_->GetTopChannels(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.channel_size(), 1);
}

TEST_P(ChannelzServerTest, HighStartId) {
  ResetStubs();
  ConfigureProxy(1);
  GetTopChannelsRequest request;
  GetTopChannelsResponse response;
  request.set_start_channel_id(10000);
  ClientContext context;
  Status s = channelz_stub_->GetTopChannels(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.channel_size(), 0);
}

TEST_P(ChannelzServerTest, SuccessfulRequestTest) {
  ResetStubs();
  ConfigureProxy(1);
  SendSuccessfulEcho(0);
  GetChannelRequest request;
  GetChannelResponse response;
  request.set_channel_id(GetChannelId(0));
  ClientContext context;
  Status s = channelz_stub_->GetChannel(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.channel().data().calls_started(), 1);
  EXPECT_EQ(response.channel().data().calls_succeeded(), 1);
  EXPECT_EQ(response.channel().data().calls_failed(), 0);
}

TEST_P(ChannelzServerTest, FailedRequestTest) {
  ResetStubs();
  ConfigureProxy(1);
  SendFailedEcho(0);
  GetChannelRequest request;
  GetChannelResponse response;
  request.set_channel_id(GetChannelId(0));
  ClientContext context;
  Status s = channelz_stub_->GetChannel(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.channel().data().calls_started(), 1);
  EXPECT_EQ(response.channel().data().calls_succeeded(), 0);
  EXPECT_EQ(response.channel().data().calls_failed(), 1);
}

TEST_P(ChannelzServerTest, ManyRequestsTest) {
  ResetStubs();
  ConfigureProxy(1);
  // send some RPCs
  const int kNumSuccess = 10;
  const int kNumFailed = 11;
  for (int i = 0; i < kNumSuccess; ++i) {
    SendSuccessfulEcho(0);
  }
  for (int i = 0; i < kNumFailed; ++i) {
    SendFailedEcho(0);
  }
  GetChannelRequest request;
  GetChannelResponse response;
  request.set_channel_id(GetChannelId(0));
  ClientContext context;
  Status s = channelz_stub_->GetChannel(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.channel().data().calls_started(),
            kNumSuccess + kNumFailed);
  EXPECT_EQ(response.channel().data().calls_succeeded(), kNumSuccess);
  EXPECT_EQ(response.channel().data().calls_failed(), kNumFailed);
}

TEST_P(ChannelzServerTest, ManyChannels) {
  ResetStubs();
  const int kNumChannels = 4;
  ConfigureProxy(kNumChannels);
  GetTopChannelsRequest request;
  GetTopChannelsResponse response;
  request.set_start_channel_id(0);
  ClientContext context;
  Status s = channelz_stub_->GetTopChannels(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.channel_size(), kNumChannels);
}

TEST_P(ChannelzServerTest, ManyRequestsManyChannels) {
  ResetStubs();
  const int kNumChannels = 4;
  ConfigureProxy(kNumChannels);
  const int kNumSuccess = 10;
  const int kNumFailed = 11;
  for (int i = 0; i < kNumSuccess; ++i) {
    SendSuccessfulEcho(0);
    SendSuccessfulEcho(2);
  }
  for (int i = 0; i < kNumFailed; ++i) {
    SendFailedEcho(1);
    SendFailedEcho(2);
  }

  // the first channel saw only successes
  {
    GetChannelRequest request;
    GetChannelResponse response;
    request.set_channel_id(GetChannelId(0));
    ClientContext context;
    Status s = channelz_stub_->GetChannel(&context, request, &response);
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
    EXPECT_EQ(response.channel().data().calls_started(), kNumSuccess);
    EXPECT_EQ(response.channel().data().calls_succeeded(), kNumSuccess);
    EXPECT_EQ(response.channel().data().calls_failed(), 0);
  }

  // the second channel saw only failures
  {
    GetChannelRequest request;
    GetChannelResponse response;
    request.set_channel_id(GetChannelId(1));
    ClientContext context;
    Status s = channelz_stub_->GetChannel(&context, request, &response);
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
    EXPECT_EQ(response.channel().data().calls_started(), kNumFailed);
    EXPECT_EQ(response.channel().data().calls_succeeded(), 0);
    EXPECT_EQ(response.channel().data().calls_failed(), kNumFailed);
  }

  // the third channel saw both
  {
    GetChannelRequest request;
    GetChannelResponse response;
    request.set_channel_id(GetChannelId(2));
    ClientContext context;
    Status s = channelz_stub_->GetChannel(&context, request, &response);
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
    EXPECT_EQ(response.channel().data().calls_started(),
              kNumSuccess + kNumFailed);
    EXPECT_EQ(response.channel().data().calls_succeeded(), kNumSuccess);
    EXPECT_EQ(response.channel().data().calls_failed(), kNumFailed);
  }

  // the fourth channel saw nothing
  {
    GetChannelRequest request;
    GetChannelResponse response;
    request.set_channel_id(GetChannelId(3));
    ClientContext context;
    Status s = channelz_stub_->GetChannel(&context, request, &response);
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
    EXPECT_EQ(response.channel().data().calls_started(), 0);
    EXPECT_EQ(response.channel().data().calls_succeeded(), 0);
    EXPECT_EQ(response.channel().data().calls_failed(), 0);
  }
}

TEST_P(ChannelzServerTest, ManySubchannels) {
  ResetStubs();
  const int kNumChannels = 4;
  ConfigureProxy(kNumChannels);
  const int kNumSuccess = 10;
  const int kNumFailed = 11;
  for (int i = 0; i < kNumSuccess; ++i) {
    SendSuccessfulEcho(0);
    SendSuccessfulEcho(2);
  }
  for (int i = 0; i < kNumFailed; ++i) {
    SendFailedEcho(1);
    SendFailedEcho(2);
  }
  GetTopChannelsRequest gtc_request;
  GetTopChannelsResponse gtc_response;
  gtc_request.set_start_channel_id(0);
  ClientContext context;
  Status s =
      channelz_stub_->GetTopChannels(&context, gtc_request, &gtc_response);
  EXPECT_TRUE(s.ok()) << s.error_message();
  EXPECT_EQ(gtc_response.channel_size(), kNumChannels);
  for (int i = 0; i < gtc_response.channel_size(); ++i) {
    // if the channel sent no RPCs, then expect no subchannels to have been
    // created.
    if (gtc_response.channel(i).data().calls_started() == 0) {
      EXPECT_EQ(gtc_response.channel(i).subchannel_ref_size(), 0);
      continue;
    }
    // The resolver must return at least one address.
    ASSERT_GT(gtc_response.channel(i).subchannel_ref_size(), 0);
    GetSubchannelRequest gsc_request;
    GetSubchannelResponse gsc_response;
    gsc_request.set_subchannel_id(
        gtc_response.channel(i).subchannel_ref(0).subchannel_id());
    ClientContext context;
    Status s =
        channelz_stub_->GetSubchannel(&context, gsc_request, &gsc_response);
    EXPECT_TRUE(s.ok()) << s.error_message();
    EXPECT_EQ(gtc_response.channel(i).data().calls_started(),
              gsc_response.subchannel().data().calls_started());
    EXPECT_EQ(gtc_response.channel(i).data().calls_succeeded(),
              gsc_response.subchannel().data().calls_succeeded());
    EXPECT_EQ(gtc_response.channel(i).data().calls_failed(),
              gsc_response.subchannel().data().calls_failed());
  }
}

TEST_P(ChannelzServerTest, BasicServerTest) {
  ResetStubs();
  ConfigureProxy(1);
  GetServersRequest request;
  GetServersResponse response;
  request.set_start_server_id(0);
  ClientContext context;
  Status s = channelz_stub_->GetServers(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.server_size(), 1);
}

TEST_P(ChannelzServerTest, BasicGetServerTest) {
  ResetStubs();
  ConfigureProxy(1);
  GetServersRequest get_servers_request;
  GetServersResponse get_servers_response;
  get_servers_request.set_start_server_id(0);
  ClientContext get_servers_context;
  Status s = channelz_stub_->GetServers(
      &get_servers_context, get_servers_request, &get_servers_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_servers_response.server_size(), 1);
  GetServerRequest get_server_request;
  GetServerResponse get_server_response;
  get_server_request.set_server_id(
      get_servers_response.server(0).ref().server_id());
  ClientContext get_server_context;
  s = channelz_stub_->GetServer(&get_server_context, get_server_request,
                                &get_server_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_servers_response.server(0).ref().server_id(),
            get_server_response.server().ref().server_id());
}

TEST_P(ChannelzServerTest, ServerCallTest) {
  ResetStubs();
  ConfigureProxy(1);
  const int kNumSuccess = 10;
  const int kNumFailed = 11;
  for (int i = 0; i < kNumSuccess; ++i) {
    SendSuccessfulEcho(0);
  }
  for (int i = 0; i < kNumFailed; ++i) {
    SendFailedEcho(0);
  }
  GetServersRequest request;
  GetServersResponse response;
  request.set_start_server_id(0);
  ClientContext context;
  Status s = channelz_stub_->GetServers(&context, request, &response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(response.server_size(), 1);
  EXPECT_EQ(response.server(0).data().calls_succeeded(), kNumSuccess);
  EXPECT_EQ(response.server(0).data().calls_failed(), kNumFailed);
  // This is success+failure+1 because the call that retrieved this information
  // will be counted as started. It will not track success/failure until after
  // it has returned, so that is not included in the response.
  EXPECT_EQ(response.server(0).data().calls_started(),
            kNumSuccess + kNumFailed + 1);
}

TEST_P(ChannelzServerTest, ManySubchannelsAndSockets) {
  ResetStubs();
  const int kNumChannels = 4;
  ConfigureProxy(kNumChannels);
  const int kNumSuccess = 10;
  const int kNumFailed = 11;
  for (int i = 0; i < kNumSuccess; ++i) {
    SendSuccessfulEcho(0);
    SendSuccessfulEcho(2);
  }
  for (int i = 0; i < kNumFailed; ++i) {
    SendFailedEcho(1);
    SendFailedEcho(2);
  }
  GetTopChannelsRequest gtc_request;
  GetTopChannelsResponse gtc_response;
  gtc_request.set_start_channel_id(0);
  ClientContext context;
  Status s =
      channelz_stub_->GetTopChannels(&context, gtc_request, &gtc_response);
  EXPECT_TRUE(s.ok()) << s.error_message();
  EXPECT_EQ(gtc_response.channel_size(), kNumChannels);
  for (int i = 0; i < gtc_response.channel_size(); ++i) {
    // if the channel sent no RPCs, then expect no subchannels to have been
    // created.
    if (gtc_response.channel(i).data().calls_started() == 0) {
      EXPECT_EQ(gtc_response.channel(i).subchannel_ref_size(), 0);
      continue;
    }
    // The resolver must return at least one address.
    ASSERT_GT(gtc_response.channel(i).subchannel_ref_size(), 0);
    // First grab the subchannel
    GetSubchannelRequest get_subchannel_req;
    GetSubchannelResponse get_subchannel_resp;
    get_subchannel_req.set_subchannel_id(
        gtc_response.channel(i).subchannel_ref(0).subchannel_id());
    ClientContext get_subchannel_ctx;
    Status s = channelz_stub_->GetSubchannel(
        &get_subchannel_ctx, get_subchannel_req, &get_subchannel_resp);
    EXPECT_TRUE(s.ok()) << s.error_message();
    EXPECT_EQ(get_subchannel_resp.subchannel().socket_ref_size(), 1);
    // Now grab the socket.
    GetSocketRequest get_socket_req;
    GetSocketResponse get_socket_resp;
    ClientContext get_socket_ctx;
    get_socket_req.set_socket_id(
        get_subchannel_resp.subchannel().socket_ref(0).socket_id());
    s = channelz_stub_->GetSocket(&get_socket_ctx, get_socket_req,
                                  &get_socket_resp);
    EXPECT_TRUE(
        get_subchannel_resp.subchannel().socket_ref(0).name().find("http"));
    EXPECT_TRUE(s.ok()) << s.error_message();
    // calls started == streams started AND stream succeeded. Since none of
    // these RPCs were canceled, all of the streams will succeeded even though
    // the RPCs they represent might have failed.
    EXPECT_EQ(get_subchannel_resp.subchannel().data().calls_started(),
              get_socket_resp.socket().data().streams_started());
    EXPECT_EQ(get_subchannel_resp.subchannel().data().calls_started(),
              get_socket_resp.socket().data().streams_succeeded());
    // All of the calls were unary, so calls started == messages sent.
    EXPECT_EQ(get_subchannel_resp.subchannel().data().calls_started(),
              get_socket_resp.socket().data().messages_sent());
    // We only get responses when the RPC was successful, so
    // calls succeeded == messages received.
    EXPECT_EQ(get_subchannel_resp.subchannel().data().calls_succeeded(),
              get_socket_resp.socket().data().messages_received());
    switch (GetParam()) {
      case CredentialsType::kInsecure:
        EXPECT_FALSE(get_socket_resp.socket().has_security());
        break;
      case CredentialsType::kTls:
      case CredentialsType::kMtls:
        EXPECT_TRUE(get_socket_resp.socket().has_security());
        EXPECT_TRUE(get_socket_resp.socket().security().has_tls());
        EXPECT_EQ(
            RemoveWhitespaces(
                get_socket_resp.socket().security().tls().remote_certificate()),
            RemoveWhitespaces(GetFileContents(kServerCertPath)));
        break;
    }
  }
}

TEST_P(ChannelzServerTest, StreamingRPC) {
  ResetStubs();
  ConfigureProxy(1);
  const int kNumMessages = 5;
  SendSuccessfulStream(kNumMessages);
  // Get the channel
  GetChannelRequest get_channel_request;
  GetChannelResponse get_channel_response;
  get_channel_request.set_channel_id(GetChannelId(0));
  ClientContext get_channel_context;
  Status s = channelz_stub_->GetChannel(
      &get_channel_context, get_channel_request, &get_channel_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_channel_response.channel().data().calls_started(), 1);
  EXPECT_EQ(get_channel_response.channel().data().calls_succeeded(), 1);
  EXPECT_EQ(get_channel_response.channel().data().calls_failed(), 0);
  // Get the subchannel
  ASSERT_GT(get_channel_response.channel().subchannel_ref_size(), 0);
  GetSubchannelRequest get_subchannel_request;
  GetSubchannelResponse get_subchannel_response;
  ClientContext get_subchannel_context;
  get_subchannel_request.set_subchannel_id(
      get_channel_response.channel().subchannel_ref(0).subchannel_id());
  s = channelz_stub_->GetSubchannel(&get_subchannel_context,
                                    get_subchannel_request,
                                    &get_subchannel_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_subchannel_response.subchannel().data().calls_started(), 1);
  EXPECT_EQ(get_subchannel_response.subchannel().data().calls_succeeded(), 1);
  EXPECT_EQ(get_subchannel_response.subchannel().data().calls_failed(), 0);
  // Get the socket
  ASSERT_GT(get_subchannel_response.subchannel().socket_ref_size(), 0);
  GetSocketRequest get_socket_request;
  GetSocketResponse get_socket_response;
  ClientContext get_socket_context;
  get_socket_request.set_socket_id(
      get_subchannel_response.subchannel().socket_ref(0).socket_id());
  EXPECT_TRUE(
      get_subchannel_response.subchannel().socket_ref(0).name().find("http"));
  s = channelz_stub_->GetSocket(&get_socket_context, get_socket_request,
                                &get_socket_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_socket_response.socket().data().streams_started(), 1);
  EXPECT_EQ(get_socket_response.socket().data().streams_succeeded(), 1);
  EXPECT_EQ(get_socket_response.socket().data().streams_failed(), 0);
  EXPECT_EQ(get_socket_response.socket().data().messages_sent(), kNumMessages);
  EXPECT_EQ(get_socket_response.socket().data().messages_received(),
            kNumMessages);
  switch (GetParam()) {
    case CredentialsType::kInsecure:
      EXPECT_FALSE(get_socket_response.socket().has_security());
      break;
    case CredentialsType::kTls:
    case CredentialsType::kMtls:
      EXPECT_TRUE(get_socket_response.socket().has_security());
      EXPECT_TRUE(get_socket_response.socket().security().has_tls());
      EXPECT_EQ(RemoveWhitespaces(get_socket_response.socket()
                                      .security()
                                      .tls()
                                      .remote_certificate()),
                RemoveWhitespaces(GetFileContents(kServerCertPath)));
      break;
  }
}

TEST_P(ChannelzServerTest, GetServerSocketsTest) {
  ResetStubs();
  ConfigureProxy(1);
  GetServersRequest get_server_request;
  GetServersResponse get_server_response;
  get_server_request.set_start_server_id(0);
  ClientContext get_server_context;
  Status s = channelz_stub_->GetServers(&get_server_context, get_server_request,
                                        &get_server_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_server_response.server_size(), 1);
  GetServerSocketsRequest get_server_sockets_request;
  GetServerSocketsResponse get_server_sockets_response;
  get_server_sockets_request.set_server_id(
      get_server_response.server(0).ref().server_id());
  get_server_sockets_request.set_start_socket_id(0);
  ClientContext get_server_sockets_context;
  s = channelz_stub_->GetServerSockets(&get_server_sockets_context,
                                       get_server_sockets_request,
                                       &get_server_sockets_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_server_sockets_response.socket_ref_size(), 1);
  EXPECT_TRUE(get_server_sockets_response.socket_ref(0).name().find("http"));
  // Get the socket to verify security information.
  GetSocketRequest get_socket_request;
  GetSocketResponse get_socket_response;
  ClientContext get_socket_context;
  get_socket_request.set_socket_id(
      get_server_sockets_response.socket_ref(0).socket_id());
  s = channelz_stub_->GetSocket(&get_socket_context, get_socket_request,
                                &get_socket_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_TRUE(ValidateAddress(get_socket_response.socket().remote()));
  EXPECT_TRUE(ValidateAddress(get_socket_response.socket().local()));
  switch (GetParam()) {
    case CredentialsType::kInsecure:
      EXPECT_FALSE(get_socket_response.socket().has_security());
      break;
    case CredentialsType::kTls:
    case CredentialsType::kMtls:
      EXPECT_TRUE(get_socket_response.socket().has_security());
      EXPECT_TRUE(get_socket_response.socket().security().has_tls());
      if (GetParam() == CredentialsType::kMtls) {
        EXPECT_EQ(RemoveWhitespaces(get_socket_response.socket()
                                        .security()
                                        .tls()
                                        .remote_certificate()),
                  RemoveWhitespaces(GetFileContents(kClientCertPath)));
      } else {
        EXPECT_TRUE(get_socket_response.socket()
                        .security()
                        .tls()
                        .remote_certificate()
                        .empty());
      }
      break;
  }
}

TEST_P(ChannelzServerTest, GetServerSocketsPaginationTest) {
  ResetStubs();
  ConfigureProxy(1);
  std::vector<std::unique_ptr<grpc::testing::EchoTestService::Stub>> stubs;
  const int kNumServerSocketsCreated = 20;
  for (int i = 0; i < kNumServerSocketsCreated; ++i) {
    stubs.push_back(NewEchoStub());
    EchoRequest request;
    EchoResponse response;
    request.set_message("Hello channelz");
    request.mutable_param()->set_backend_channel_idx(0);
    ClientContext context;
    Status s = stubs.back()->Echo(&context, request, &response);
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
    if (s.ok()) EXPECT_EQ(response.message(), request.message());
  }
  GetServersRequest get_server_request;
  GetServersResponse get_server_response;
  get_server_request.set_start_server_id(0);
  ClientContext get_server_context;
  Status s = channelz_stub_->GetServers(&get_server_context, get_server_request,
                                        &get_server_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_server_response.server_size(), 1);
  // Make a request that gets all of the serversockets
  {
    GetServerSocketsRequest get_server_sockets_request;
    GetServerSocketsResponse get_server_sockets_response;
    get_server_sockets_request.set_server_id(
        get_server_response.server(0).ref().server_id());
    get_server_sockets_request.set_start_socket_id(0);
    ClientContext get_server_sockets_context;
    s = channelz_stub_->GetServerSockets(&get_server_sockets_context,
                                         get_server_sockets_request,
                                         &get_server_sockets_response);
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
    // We add one to account the channelz stub that will end up creating
    // a serversocket.
    EXPECT_EQ(get_server_sockets_response.socket_ref_size(),
              kNumServerSocketsCreated + 1);
    EXPECT_TRUE(get_server_sockets_response.end());
  }
  // Now we make a request that exercises pagination.
  {
    GetServerSocketsRequest get_server_sockets_request;
    GetServerSocketsResponse get_server_sockets_response;
    get_server_sockets_request.set_server_id(
        get_server_response.server(0).ref().server_id());
    get_server_sockets_request.set_start_socket_id(0);
    const int kMaxResults = 10;
    get_server_sockets_request.set_max_results(kMaxResults);
    ClientContext get_server_sockets_context;
    s = channelz_stub_->GetServerSockets(&get_server_sockets_context,
                                         get_server_sockets_request,
                                         &get_server_sockets_response);
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
    EXPECT_EQ(get_server_sockets_response.socket_ref_size(), kMaxResults);
    EXPECT_FALSE(get_server_sockets_response.end());
  }
}

TEST_P(ChannelzServerTest, GetServerListenSocketsTest) {
  ResetStubs();
  ConfigureProxy(1);
  GetServersRequest get_server_request;
  GetServersResponse get_server_response;
  get_server_request.set_start_server_id(0);
  ClientContext get_server_context;
  Status s = channelz_stub_->GetServers(&get_server_context, get_server_request,
                                        &get_server_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_server_response.server_size(), 1);
  // The resolver might return one or two addresses depending on the
  // configuration, one for ipv4 and one for ipv6.
  int listen_socket_size = get_server_response.server(0).listen_socket_size();
  EXPECT_THAT(listen_socket_size, ::testing::AnyOf(1, 2));
  GetSocketRequest get_socket_request;
  GetSocketResponse get_socket_response;
  get_socket_request.set_socket_id(
      get_server_response.server(0).listen_socket(0).socket_id());
  EXPECT_TRUE(
      get_server_response.server(0).listen_socket(0).name().find("http"));
  ClientContext get_socket_context_1;
  s = channelz_stub_->GetSocket(&get_socket_context_1, get_socket_request,
                                &get_socket_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();

  EXPECT_TRUE(ValidateAddress(get_socket_response.socket().remote()));
  EXPECT_TRUE(ValidateAddress(get_socket_response.socket().local()));
  if (listen_socket_size == 2) {
    get_socket_request.set_socket_id(
        get_server_response.server(0).listen_socket(1).socket_id());
    ClientContext get_socket_context_2;
    EXPECT_TRUE(
        get_server_response.server(0).listen_socket(1).name().find("http"));
    s = channelz_stub_->GetSocket(&get_socket_context_2, get_socket_request,
                                  &get_socket_response);
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  }
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
