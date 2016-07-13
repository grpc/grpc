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

extern "C" {
#include <grpc_c/grpc_c.h>
#include <grpc_c/client_context.h>
#include <grpc_c/channel.h>
#include <grpc_c/unary_blocking_call.h>
#include <grpc_c/status.h>
}

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

class End2endTest {
public:
  End2endTest()
    : is_server_started_(false),
      kMaxMessageSize_(8192),
      c_channel_(NULL) {
  }

  ~End2endTest() {
    GRPC_channel_destroy(&c_channel_);
  }

  void TearDown() {
    if (is_server_started_) {
      server_->Shutdown();
    }
  }

  void StartServer(const std::shared_ptr<AuthMetadataProcessor> &processor) {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "127.0.0.1:" << port;
    // Setup server
    ServerBuilder builder;

    builder.AddListeningPort(server_address_.str(), InsecureServerCredentials());
    builder.RegisterService(&service_);
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

    // TODO(yifeit): add credentials
    if (c_channel_) GRPC_channel_destroy(&c_channel_);
    c_channel_ = GRPC_channel_create(server_address_.str().c_str());
  }

  void ResetStub() {
    ResetChannel();
  }

  bool is_server_started_;

  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  const int kMaxMessageSize_;
  TestServiceImpl service_;
  grpc::string user_agent_prefix_;

  GRPC_channel *c_channel_;
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

    EXPECT_TRUE(status.ok) << status.details;
    EXPECT_TRUE(status.code == GRPC_STATUS_OK) << status.details;

    char *response_string = (char *) malloc(resp.length - 2 + 1);
    memcpy(response_string, ((char *) resp.data) + 2, resp.length - 2);
    response_string[resp.length - 2] = '\0';

    EXPECT_EQ(grpc::string(response_string), grpc::string("gRPC-C"));

    GRPC_message_destroy(&resp);
    GRPC_client_context_destroy(&context);
  }
}

class UnaryEnd2endTest : public End2endTest {
protected:
};

TEST(UnaryEnd2endTest, SimpleRpc) {
  UnaryEnd2endTest test;
  test.ResetStub();
  SendRpc(test.c_channel_, 1, false);
  test.TearDown();
}

} // namespace
} // namespace testing
} // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
