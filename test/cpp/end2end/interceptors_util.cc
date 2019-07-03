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

#include "test/cpp/end2end/interceptors_util.h"

namespace grpc {
namespace testing {

std::atomic<int> DummyInterceptor::num_times_run_;
std::atomic<int> DummyInterceptor::num_times_run_reverse_;
std::atomic<int> DummyInterceptor::num_times_cancel_;

void MakeCall(const std::shared_ptr<Channel>& channel) {
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  req.mutable_param()->set_echo_metadata(true);
  ctx.AddMetadata("testkey", "testvalue");
  req.set_message("Hello");
  EchoResponse resp;
  Status s = stub->Echo(&ctx, req, &resp);
  EXPECT_EQ(s.ok(), true);
  EXPECT_EQ(resp.message(), "Hello");
}

void MakeClientStreamingCall(const std::shared_ptr<Channel>& channel) {
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  req.mutable_param()->set_echo_metadata(true);
  ctx.AddMetadata("testkey", "testvalue");
  req.set_message("Hello");
  EchoResponse resp;
  string expected_resp = "";
  auto writer = stub->RequestStream(&ctx, &resp);
  for (int i = 0; i < kNumStreamingMessages; i++) {
    writer->Write(req);
    expected_resp += "Hello";
  }
  writer->WritesDone();
  Status s = writer->Finish();
  EXPECT_EQ(s.ok(), true);
  EXPECT_EQ(resp.message(), expected_resp);
}

void MakeServerStreamingCall(const std::shared_ptr<Channel>& channel) {
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  req.mutable_param()->set_echo_metadata(true);
  ctx.AddMetadata("testkey", "testvalue");
  req.set_message("Hello");
  EchoResponse resp;
  string expected_resp = "";
  auto reader = stub->ResponseStream(&ctx, req);
  int count = 0;
  while (reader->Read(&resp)) {
    EXPECT_EQ(resp.message(), "Hello");
    count++;
  }
  ASSERT_EQ(count, kNumStreamingMessages);
  Status s = reader->Finish();
  EXPECT_EQ(s.ok(), true);
}

void MakeBidiStreamingCall(const std::shared_ptr<Channel>& channel) {
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  EchoResponse resp;
  ctx.AddMetadata("testkey", "testvalue");
  auto stream = stub->BidiStream(&ctx);
  for (auto i = 0; i < kNumStreamingMessages; i++) {
    req.set_message("Hello" + std::to_string(i));
    stream->Write(req);
    stream->Read(&resp);
    EXPECT_EQ(req.message(), resp.message());
  }
  ASSERT_TRUE(stream->WritesDone());
  Status s = stream->Finish();
  EXPECT_EQ(s.ok(), true);
}

void MakeCallbackCall(const std::shared_ptr<Channel>& channel) {
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  req.mutable_param()->set_echo_metadata(true);
  ctx.AddMetadata("testkey", "testvalue");
  req.set_message("Hello");
  EchoResponse resp;
  stub->experimental_async()->Echo(&ctx, &req, &resp,
                                   [&resp, &mu, &done, &cv](Status s) {
                                     // gpr_log(GPR_ERROR, "got the callback");
                                     EXPECT_EQ(s.ok(), true);
                                     EXPECT_EQ(resp.message(), "Hello");
                                     std::lock_guard<std::mutex> l(mu);
                                     done = true;
                                     cv.notify_one();
                                   });
  std::unique_lock<std::mutex> l(mu);
  while (!done) {
    cv.wait(l);
  }
}

bool CheckMetadata(const std::multimap<grpc::string_ref, grpc::string_ref>& map,
                   const string& key, const string& value) {
  for (const auto& pair : map) {
    if (pair.first.starts_with(key) && pair.second.starts_with(value)) {
      return true;
    }
  }
  return false;
}

bool CheckMetadata(const std::multimap<grpc::string, grpc::string>& map,
                   const string& key, const string& value) {
  for (const auto& pair : map) {
    if (pair.first == key && pair.second == value) {
      return true;
    }
  }
  return false;
}

std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
CreateDummyClientInterceptors() {
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  // Add 20 dummy interceptors before hijacking interceptor
  creators.reserve(20);
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  return creators;
}

}  // namespace testing
}  // namespace grpc
