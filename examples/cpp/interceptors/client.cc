/*
 *
 * Copyright 2018 gRPC authors.
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

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "caching_interceptor.h"

#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/keyvaluestore.grpc.pb.h"
#else
#include "keyvaluestore.grpc.pb.h"
#endif

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using keyvaluestore::KeyValueStore;
using keyvaluestore::Request;
using keyvaluestore::Response;

// Requests each key in the vector and displays the key and its corresponding
// value as a pair.
class KeyValueStoreClient : public grpc::ClientBidiReactor<Request, Response> {
 public:
  KeyValueStoreClient(std::shared_ptr<Channel> channel,
                      std::vector<std::string> keys)
      : stub_(KeyValueStore::NewStub(channel)), keys_(std::move(keys)) {
    stub_->async()->GetValues(&context_, this);
    assert(!keys_.empty());
    request_.set_key(keys_[0]);
    StartWrite(&request_);
    StartCall();
  }

  void OnReadDone(bool ok) override {
    if (ok) {
      std::cout << request_.key() << " : " << response_.value() << std::endl;
      if (++counter_ < keys_.size()) {
        request_.set_key(keys_[counter_]);
        StartWrite(&request_);
      } else {
        StartWritesDone();
      }
    }
  }

  void OnWriteDone(bool ok) override {
    if (ok) {
      StartRead(&response_);
    }
  }

  void OnDone(const grpc::Status& status) override {
    if (!status.ok()) {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      std::cout << "RPC failed";
    }
    std::unique_lock<std::mutex> l(mu_);
    done_ = true;
    cv_.notify_all();
  }

  void Await() {
    std::unique_lock<std::mutex> l(mu_);
    while (!done_) {
      cv_.wait(l);
    }
  }

 private:
  std::unique_ptr<KeyValueStore::Stub> stub_;
  std::vector<std::string> keys_;
  size_t counter_ = 0;
  ClientContext context_;
  bool done_ = false;
  Request request_;
  Response response_;
  std::mutex mu_;
  std::condition_variable cv_;
};

int main(int argc, char** argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint (in this case,
  // localhost at port 50051). We indicate that the channel isn't authenticated
  // (use of InsecureChannelCredentials()).
  // In this example, we are using a cache which has been added in as an
  // interceptor.
  grpc::ChannelArguments args;
  std::vector<
      std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
      interceptor_creators;
  interceptor_creators.push_back(std::make_unique<CachingInterceptorFactory>());
  auto channel = grpc::experimental::CreateCustomChannelWithInterceptors(
      "localhost:50051", grpc::InsecureChannelCredentials(), args,
      std::move(interceptor_creators));
  std::vector<std::string> keys = {"key1", "key2", "key3", "key4",
                                   "key5", "key1", "key2", "key4"};
  KeyValueStoreClient client(channel, keys);
  client.Await();
  return 0;
}
