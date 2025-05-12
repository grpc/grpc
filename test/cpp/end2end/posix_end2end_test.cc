//
//
// Copyright 2025 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//
#include <fcntl.h>
#include <grpc/grpc.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/async_generic_service.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/impl/proto_utils.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/slice.h>

#include <memory>
#include <thread>

#include "gtest/gtest.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"

namespace grpc {
namespace testing {
namespace {

void* tag(int i) { return reinterpret_cast<void*>(i); }

void verify_ok(CompletionQueue* cq, int i, bool expect_ok) {
  bool ok;
  void* got_tag;
  EXPECT_TRUE(cq->Next(&got_tag, &ok));
  EXPECT_EQ(expect_ok, ok);
  EXPECT_EQ(tag(i), got_tag);
}

class PosixEnd2endTest : public ::testing::Test {
 protected:
  PosixEnd2endTest() { create_sockets(fd_pair_); }

  void SetUp() override {
    std::unique_ptr<experimental::PassiveListener> passive_listener_;
    ServerBuilder builder;
    builder.RegisterAsyncGenericService(&generic_service_);
    auto creds_ = InsecureServerCredentials();
    builder.experimental().AddPassiveListener(creds_, passive_listener_);
    srv_cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
    auto status = passive_listener_->AcceptConnectedFd(fd_pair_[1]);
    EXPECT_EQ(status, absl::OkStatus());
  }

  void ResetStub() {
    grpc::ChannelArguments args_;
    std::shared_ptr<Channel> channel = grpc::experimental::CreateChannelFromFd(
        fd_pair_[0], InsecureChannelCredentials(), args_);
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
    generic_stub_ = std::make_unique<GenericStub>(channel);
  }

  void server_ok(int i) { verify_ok(srv_cq_.get(), i, true); }
  void client_ok(int i) { verify_ok(&cli_cq_, i, true); }
  void server_fail(int i) { verify_ok(srv_cq_.get(), i, false); }
  void client_fail(int i) { verify_ok(&cli_cq_, i, false); }

  void SendRpc(int num_rpcs) {
    SendRpc(num_rpcs, false, gpr_inf_future(GPR_CLOCK_MONOTONIC));
  }

  void SendRpc(int num_rpcs, bool check_deadline, gpr_timespec deadline) {
    const std::string kMethodName("/grpc.cpp.test.util.EchoTestService/Echo");
    for (int i = 0; i < num_rpcs; i++) {
      EchoRequest send_request;
      EchoRequest recv_request;
      EchoResponse send_response;
      EchoResponse recv_response;
      Status recv_status;

      ClientContext cli_ctx;
      GenericServerContext srv_ctx;
      GenericServerAsyncReaderWriter stream(&srv_ctx);

      // The string needs to be long enough to test heap-based slice.
      send_request.set_message("Hello world. Hello world. Hello world.");

      if (check_deadline) {
        cli_ctx.set_deadline(deadline);
      }

      // Rather than using the original kMethodName, make a short-lived
      // copy to also confirm that we don't refer to this object beyond
      // the initial call preparation
      const std::string* method_name = new std::string(kMethodName);

      std::unique_ptr<GenericClientAsyncReaderWriter> call =
          generic_stub_->PrepareCall(&cli_ctx, *method_name, &cli_cq_);

      delete method_name;  // Make sure that this is not needed after invocation

      std::thread request_call([this]() { server_ok(4); });
      call->StartCall(tag(1));
      client_ok(1);
      std::unique_ptr<ByteBuffer> send_buffer =
          SerializeToByteBuffer(&send_request);
      call->Write(*send_buffer, tag(2));
      // Send ByteBuffer can be destroyed after calling Write.
      send_buffer.reset();
      client_ok(2);
      call->WritesDone(tag(3));
      client_ok(3);

      generic_service_.RequestCall(&srv_ctx, &stream, srv_cq_.get(),
                                   srv_cq_.get(), tag(4));

      request_call.join();
      EXPECT_EQ(kMethodName, srv_ctx.method());

      if (check_deadline) {
        EXPECT_TRUE(gpr_time_similar(deadline, srv_ctx.raw_deadline(),
                                     gpr_time_from_millis(1000, GPR_TIMESPAN)));
      }

      ByteBuffer recv_buffer;
      stream.Read(&recv_buffer, tag(5));
      server_ok(5);
      EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_request));
      EXPECT_EQ(send_request.message(), recv_request.message());

      send_response.set_message(recv_request.message());
      send_buffer = SerializeToByteBuffer(&send_response);
      stream.Write(*send_buffer, tag(6));
      send_buffer.reset();
      server_ok(6);

      stream.Finish(Status::OK, tag(7));
      server_ok(7);

      recv_buffer.Clear();
      call->Read(&recv_buffer, tag(8));
      client_ok(8);
      EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_response));

      call->Finish(&recv_status, tag(9));
      client_ok(9);

      EXPECT_EQ(send_response.message(), recv_response.message());
      EXPECT_TRUE(recv_status.ok());
    }
  }

  CompletionQueue cli_cq_;
  std::unique_ptr<ServerCompletionQueue> srv_cq_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<grpc::GenericStub> generic_stub_;
  std::unique_ptr<Server> server_;
  AsyncGenericService generic_service_;
  int fd_pair_[2];

  static void create_sockets(int sv[2]) {
    int flags;
    grpc_create_socketpair_if_unix(sv);
    flags = fcntl(sv[0], F_GETFL, 0);
    CHECK_EQ(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK), 0);
    flags = fcntl(sv[1], F_GETFL, 0);
    CHECK_EQ(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK), 0);
    CHECK(grpc_set_socket_no_sigpipe_if_possible(sv[0]) == absl::OkStatus());
    CHECK(grpc_set_socket_no_sigpipe_if_possible(sv[1]) == absl::OkStatus());
  }
};

TEST_F(PosixEnd2endTest, SimpleRpc) {
  ResetStub();
  SendRpc(1);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
