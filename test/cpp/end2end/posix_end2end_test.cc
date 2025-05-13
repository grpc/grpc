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
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"
#include "test/cpp/util/test_credentials_provider.h"

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

enum class CredentialsType : std::uint8_t {
  Insecure,
  Tls,
  Alts,
  GoogleDefault,
};

const char* GetCredentialsTypeLiteral(CredentialsType type) {
  switch (type) {
    case CredentialsType::Insecure:
      return kInsecureCredentialsType;
    case CredentialsType::Tls:
      return kTlsCredentialsType;
    case CredentialsType::Alts:
      return kAltsCredentialsType;
    case CredentialsType::GoogleDefault:
      return kGoogleDefaultCredentialsType;
    default:
      return kInsecureCredentialsType;
  }
}

constexpr char kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
constexpr char kServerCertPath[] = "src/core/tsi/test_creds/server1.pem";
constexpr char kServerKeyPath[] = "src/core/tsi/test_creds/server1.key";
constexpr char kClientCertPath[] =
    "src/core/tsi/test_creds/client-with-spiffe.pem";
constexpr char kClientKeyPath[] =
    "src/core/tsi/test_creds/client-with-spiffe.key";

class PosixEnd2endTest : public ::testing::Test {
 protected:
  PosixEnd2endTest() { create_fds(fd_pair_); }

  void SetUp() override {}

  std::shared_ptr<ChannelCredentials> get_channel_creds() {
    return InsecureChannelCredentials();
    std::vector<experimental::IdentityKeyCertPair>
        channel_identity_key_cert_pairs = {
            {grpc_core::testing::GetFileContents(kClientKeyPath),
             grpc_core::testing::GetFileContents(kClientCertPath)}};
    grpc::experimental::TlsChannelCredentialsOptions channel_options;
    channel_options.set_certificate_provider(
        std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
            grpc_core::testing::GetFileContents(kCaCertPath),
            channel_identity_key_cert_pairs));
    channel_options.watch_identity_key_cert_pairs();
    channel_options.watch_root_certs();
    return grpc::experimental::TlsCredentials(channel_options);
  }

  std::shared_ptr<ServerCredentials> get_server_creds() {
    return InsecureServerCredentials();
    std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
    std::string identity_cert =
        grpc_core::testing::GetFileContents(kServerCertPath);
    std::string private_key =
        grpc_core::testing::GetFileContents(kServerKeyPath);
    std::vector<experimental::IdentityKeyCertPair>
        server_identity_key_cert_pairs = {{private_key, identity_cert}};
    grpc::experimental::TlsServerCredentialsOptions server_options(
        std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
            root_cert, server_identity_key_cert_pairs));
    server_options.watch_root_certs();
    server_options.watch_identity_key_cert_pairs();
    server_options.set_cert_request_type(
        GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
    return grpc::experimental::TlsServerCredentials(server_options);
  }

  void SetUpServer() {
    shut_down_ = false;
    std::unique_ptr<experimental::PassiveListener> passive_listener_;
    ServerBuilder builder;
    builder.RegisterAsyncGenericService(&generic_service_);
    builder.experimental().AddPassiveListener(get_server_creds(), passive_listener_);
    srv_cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
    auto status = passive_listener_->AcceptConnectedFd(fd_pair_[1]);
    EXPECT_EQ(status, absl::OkStatus());
  }

  void ResetStub() {
    grpc::ChannelArguments args_;
    std::shared_ptr<Channel> channel = grpc::experimental::CreateChannelFromFd(
        fd_pair_[0],
        get_channel_creds(),
        args_);
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
    generic_stub_ = std::make_unique<GenericStub>(channel);
  }

  void ShutDownServerAndCQs() {
    if (!shut_down_) {
      server_->Shutdown();
      void* ignored_tag;
      bool ignored_ok;
      cli_cq_.Shutdown();
      srv_cq_->Shutdown();
      while (cli_cq_.Next(&ignored_tag, &ignored_ok)) {
      }
      while (srv_cq_->Next(&ignored_tag, &ignored_ok)) {
      }
      shut_down_ = true;
    }
  }
  void TearDown() override { ShutDownServerAndCQs(); }

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
  const char* credentials_type_;
  bool shut_down_;
  void SetCredentialsType(CredentialsType type) {
    credentials_type_ = GetCredentialsTypeLiteral(type);
  }

  static void create_fds(int sv[2]) {
    // Create a listening socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
      // std::cerr << "Error creating server socket" << std::endl;
      return;
    }

    // Bind the socket to an address and port
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(50052);
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) <
        0) {
      // std::cerr << "Error binding server socket" << std::endl;
      close(server_fd);
      return;
    }

    // Listen for incoming connections
    if (listen(server_fd, 128) < 0) {
      // std::cerr << "Error listening on server socket" << std::endl;
      close(server_fd);
      return;
    }

    // std::cout << "Server listening on port 50052..." << std::endl;

    // Accept an incoming connection
   
    int new_socket_fd;
    std::thread t([&new_socket_fd, server_fd]() {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        new_socket_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (new_socket_fd < 0) {
            std::cerr << "Accept failed\n";
        } else {
            std::cout << "Connection accepted on socket: " << new_socket_fd << "\n";
        }
    });

    // 1. Create a socket
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
      //  std::cerr << "Error creating socket" << std::endl;
      return;
    }

    // // 2. Connect to the server using the socket
    // sockaddr_in server_addr;
    // server_addr.sin_family = AF_INET;
    // server_addr.sin_port = htons(50052);
    // if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
    //   //  std::cerr << "Invalid address/address not supported" << std::endl;
    //   close(client_fd);
    //   return;
    // }

    if (connect(client_fd, (struct sockaddr*)&server_addr,
                sizeof(server_addr)) < 0) {
      //  std::cerr << "Error connecting to server" << std::endl;
      close(client_fd);
      return;
    }

    t.join();

    if (new_socket_fd < 0) {
      // std::cerr << "Error accepting connection" << std::endl;
      close(server_fd);
      return;
    }
    sv[0] = client_fd;
    sv[1] = new_socket_fd;
  }

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
  SetCredentialsType(CredentialsType::Tls);
  SetUpServer();
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
