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

#include <google/protobuf/stubs/common.h>

#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/status.h>
#include <gtest/gtest.h>

#include "examples/pubsub/publisher.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

using grpc::ChannelInterface;

namespace grpc {
namespace testing {
namespace {

const char kProjectId[] = "project id";
const char kTopic[] = "test topic";
const char kMessageData[] = "test message data";

class PublisherServiceImpl : public tech::pubsub::PublisherService::Service {
 public:
  Status CreateTopic(::grpc::ServerContext* context,
                     const ::tech::pubsub::Topic* request,
                     ::tech::pubsub::Topic* response) GRPC_OVERRIDE {
    EXPECT_EQ(request->name(), kTopic);
    return Status::OK;
  }

  Status Publish(ServerContext* context,
                 const ::tech::pubsub::PublishRequest* request,
                 ::proto2::Empty* response) GRPC_OVERRIDE {
    EXPECT_EQ(request->message().data(), kMessageData);
    return Status::OK;
  }

  Status GetTopic(ServerContext* context,
                  const ::tech::pubsub::GetTopicRequest* request,
                  ::tech::pubsub::Topic* response) GRPC_OVERRIDE {
    EXPECT_EQ(request->topic(), kTopic);
    return Status::OK;
  }

  Status ListTopics(
      ServerContext* context, const ::tech::pubsub::ListTopicsRequest* request,
      ::tech::pubsub::ListTopicsResponse* response) GRPC_OVERRIDE {
   std::ostringstream ss;
   ss << "cloud.googleapis.com/project in (/projects/" << kProjectId << ")";
   EXPECT_EQ(request->query(), ss.str());
   response->add_topic()->set_name(kTopic);
   return Status::OK;
 }

 Status DeleteTopic(ServerContext* context,
                    const ::tech::pubsub::DeleteTopicRequest* request,
                    ::proto2::Empty* response) GRPC_OVERRIDE {
    EXPECT_EQ(request->topic(), kTopic);
    return Status::OK;
 }

};

class PublisherTest : public ::testing::Test {
 protected:
  // Setup a server and a client for PublisherService.
  void SetUp() GRPC_OVERRIDE {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    ServerBuilder builder;
    builder.AddPort(server_address_.str(), grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();

    channel_ = CreateChannel(server_address_.str(), grpc::InsecureCredentials(), ChannelArguments());

    publisher_.reset(new grpc::examples::pubsub::Publisher(channel_));
  }

  void TearDown() GRPC_OVERRIDE {
    server_->Shutdown();
    publisher_->Shutdown();
  }

  std::ostringstream server_address_;
  std::unique_ptr<Server> server_;
  PublisherServiceImpl service_;

  std::shared_ptr<ChannelInterface> channel_;

  std::unique_ptr<grpc::examples::pubsub::Publisher> publisher_;
};

TEST_F(PublisherTest, TestPublisher) {
  EXPECT_TRUE(publisher_->CreateTopic(kTopic).IsOk());

  EXPECT_TRUE(publisher_->Publish(kTopic, kMessageData).IsOk());

  EXPECT_TRUE(publisher_->GetTopic(kTopic).IsOk());

  std::vector<grpc::string> topics;
  EXPECT_TRUE(publisher_->ListTopics(kProjectId, &topics).IsOk());
  EXPECT_EQ(topics.size(), static_cast<size_t>(1));
  EXPECT_EQ(topics[0], kTopic);
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
