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

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <gflags/gflags.h>
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>
#include <grpc++/status.h>
#include "test/cpp/util/test_config.h"

#include "examples/pubsub/publisher.h"
#include "examples/pubsub/subscriber.h"

DEFINE_int32(server_port, 443, "Server port.");
DEFINE_string(server_host, "pubsub-staging.googleapis.com",
              "Server host to connect to");
DEFINE_string(project_id, "", "GCE project id such as stoked-keyword-656");

namespace {

const char kTopic[] = "testtopics";
const char kSubscriptionName[] = "testsubscription";
const char kMessageData[] = "Test Data";

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  gpr_log(GPR_INFO, "Start PUBSUB client");

  std::ostringstream ss;

  ss << FLAGS_server_host << ":" << FLAGS_server_port;

  std::shared_ptr<grpc::Credentials> creds = grpc::GoogleDefaultCredentials();
  std::shared_ptr<grpc::ChannelInterface> channel =
      grpc::CreateChannel(ss.str(), creds, grpc::ChannelArguments());

  grpc::examples::pubsub::Publisher publisher(channel);
  grpc::examples::pubsub::Subscriber subscriber(channel);

  GPR_ASSERT(FLAGS_project_id != "");
  ss.str("");
  ss << "/topics/" << FLAGS_project_id << "/" << kTopic;
  grpc::string topic = ss.str();

  ss.str("");
  ss << FLAGS_project_id << "/" << kSubscriptionName;
  grpc::string subscription_name = ss.str();

  // Clean up test topic and subcription if they exist before.
  grpc::string subscription_topic;
  if (subscriber.GetSubscription(subscription_name, &subscription_topic)
          .IsOk()) {
    subscriber.DeleteSubscription(subscription_name);
  }

  if (publisher.GetTopic(topic).IsOk()) publisher.DeleteTopic(topic);

  grpc::Status s = publisher.CreateTopic(topic);
  gpr_log(GPR_INFO, "Create topic returns code %d, %s", s.code(),
          s.details().c_str());
  GPR_ASSERT(s.IsOk());

  s = publisher.GetTopic(topic);
  gpr_log(GPR_INFO, "Get topic returns code %d, %s", s.code(),
          s.details().c_str());
  GPR_ASSERT(s.IsOk());

  std::vector<grpc::string> topics;
  s = publisher.ListTopics(FLAGS_project_id, &topics);
  gpr_log(GPR_INFO, "List topic returns code %d, %s", s.code(),
          s.details().c_str());
  bool topic_found = false;
  for (unsigned int i = 0; i < topics.size(); i++) {
    if (topics[i] == topic) topic_found = true;
    gpr_log(GPR_INFO, "topic: %s", topics[i].c_str());
  }
  GPR_ASSERT(s.IsOk());
  GPR_ASSERT(topic_found);

  s = subscriber.CreateSubscription(topic, subscription_name);
  gpr_log(GPR_INFO, "create subscrption returns code %d, %s", s.code(),
          s.details().c_str());
  GPR_ASSERT(s.IsOk());

  s = publisher.Publish(topic, kMessageData);
  gpr_log(GPR_INFO, "Publish %s returns code %d, %s", kMessageData, s.code(),
          s.details().c_str());
  GPR_ASSERT(s.IsOk());

  grpc::string data;
  s = subscriber.Pull(subscription_name, &data);
  gpr_log(GPR_INFO, "Pull %s", data.c_str());

  s = subscriber.DeleteSubscription(subscription_name);
  gpr_log(GPR_INFO, "Delete subscription returns code %d, %s", s.code(),
          s.details().c_str());
  GPR_ASSERT(s.IsOk());

  s = publisher.DeleteTopic(topic);
  gpr_log(GPR_INFO, "Delete topic returns code %d, %s", s.code(),
          s.details().c_str());
  GPR_ASSERT(s.IsOk());

  subscriber.Shutdown();
  publisher.Shutdown();
  return 0;
}
