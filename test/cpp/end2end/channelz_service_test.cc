/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <grpcpp/ext/channelz_service_plugin.h>
#include "src/proto/grpc/channelz/channelz.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

#include <google/protobuf/text_format.h>

#include <gtest/gtest.h>

using grpc::channelz::v1::GetChannelRequest;
using grpc::channelz::v1::GetChannelResponse;
using grpc::channelz::v1::GetServersRequest;
using grpc::channelz::v1::GetServersResponse;
using grpc::channelz::v1::GetSocketRequest;
using grpc::channelz::v1::GetSocketResponse;
using grpc::channelz::v1::GetSubchannelRequest;
using grpc::channelz::v1::GetSubchannelResponse;
using grpc::channelz::v1::GetTopChannelsRequest;
using grpc::channelz::v1::GetTopChannelsResponse;

namespace grpc {
namespace testing {
namespace {

// Proxy service supports N backends. Sends RPC to backend dictated by
// request->backend_channel_idx().
class Proxy : public ::grpc::testing::EchoTestService::Service {
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
  std::vector<std::unique_ptr<::grpc::testing::EchoTestService::Stub>> stubs_;
};

}  // namespace

class ChannelzServerTest : public ::testing::Test {
 public:
  ChannelzServerTest() {}

  void SetUp() override {
    // ensure channel server is brought up on all severs we build.
    ::grpc::channelz::experimental::InitChannelzService();

    // We set up a proxy server with channelz enabled.
    proxy_port_ = grpc_pick_unused_port_or_die();
    ServerBuilder proxy_builder;
    grpc::string proxy_server_address = "localhost:" + to_string(proxy_port_);
    proxy_builder.AddListeningPort(proxy_server_address,
                                   InsecureServerCredentials());
    // forces channelz and channel tracing to be enabled.
    proxy_builder.AddChannelArgument(GRPC_ARG_ENABLE_CHANNELZ, 1);
    proxy_builder.AddChannelArgument(GRPC_ARG_MAX_CHANNEL_TRACE_EVENTS_PER_NODE,
                                     10);
    proxy_builder.RegisterService(&proxy_service_);
    proxy_server_ = proxy_builder.BuildAndStart();
  }

  // Sets the proxy up to have an arbitrary number of backends.
  void ConfigureProxy(size_t num_backends) {
    backends_.resize(num_backends);
    for (size_t i = 0; i < num_backends; ++i) {
      // create a new backend.
      backends_[i].port = grpc_pick_unused_port_or_die();
      ServerBuilder backend_builder;
      grpc::string backend_server_address =
          "localhost:" + to_string(backends_[i].port);
      backend_builder.AddListeningPort(backend_server_address,
                                       InsecureServerCredentials());
      backends_[i].service.reset(new TestServiceImpl);
      // ensure that the backend itself has channelz disabled.
      backend_builder.AddChannelArgument(GRPC_ARG_ENABLE_CHANNELZ, 0);
      backend_builder.RegisterService(backends_[i].service.get());
      backends_[i].server = backend_builder.BuildAndStart();
      // set up a channel to the backend. We ensure that this channel has
      // channelz enabled since these channels (proxy outbound to backends)
      // are the ones that our test will actually be validating.
      ChannelArguments args;
      args.SetInt(GRPC_ARG_ENABLE_CHANNELZ, 1);
      args.SetInt(GRPC_ARG_MAX_CHANNEL_TRACE_EVENTS_PER_NODE, 10);
      std::shared_ptr<Channel> channel_to_backend = CreateCustomChannel(
          backend_server_address, InsecureChannelCredentials(), args);
      proxy_service_.AddChannelToBackend(channel_to_backend);
    }
  }

  void ResetStubs() {
    string target = "dns:localhost:" + to_string(proxy_port_);
    ChannelArguments args;
    // disable channelz. We only want to focus on proxy to backend outbound.
    args.SetInt(GRPC_ARG_ENABLE_CHANNELZ, 0);
    std::shared_ptr<Channel> channel =
        CreateCustomChannel(target, InsecureChannelCredentials(), args);
    channelz_stub_ = grpc::channelz::v1::Channelz::NewStub(channel);
    echo_stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

  void SendSuccessfulEcho(int channel_idx) {
    EchoRequest request;
    EchoResponse response;
    request.set_message("Hello channelz");
    request.mutable_param()->set_backend_channel_idx(channel_idx);
    ClientContext context;
    Status s = echo_stub_->Echo(&context, request, &response);
    EXPECT_EQ(response.message(), request.message());
    EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
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
  Proxy proxy_service_;

  // backends. All implement the echo service.
  std::vector<BackendData> backends_;
};

TEST_F(ChannelzServerTest, BasicTest) {
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

TEST_F(ChannelzServerTest, HighStartId) {
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

TEST_F(ChannelzServerTest, SuccessfulRequestTest) {
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

TEST_F(ChannelzServerTest, FailedRequestTest) {
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

TEST_F(ChannelzServerTest, ManyRequestsTest) {
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

TEST_F(ChannelzServerTest, ManyChannels) {
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

TEST_F(ChannelzServerTest, ManyRequestsManyChannels) {
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

TEST_F(ChannelzServerTest, ManySubchannels) {
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

TEST_F(ChannelzServerTest, BasicServerTest) {
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

TEST_F(ChannelzServerTest, ServerCallTest) {
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

TEST_F(ChannelzServerTest, ManySubchannelsAndSockets) {
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
  }
}

TEST_F(ChannelzServerTest, StreamingRPC) {
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
  s = channelz_stub_->GetSocket(&get_socket_context, get_socket_request,
                                &get_socket_response);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
  EXPECT_EQ(get_socket_response.socket().data().streams_started(), 1);
  EXPECT_EQ(get_socket_response.socket().data().streams_succeeded(), 1);
  EXPECT_EQ(get_socket_response.socket().data().streams_failed(), 0);
  EXPECT_EQ(get_socket_response.socket().data().messages_sent(), kNumMessages);
  EXPECT_EQ(get_socket_response.socket().data().messages_received(),
            kNumMessages);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
