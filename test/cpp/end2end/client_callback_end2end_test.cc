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

#include <functional>
#include <mutex>
#include <thread>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/impl/codegen/proto_utils.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/client_callback.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"
#include "test/cpp/util/string_ref_helper.h"

#include <gtest/gtest.h>

namespace grpc {
namespace testing {
namespace {

class TestScenario {
 public:
  TestScenario(bool serve_callback) : callback_server(serve_callback) {}
  void Log() const;
  bool callback_server;
};

static std::ostream& operator<<(std::ostream& out,
                                const TestScenario& scenario) {
  return out << "TestScenario{callback_server="
             << (scenario.callback_server ? "true" : "false") << "}";
}

void TestScenario::Log() const {
  std::ostringstream out;
  out << *this;
  gpr_log(GPR_DEBUG, "%s", out.str().c_str());
}

class ClientCallbackEnd2endTest
    : public ::testing::TestWithParam<TestScenario> {
 protected:
  ClientCallbackEnd2endTest() { GetParam().Log(); }

  void SetUp() override {
    ServerBuilder builder;

    if (!GetParam().callback_server) {
      builder.RegisterService(&service_);
    } else {
      builder.RegisterService(&callback_service_);
    }

    server_ = builder.BuildAndStart();
    is_server_started_ = true;
  }

  void ResetStub() {
    ChannelArguments args;
    channel_ = server_->InProcessChannel(args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
    generic_stub_.reset(new GenericStub(channel_));
  }

  void TearDown() override {
    if (is_server_started_) {
      server_->Shutdown();
    }
  }

  void SendRpcs(int num_rpcs, bool with_binary_metadata) {
    grpc::string test_string("");
    for (int i = 0; i < num_rpcs; i++) {
      EchoRequest request;
      EchoResponse response;
      ClientContext cli_ctx;

      test_string += "Hello world. ";
      request.set_message(test_string);
      grpc::string val;
      if (with_binary_metadata) {
        request.mutable_param()->set_echo_metadata(true);
        char bytes[8] = {'\0', '\1', '\2', '\3',
                         '\4', '\5', '\6', static_cast<char>(i)};
        val = grpc::string(bytes, 8);
        cli_ctx.AddMetadata("custom-bin", val);
      }

      cli_ctx.set_compression_algorithm(GRPC_COMPRESS_GZIP);

      std::mutex mu;
      std::condition_variable cv;
      bool done = false;
      stub_->experimental_async()->Echo(
          &cli_ctx, &request, &response,
          [&cli_ctx, &request, &response, &done, &mu, &cv, val,
           with_binary_metadata](Status s) {
            GPR_ASSERT(s.ok());

            EXPECT_EQ(request.message(), response.message());
            if (with_binary_metadata) {
              EXPECT_EQ(
                  1u, cli_ctx.GetServerTrailingMetadata().count("custom-bin"));
              EXPECT_EQ(val, ToString(cli_ctx.GetServerTrailingMetadata()
                                          .find("custom-bin")
                                          ->second));
            }
            std::lock_guard<std::mutex> l(mu);
            done = true;
            cv.notify_one();
          });
      std::unique_lock<std::mutex> l(mu);
      while (!done) {
        cv.wait(l);
      }
    }
  }

  void SendRpcsGeneric(int num_rpcs, bool maybe_except) {
    const grpc::string kMethodName("/grpc.testing.EchoTestService/Echo");
    grpc::string test_string("");
    for (int i = 0; i < num_rpcs; i++) {
      EchoRequest request;
      std::unique_ptr<ByteBuffer> send_buf;
      ByteBuffer recv_buf;
      ClientContext cli_ctx;

      test_string += "Hello world. ";
      request.set_message(test_string);
      send_buf = SerializeToByteBuffer(&request);

      std::mutex mu;
      std::condition_variable cv;
      bool done = false;
      generic_stub_->experimental().UnaryCall(
          &cli_ctx, kMethodName, send_buf.get(), &recv_buf,
          [&request, &recv_buf, &done, &mu, &cv, maybe_except](Status s) {
            GPR_ASSERT(s.ok());

            EchoResponse response;
            EXPECT_TRUE(ParseFromByteBuffer(&recv_buf, &response));
            EXPECT_EQ(request.message(), response.message());
            std::lock_guard<std::mutex> l(mu);
            done = true;
            cv.notify_one();
#if GRPC_ALLOW_EXCEPTIONS
            if (maybe_except) {
              throw - 1;
            }
#else
            GPR_ASSERT(!maybe_except);
#endif
          });
      std::unique_lock<std::mutex> l(mu);
      while (!done) {
        cv.wait(l);
      }
    }
  }

  bool is_server_started_;
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<grpc::GenericStub> generic_stub_;
  TestServiceImpl service_;
  CallbackTestServiceImpl callback_service_;
  std::unique_ptr<Server> server_;
};

TEST_P(ClientCallbackEnd2endTest, SimpleRpc) {
  ResetStub();
  SendRpcs(1, false);
}

TEST_P(ClientCallbackEnd2endTest, SequentialRpcs) {
  ResetStub();
  SendRpcs(10, false);
}

TEST_P(ClientCallbackEnd2endTest, SequentialRpcsWithVariedBinaryMetadataValue) {
  ResetStub();
  SendRpcs(10, true);
}

TEST_P(ClientCallbackEnd2endTest, SequentialGenericRpcs) {
  ResetStub();
  SendRpcsGeneric(10, false);
}

#if GRPC_ALLOW_EXCEPTIONS
TEST_P(ClientCallbackEnd2endTest, ExceptingRpc) {
  ResetStub();
  SendRpcsGeneric(10, true);
}
#endif

TEST_P(ClientCallbackEnd2endTest, MultipleRpcsWithVariedBinaryMetadataValue) {
  ResetStub();
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([this] { SendRpcs(10, true); });
  }
  for (int i = 0; i < 10; ++i) {
    threads[i].join();
  }
}

TEST_P(ClientCallbackEnd2endTest, MultipleRpcs) {
  ResetStub();
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([this] { SendRpcs(10, false); });
  }
  for (int i = 0; i < 10; ++i) {
    threads[i].join();
  }
}

TEST_P(ClientCallbackEnd2endTest, CancelRpcBeforeStart) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");
  context.TryCancel();

  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  stub_->experimental_async()->Echo(
      &context, &request, &response, [&response, &done, &mu, &cv](Status s) {
        EXPECT_EQ("", response.message());
        EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
        std::lock_guard<std::mutex> l(mu);
        done = true;
        cv.notify_one();
      });
  std::unique_lock<std::mutex> l(mu);
  while (!done) {
    cv.wait(l);
  }
}

TestScenario scenarios[] = {TestScenario{false}, TestScenario{true}};

INSTANTIATE_TEST_CASE_P(ClientCallbackEnd2endTest, ClientCallbackEnd2endTest,
                        ::testing::ValuesIn(scenarios));

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
