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

#include <sstream>

#include <grpc++/client_context.h>

#include "examples/pubsub/publisher.h"

using tech::pubsub::Topic;
using tech::pubsub::DeleteTopicRequest;
using tech::pubsub::GetTopicRequest;
using tech::pubsub::PublisherService;
using tech::pubsub::ListTopicsRequest;
using tech::pubsub::ListTopicsResponse;
using tech::pubsub::PublishRequest;
using tech::pubsub::PubsubMessage;

namespace grpc {
namespace examples {
namespace pubsub {

Publisher::Publisher(std::shared_ptr<Channel> channel)
    : stub_(PublisherService::NewStub(channel)) {}

void Publisher::Shutdown() { stub_.reset(); }

Status Publisher::CreateTopic(const grpc::string& topic) {
  Topic request;
  Topic response;
  request.set_name(topic);
  ClientContext context;

  return stub_->CreateTopic(&context, request, &response);
}

Status Publisher::ListTopics(const grpc::string& project_id,
                             std::vector<grpc::string>* topics) {
  ListTopicsRequest request;
  ListTopicsResponse response;
  ClientContext context;

  std::ostringstream ss;
  ss << "cloud.googleapis.com/project in (/projects/" << project_id << ")";
  request.set_query(ss.str());

  Status s = stub_->ListTopics(&context, request, &response);

  tech::pubsub::Topic topic;
  for (int i = 0; i < response.topic_size(); i++) {
    topic = response.topic(i);
    topics->push_back(topic.name());
  }

  return s;
}

Status Publisher::GetTopic(const grpc::string& topic) {
  GetTopicRequest request;
  Topic response;
  ClientContext context;

  request.set_topic(topic);

  return stub_->GetTopic(&context, request, &response);
}

Status Publisher::DeleteTopic(const grpc::string& topic) {
  DeleteTopicRequest request;
  proto2::Empty response;
  ClientContext context;

  request.set_topic(topic);

  return stub_->DeleteTopic(&context, request, &response);
}

Status Publisher::Publish(const grpc::string& topic, const grpc::string& data) {
  PublishRequest request;
  proto2::Empty response;
  ClientContext context;

  request.mutable_message()->set_data(data);
  request.set_topic(topic);

  return stub_->Publish(&context, request, &response);
}

}  // namespace pubsub
}  // namespace examples
}  // namespace grpc
