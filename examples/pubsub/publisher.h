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

#ifndef GRPC_EXAMPLES_PUBSUB_PUBLISHER_H
#define GRPC_EXAMPLES_PUBSUB_PUBLISHER_H

#include <grpc++/channel.h>
#include <grpc++/status.h>

#include "examples/pubsub/pubsub.grpc.pb.h"

namespace grpc {
namespace examples {
namespace pubsub {

class Publisher {
 public:
  Publisher(std::shared_ptr<Channel> channel);
  void Shutdown();

  Status CreateTopic(const grpc::string& topic);
  Status GetTopic(const grpc::string& topic);
  Status DeleteTopic(const grpc::string& topic);
  Status ListTopics(const grpc::string& project_id,
                    std::vector<grpc::string>* topics);

  Status Publish(const grpc::string& topic, const grpc::string& data);

 private:
  std::unique_ptr<tech::pubsub::PublisherService::Stub> stub_;
};

}  // namespace pubsub
}  // namespace examples
}  // namespace grpc

#endif  // GRPC_EXAMPLES_PUBSUB_PUBLISHER_H
