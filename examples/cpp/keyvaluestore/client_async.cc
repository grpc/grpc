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

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "caching_interceptor.h"

#ifdef BAZEL_BUILD
#include "examples/protos/keyvaluestore.grpc.pb.h"
#else
#include "keyvaluestore.grpc.pb.h"
#endif

using grpc::Channel;
using grpc::ClientAsyncReaderWriter;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using keyvaluestore::KeyValueStore;
using keyvaluestore::Request;
using keyvaluestore::Response;

class KeyValueStoreClient {
 public:
  KeyValueStoreClient(std::shared_ptr<Channel> channel)
      : stub_(KeyValueStore::NewStub(channel)), index_(0) {}

  void Proceed(void* tag) {
    if (tag == (void*)Event::START_CALL) {
      stream_->Read(&response_, (void*)Event::READ);

      if (index_ < keys_.size()) {
        Request request;
        request.set_key(keys_[index_++]);
        stream_->Write(request, (void*)Event::WRITE);
      }
    } else if (tag == (void*)Event::READ) {
      std::cout << response_.value() << std::endl;
      stream_->Read(&response_, (void*)Event::READ);

    } else if (tag == (void*)Event::WRITE) {
      if (index_ < keys_.size()) {
        Request request;
        request.set_key(keys_[index_++]);
        stream_->Write(request, (void*)Event::WRITE);
      } else {
        stream_->WritesDone((void*)Event::WRITES_DONE);
      }

    } else if (tag == (void*)Event::WRITES_DONE) {
    } else if (tag == (void*)Event::FINISH) {
      cq_.Shutdown();
    } else {
      std::cout << "Unexpected tag received!" << std::endl;
      std::abort();
    }
  }

  void GetValues(const std::vector<std::string>& keys) {
    keys_ = keys;

    stream_ = stub_->PrepareAsyncGetValues(&context_, &cq_);
    stream_->StartCall((void*)Event::START_CALL);
  }

  // Loop while listening for completed responses.
  void AsyncCompleteRpc() {
    void* tag;
    bool ok = false;

    // Block until the next result is available in the completion queue "cq".
    while (cq_.Next(&tag, &ok)) {
      // Verify that the request was completed successfully. Note that "ok"
      // corresponds solely to the request for updates introduced by
      // Finish().
      if (ok) {
        // The tag in this example is the enum Event
        Proceed(tag);
      } else {
        stream_->Finish(&status_, (void*)Event::FINISH);
      }
    }
  }

 private:
  std::unique_ptr<KeyValueStore::Stub> stub_;

  // Context for the client. It could be used to convey extra information to
  // the server and/or tweak certain RPC behaviors.
  ClientContext context_;

  // The producer-consumer queue we use to communicate asynchronously with the
  // gRPC runtime.
  CompletionQueue cq_;

  // Storage for the status of the RPC upon completion.
  Status status_;

  std::unique_ptr<ClientAsyncReaderWriter<Request, Response>> stream_;

  Response response_;

  std::vector<std::string> keys_;
  int index_;

  enum Event { START_CALL, READ, WRITE, WRITES_DONE, FINISH };
};

int main(int argc, char** argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint (in this case,
  // localhost at port 50051). We indicate that the channel isn't authenticated
  // (use of InsecureChannelCredentials()).
  KeyValueStoreClient client(grpc::CreateChannel(
      "localhost:50051", grpc::InsecureChannelCredentials()));

  std::thread thread_ =
      std::thread(&KeyValueStoreClient::AsyncCompleteRpc, &client);

  std::vector<std::string> keys = {"key1", "key2", "key3", "key4",
                                   "key5", "key1", "key2", "key4"};
  client.GetValues(keys);

  thread_.join();

  return 0;
}
