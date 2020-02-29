/*
 *
 * Copyright 2019 gRPC authors.
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

#include "test/cpp/microbenchmarks/callback_test_service.h"

namespace grpc {
namespace testing {
namespace {

std::string ToString(const grpc::string_ref& r) {
  return std::string(r.data(), r.size());
}

int GetIntValueFromMetadataHelper(
    const char* key,
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    int default_value) {
  if (metadata.find(key) != metadata.end()) {
    std::istringstream iss(ToString(metadata.find(key)->second));
    iss >> default_value;
  }

  return default_value;
}

int GetIntValueFromMetadata(
    const char* key,
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    int default_value) {
  return GetIntValueFromMetadataHelper(key, metadata, default_value);
}
}  // namespace

experimental::ServerUnaryReactor* CallbackStreamingTestService::Echo(
    experimental::CallbackServerContext* context,
    const EchoRequest* /*request*/, EchoResponse* response) {
  int response_msgs_size = GetIntValueFromMetadata(
      kServerMessageSize, context->client_metadata(), 0);
  if (response_msgs_size > 0) {
    response->set_message(std::string(response_msgs_size, 'a'));
  } else {
    response->set_message("");
  }
  auto* reactor = context->DefaultReactor();
  reactor->Finish(::grpc::Status::OK);
  return reactor;
}

experimental::ServerBidiReactor<EchoRequest, EchoResponse>*
CallbackStreamingTestService::BidiStream(
    experimental::CallbackServerContext* context) {
  class Reactor
      : public experimental::ServerBidiReactor<EchoRequest, EchoResponse> {
   public:
    explicit Reactor(experimental::CallbackServerContext* context) {
      message_size_ = GetIntValueFromMetadata(kServerMessageSize,
                                              context->client_metadata(), 0);
      StartRead(&request_);
    }
    void OnDone() override {
      GPR_ASSERT(finished_);
      delete this;
    }
    void OnCancel() override {}
    void OnReadDone(bool ok) override {
      if (!ok) {
        // Stream is over
        Finish(::grpc::Status::OK);
        finished_ = true;
        return;
      }
      if (message_size_ > 0) {
        response_.set_message(std::string(message_size_, 'a'));
      } else {
        response_.set_message("");
      }
      StartWrite(&response_);
    }
    void OnWriteDone(bool ok) override {
      if (!ok) {
        gpr_log(GPR_ERROR, "Server write failed");
        return;
      }
      StartRead(&request_);
    }

   private:
    EchoRequest request_;
    EchoResponse response_;
    int message_size_;
    bool finished_{false};
  };

  return new Reactor(context);
}
}  // namespace testing
}  // namespace grpc
