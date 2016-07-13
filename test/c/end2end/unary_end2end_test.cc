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

#include <grpc_c/grpc_c.h>
#include <grpc_c/client_context.h>
#include <grpc_c/channel.h>
#include <grpc_c/unary_blocking_call.h>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/auth_metadata_processor.h>
#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <gtest/gtest.h>

#include <grpc_c/status.h>

#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/string_ref_helper.h"
#include "test/cpp/util/test_credentials_provider.h"


using grpc::testing::kTlsCredentialsType;
using std::chrono::system_clock;

namespace grpc {
namespace testing {
namespace {

class TestScenario {
public:
  TestScenario(bool proxy, const grpc::string &creds_type)
    : use_proxy(proxy), credentials_type(creds_type) { }

  void Log() const {
    gpr_log(GPR_INFO, "Scenario: proxy %d, credentials %s", use_proxy,
            credentials_type.c_str());
  }

  bool use_proxy;
  // Although the below grpc::string is logically const, we can't declare
  // them const because of a limitation in the way old compilers (e.g., gcc-4.4)
  // manage vector insertion using a copy constructor
  grpc::string credentials_type;
};

class Proxy : public ::grpc::testing::EchoTestService::Service {
public:
  Proxy(std::shared_ptr<Channel> channel)
    : stub_(grpc::testing::EchoTestService::NewStub(channel)) {}

  Status Echo(ServerContext* server_context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    std::unique_ptr<ClientContext> client_context =
      ClientContext::FromServerContext(*server_context);
    return stub_->Echo(client_context.get(), *request, response);
  }

private:
  std::unique_ptr< ::grpc::testing::EchoTestService::Stub> stub_;
};

class End2endTest : public ::testing::TestWithParam<TestScenario> {
protected:
  End2endTest()
    : is_server_started_(false),
      c_channel_(NULL),
      kMaxMessageSize_(8192),
      special_service_("special") {
    GetParam().Log();
  }

  ~End2endTest() {
    GRPC_channel_destroy(&c_channel_);
  }

  void TearDown() GRPC_OVERRIDE {
    if (is_server_started_) {
      server_->Shutdown();
      if (proxy_server_) proxy_server_->Shutdown();
    }
  }

  void StartServer(const std::shared_ptr<AuthMetadataProcessor> &processor) {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "127.0.0.1:" << port;
    // Setup server
    ServerBuilder builder;
    auto server_creds = GetServerCredentials(GetParam().credentials_type);
    if (GetParam().credentials_type != kInsecureCredentialsType) {
      server_creds->SetAuthMetadataProcessor(processor);
    }
    builder.AddListeningPort(server_address_.str(), server_creds);
    builder.RegisterService(&service_);
    builder.RegisterService("foo.test.youtube.com", &special_service_);
    builder.SetMaxMessageSize(
      kMaxMessageSize_);  // For testing max message size.
    server_ = builder.BuildAndStart();
    is_server_started_ = true;
  }

  void ResetChannel() {
    if (!is_server_started_) {
      StartServer(std::shared_ptr<AuthMetadataProcessor>());
    }
    EXPECT_TRUE(is_server_started_);
    ChannelArguments args;
    auto channel_creds =
      GetChannelCredentials(GetParam().credentials_type, &args);
    if (!user_agent_prefix_.empty()) {
      args.SetUserAgentPrefix(user_agent_prefix_);
    }
    args.SetString(GRPC_ARG_SECONDARY_USER_AGENT_STRING, "end2end_test");

    channel_ = CreateCustomChannel(server_address_.str(), channel_creds, args);
    // TODO(yifeit): add credentials
    if (c_channel_) GRPC_channel_destroy(&c_channel_);
    c_channel_ = GRPC_channel_create(server_address_.str().c_str());
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

      channel_ = CreateChannel(proxyaddr.str(), InsecureChannelCredentials());
      c_channel_ = GRPC_channel_create(proxyaddr.str().c_str());
    }
  }

  bool is_server_started_;

  std::shared_ptr<Channel> channel_;
  GRPC_channel *c_channel_;

  std::unique_ptr<Server> server_;
  std::unique_ptr<Server> proxy_server_;
  std::unique_ptr<Proxy> proxy_service_;
  std::ostringstream server_address_;
  const int kMaxMessageSize_;
  TestServiceImpl service_;
  TestServiceImpl special_service_;
  grpc::string user_agent_prefix_;
};

static void SendRpc(GRPC_channel *channel,
                    int num_rpcs,
                    bool with_binary_metadata) {

  for (int i = 0; i < num_rpcs; ++i) {
    GRPC_method method = { GRPC_method::RpcType::NORMAL_RPC, "/grpc.testing.EchoTestService/Echo" };
    GRPC_client_context *context = GRPC_client_context_create(channel);
    // hardcoded string for "gRPC-C"
    char str[] = {0x0A, 0x06, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43};
    GRPC_message msg = {str, sizeof(str)};
    // using char array to hold RPC result while protobuf is not there yet
    GRPC_message resp;
    GRPC_status status = GRPC_unary_blocking_call(channel, &method, context, msg, &resp);
    assert(status.code == GRPC_STATUS_OK);
    char *response_string = (char *) malloc(resp.length - 2 + 1);
    memcpy(response_string, ((char *) resp.data) + 2, resp.length - 2);
    response_string[resp.length - 2] = '\0';
    printf("Server said: %s\n", response_string);    // skip to the string in serialized protobuf object
    GRPC_message_destroy(&resp);
    GRPC_client_context_destroy(&context);

    EXPECT_EQ(grpc::string(response_string), grpc::string("gRPC-C"));
    EXPECT_TRUE(status.code == GRPC_STATUS_OK);
  }
}


//////////////////////////////////////////////////////////////////////////
// Test with and without a proxy.
class ProxyEnd2endTest : public End2endTest {
protected:
};

TEST_P(ProxyEnd2endTest, SimpleRpc) {
  ResetStub();
  SendRpc(c_channel_, 1, false);
}

} // namespace
} // namespace testing
} // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
