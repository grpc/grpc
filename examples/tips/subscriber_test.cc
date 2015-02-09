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

#include <google/protobuf/stubs/common.h>

#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/status.h>
#include <gtest/gtest.h>

#include "examples/tips/subscriber.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

const char kTopic[] = "test topic";
const char kSubscriptionName[] = "subscription name";
const char kData[] = "Message data";

class SubscriberServiceImpl : public tech::pubsub::SubscriberService::Service {
 public:
  Status CreateSubscription(ServerContext* context,
                            const tech::pubsub::Subscription* request,
                            tech::pubsub::Subscription* response) override {
    EXPECT_EQ(request->topic(), kTopic);
    EXPECT_EQ(request->name(), kSubscriptionName);
    return Status::OK;
  }

  Status GetSubscription(ServerContext* context,
                         const tech::pubsub::GetSubscriptionRequest* request,
                         tech::pubsub::Subscription* response) override {
    EXPECT_EQ(request->subscription(), kSubscriptionName);
    response->set_topic(kTopic);
    return Status::OK;
  }

  Status DeleteSubscription(
      ServerContext* context,
      const tech::pubsub::DeleteSubscriptionRequest* request,
      proto2::Empty* response) override {
    EXPECT_EQ(request->subscription(), kSubscriptionName);
    return Status::OK;
  }

  Status Pull(ServerContext* context,
              const tech::pubsub::PullRequest* request,
              tech::pubsub::PullResponse* response) override {
    EXPECT_EQ(request->subscription(), kSubscriptionName);
    response->set_ack_id("1");
    response->mutable_pubsub_event()->mutable_message()->set_data(kData);
    return Status::OK;
  }

  Status Acknowledge(ServerContext* context,
                     const tech::pubsub::AcknowledgeRequest* request,
                     proto2::Empty* response) override {
    return Status::OK;
  }

};

class SubscriberTest : public ::testing::Test {
 protected:
  // Setup a server and a client for SubscriberService.
  void SetUp() override {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    ServerBuilder builder;
    builder.AddPort(server_address_.str());
    builder.RegisterService(service_.service());
    server_ = builder.BuildAndStart();

    channel_ = CreateChannel(server_address_.str(), ChannelArguments());

    subscriber_.reset(new grpc::examples::tips::Subscriber(channel_));
  }

  void TearDown() override {
    server_->Shutdown();
    subscriber_->Shutdown();
  }

  std::ostringstream server_address_;
  std::unique_ptr<Server> server_;
  SubscriberServiceImpl service_;

  std::shared_ptr<ChannelInterface> channel_;

  std::unique_ptr<grpc::examples::tips::Subscriber> subscriber_;
};

TEST_F(SubscriberTest, TestSubscriber) {
  EXPECT_TRUE(subscriber_->CreateSubscription(kTopic,
                                              kSubscriptionName).IsOk());

  grpc::string topic;
  EXPECT_TRUE(subscriber_->GetSubscription(kSubscriptionName,
                                           &topic).IsOk());
  EXPECT_EQ(topic, kTopic);

  grpc::string data;
  EXPECT_TRUE(subscriber_->Pull(kSubscriptionName,
                                &data).IsOk());

  EXPECT_TRUE(subscriber_->DeleteSubscription(kSubscriptionName).IsOk());
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
