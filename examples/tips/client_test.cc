/*
 *
 * Copyright 2014, Google Inc.
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

#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/status.h>
#include <gtest/gtest.h>

#include "examples/tips/client.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

using grpc::ChannelInterface;

namespace {

}  //

namespace grpc {
namespace testing {
namespace {

const char kTopic[] = "test topic";

class PublishServiceImpl : public tech::pubsub::PublisherService::Service {
 public:
  Status CreateTopic(::grpc::ServerContext* context,
                     const ::tech::pubsub::Topic* request,
                     ::tech::pubsub::Topic* response) override {
    EXPECT_EQ(request->name(), kTopic);
    return Status::OK;
  }
};

class End2endTest : public ::testing::Test {
 protected:
  void SetUp() override {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddPort(server_address_.str());
    builder.RegisterService(service_.service());
    server_ = builder.BuildAndStart();

    channel_ = CreateChannel(server_address_.str(), ChannelArguments());
  }

  void TearDown() override { server_->Shutdown(); }

  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  PublishServiceImpl service_;

  std::shared_ptr<ChannelInterface> channel_;
};

TEST_F(End2endTest, CreateTopic) {
  grpc::examples::tips::Client client(channel_);
  client.CreateTopic(kTopic);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  gpr_log(GPR_INFO, "Start test ...");
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
