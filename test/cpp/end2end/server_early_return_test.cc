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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/string_ref_helper.h"

#include <gtest/gtest.h>

namespace grpc {
namespace testing {
namespace {

const char kServerReturnStatusCode[] = "server_return_status_code";
const char kServerDelayBeforeReturnUs[] = "server_delay_before_return_us";
const char kServerReturnAfterNReads[] = "server_return_after_n_reads";

class TestServiceImpl : public ::grpc::testing::EchoTestService::Service {
 public:
  // Unused methods are not implemented.

  Status RequestStream(ServerContext* context,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) override {
    int server_return_status_code =
        GetIntValueFromMetadata(context, kServerReturnStatusCode, 0);
    int server_delay_before_return_us =
        GetIntValueFromMetadata(context, kServerDelayBeforeReturnUs, 0);
    int server_return_after_n_reads =
        GetIntValueFromMetadata(context, kServerReturnAfterNReads, 0);

    EchoRequest request;
    while (server_return_after_n_reads--) {
      EXPECT_TRUE(reader->Read(&request));
    }

    response->set_message("response msg");

    gpr_sleep_until(gpr_time_add(
        gpr_now(GPR_CLOCK_MONOTONIC),
        gpr_time_from_micros(server_delay_before_return_us, GPR_TIMESPAN)));

    return Status(static_cast<StatusCode>(server_return_status_code), "");
  }

  Status BidiStream(
      ServerContext* context,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    int server_return_status_code =
        GetIntValueFromMetadata(context, kServerReturnStatusCode, 0);
    int server_delay_before_return_us =
        GetIntValueFromMetadata(context, kServerDelayBeforeReturnUs, 0);
    int server_return_after_n_reads =
        GetIntValueFromMetadata(context, kServerReturnAfterNReads, 0);

    EchoRequest request;
    EchoResponse response;
    while (server_return_after_n_reads--) {
      EXPECT_TRUE(stream->Read(&request));
      response.set_message(request.message());
      EXPECT_TRUE(stream->Write(response));
    }

    gpr_sleep_until(gpr_time_add(
        gpr_now(GPR_CLOCK_MONOTONIC),
        gpr_time_from_micros(server_delay_before_return_us, GPR_TIMESPAN)));

    return Status(static_cast<StatusCode>(server_return_status_code), "");
  }

  int GetIntValueFromMetadata(ServerContext* context, const char* key,
                              int default_value) {
    auto metadata = context->client_metadata();
    if (metadata.find(key) != metadata.end()) {
      std::istringstream iss(ToString(metadata.find(key)->second));
      iss >> default_value;
    }
    return default_value;
  }
};

class ServerEarlyReturnTest : public ::testing::Test {
 protected:
  ServerEarlyReturnTest() : picked_port_(0) {}

  void SetUp() override {
    int port = grpc_pick_unused_port_or_die();
    picked_port_ = port;
    server_address_ << "127.0.0.1:" << port;
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();

    channel_ =
        CreateChannel(server_address_.str(), InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  void TearDown() override {
    server_->Shutdown();
    if (picked_port_ > 0) {
      grpc_recycle_unused_port(picked_port_);
    }
  }

  // Client sends 20 requests and the server returns after reading 10 requests.
  // If return_cancel is true, server returns CANCELLED status. Otherwise it
  // returns OK.
  void DoBidiStream(bool return_cancelled) {
    EchoRequest request;
    EchoResponse response;
    ClientContext context;

    context.AddMetadata(kServerReturnAfterNReads, "10");
    if (return_cancelled) {
      // "1" means CANCELLED
      context.AddMetadata(kServerReturnStatusCode, "1");
    }
    context.AddMetadata(kServerDelayBeforeReturnUs, "10000");

    auto stream = stub_->BidiStream(&context);

    for (int i = 0; i < 20; i++) {
      request.set_message(grpc::string("hello") + grpc::to_string(i));
      bool write_ok = stream->Write(request);
      bool read_ok = stream->Read(&response);
      if (i < 10) {
        EXPECT_TRUE(write_ok);
        EXPECT_TRUE(read_ok);
        EXPECT_EQ(response.message(), request.message());
      } else {
        EXPECT_FALSE(read_ok);
      }
    }

    stream->WritesDone();
    EXPECT_FALSE(stream->Read(&response));

    Status s = stream->Finish();
    if (return_cancelled) {
      EXPECT_EQ(s.error_code(), StatusCode::CANCELLED);
    } else {
      EXPECT_TRUE(s.ok());
    }
  }

  void DoRequestStream(bool return_cancelled) {
    EchoRequest request;
    EchoResponse response;
    ClientContext context;

    context.AddMetadata(kServerReturnAfterNReads, "10");
    if (return_cancelled) {
      // "1" means CANCELLED
      context.AddMetadata(kServerReturnStatusCode, "1");
    }
    context.AddMetadata(kServerDelayBeforeReturnUs, "10000");

    auto stream = stub_->RequestStream(&context, &response);
    for (int i = 0; i < 20; i++) {
      request.set_message(grpc::string("hello") + grpc::to_string(i));
      bool written = stream->Write(request);
      if (i < 10) {
        EXPECT_TRUE(written);
      }
    }
    stream->WritesDone();
    Status s = stream->Finish();
    if (return_cancelled) {
      EXPECT_EQ(s.error_code(), StatusCode::CANCELLED);
    } else {
      EXPECT_TRUE(s.ok());
    }
  }

  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  TestServiceImpl service_;
  int picked_port_;
};

TEST_F(ServerEarlyReturnTest, BidiStreamEarlyOk) { DoBidiStream(false); }

TEST_F(ServerEarlyReturnTest, BidiStreamEarlyCancel) { DoBidiStream(true); }

TEST_F(ServerEarlyReturnTest, RequestStreamEarlyOK) { DoRequestStream(false); }
TEST_F(ServerEarlyReturnTest, RequestStreamEarlyCancel) {
  DoRequestStream(true);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
