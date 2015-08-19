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

#include <grpc++/client_context.h>

#include "examples/pubsub/subscriber.h"

using tech::pubsub::Topic;
using tech::pubsub::DeleteTopicRequest;
using tech::pubsub::GetTopicRequest;
using tech::pubsub::SubscriberService;
using tech::pubsub::ListTopicsRequest;
using tech::pubsub::ListTopicsResponse;
using tech::pubsub::PublishRequest;
using tech::pubsub::PubsubMessage;

namespace grpc {
namespace examples {
namespace pubsub {

Subscriber::Subscriber(std::shared_ptr<Channel> channel)
    : stub_(SubscriberService::NewStub(channel)) {}

void Subscriber::Shutdown() { stub_.reset(); }

Status Subscriber::CreateSubscription(const grpc::string& topic,
                                      const grpc::string& name) {
  tech::pubsub::Subscription request;
  tech::pubsub::Subscription response;
  ClientContext context;

  request.set_topic(topic);
  request.set_name(name);

  return stub_->CreateSubscription(&context, request, &response);
}

Status Subscriber::GetSubscription(const grpc::string& name,
                                   grpc::string* topic) {
  tech::pubsub::GetSubscriptionRequest request;
  tech::pubsub::Subscription response;
  ClientContext context;

  request.set_subscription(name);

  Status s = stub_->GetSubscription(&context, request, &response);
  *topic = response.topic();
  return s;
}

Status Subscriber::DeleteSubscription(const grpc::string& name) {
  tech::pubsub::DeleteSubscriptionRequest request;
  proto2::Empty response;
  ClientContext context;

  request.set_subscription(name);

  return stub_->DeleteSubscription(&context, request, &response);
}

Status Subscriber::Pull(const grpc::string& name, grpc::string* data) {
  tech::pubsub::PullRequest request;
  tech::pubsub::PullResponse response;
  ClientContext context;

  request.set_subscription(name);
  Status s = stub_->Pull(&context, request, &response);
  if (s.IsOk()) {
    tech::pubsub::PubsubEvent event = response.pubsub_event();
    if (event.has_message()) {
      *data = event.message().data();
    }
    tech::pubsub::AcknowledgeRequest ack;
    proto2::Empty empty;
    ClientContext ack_context;
    ack.set_subscription(name);
    ack.add_ack_id(response.ack_id());
    stub_->Acknowledge(&ack_context, ack, &empty);
  }
  return s;
}

}  // namespace pubsub
}  // namespace examples
}  // namespace grpc
