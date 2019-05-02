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

#ifndef TEST_CPP_MICROBENCHMARKS_CALLBACK_TEST_SERVICE_H
#define TEST_CPP_MICROBENCHMARKS_CALLBACK_TEST_SERVICE_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <sstream>
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/util/string_ref_helper.h"

namespace grpc {
namespace testing {

const char* const kServerFinishAfterNReads = "server_finish_after_n_reads";
const char* const kServerMessageSize = "server_message_size";

class CallbackStreamingTestService
    : public EchoTestService::ExperimentalCallbackService {
 public:
  CallbackStreamingTestService() {}
  void Echo(ServerContext* context, const EchoRequest* request,
            EchoResponse* response,
            experimental::ServerCallbackRpcController* controller) override;

  experimental::ServerBidiReactor<EchoRequest, EchoResponse>* BidiStream()
      override;
};

class BidiClient
    : public grpc::experimental::ClientBidiReactor<EchoRequest, EchoResponse> {
 public:
  BidiClient(EchoTestService::Stub* stub, EchoRequest* request,
             EchoResponse* response, ClientContext* context,
             int num_msgs_to_send)
      : request_{request},
        response_{response},
        context_{context},
        msgs_to_send_{num_msgs_to_send} {
    stub->experimental_async()->BidiStream(context_, this);
    MaybeWrite();
    StartRead(response_);
    StartCall();
  }

  void OnReadDone(bool ok) override {
    if (!ok) {
      return;
    }
    if (reads_complete_ < msgs_to_send_) {
      reads_complete_++;
      StartRead(response_);
    }
  }

  void OnWriteDone(bool ok) override {
    if (!ok) {
      return;
    }
    writes_complete_++;
    MaybeWrite();
  }

  void OnDone(const Status& s) override {
    GPR_ASSERT(s.ok());
    std::unique_lock<std::mutex> l(mu_);
    done_ = true;
    cv_.notify_one();
  }

  void Await() {
    std::unique_lock<std::mutex> l(mu_);
    while (!done_) {
      cv_.wait(l);
    }
  }

 private:
  void MaybeWrite() {
    if (writes_complete_ == msgs_to_send_) {
      StartWritesDone();
    } else {
      StartWrite(request_);
    }
  }

  EchoRequest* request_;
  EchoResponse* response_;
  ClientContext* context_;
  int reads_complete_{0};
  int writes_complete_{0};
  const int msgs_to_send_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool done_ = false;
};

}  // namespace testing
}  // namespace grpc
#endif  // TEST_CPP_MICROBENCHMARKS_CALLBACK_TEST_SERVICE_H
