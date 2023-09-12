#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
// #include <grpcpp/security/audit_logging.h>
// #include <grpcpp/security/authorization_policy_provider.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/iomgr/load_file.h"
// #include "src/core/lib/security/authorization/audit_logging.h"
// #include
// "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"
// #include "src/core/lib/security/credentials/fake/fake_credentials.h"
// #include "src/cpp/client/secure_credentials.h"
// #include "src/cpp/server/secure_server_credentials.h"
// #include "src/proto/grpc/testing/echo.grpc.pb.h"
// #include "test/core/util/audit_logging_utils.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {
namespace {

constexpr char kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
constexpr char kServerCertPath[] = "src/core/tsi/test_creds/server1.pem";
constexpr char kServerKeyPath[] = "src/core/tsi/test_creds/server1.key";
constexpr char kClientCertPath[] = "src/core/tsi/test_creds/client.pem";
constexpr char kClientKeyPath[] = "src/core/tsi/test_creds/client.key";
constexpr char kMessage[] = "Hello";

std::string ReadFile(const char* file_path) {
  grpc_slice slice;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("load_file", grpc_load_file(file_path, 0, &slice)));
  std::string file_contents(grpc_core::StringViewFromSlice(slice));
  grpc_slice_unref(slice);
  return file_contents;
}

class GrpcResumptionTest : public ::testing::Test {
 protected:
  void RunServer() {
    std::string root_cert = ReadFile(kCaCertPath);
    grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair = {
        ReadFile(kServerKeyPath), ReadFile(kServerCertPath)};
    grpc::SslServerCredentialsOptions sslOpts;
    sslOpts.pem_key_cert_pairs.push_back(key_cert_pair);
    sslOpts.pem_root_certs = root_cert;

    grpc::ServerBuilder builder;
    TestServiceImpl service_;

    builder.AddListeningPort(server_addr_, grpc::SslServerCredentials(sslOpts));
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    std::cout << "GREG: server waiting\n";
    server_->Wait();
  }

  //   void TearDown() override {
  //     if (server_ != nullptr) {
  //       server_->Shutdown();
  //       server_thread_->join();
  //     }
  //     server_thread_ = nullptr;
  //   }

  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
  std::thread* server_thread_;
  std::string server_addr_;
};

TEST_F(GrpcResumptionTest, ConcurrentResumption) {
  std::cout << "GREG: Test Start\n";
  int port = grpc_pick_unused_port_or_die();
  std::ostringstream addr;
  addr << "localhost:" << port;
  server_addr_ = addr.str();
  server_thread_ = new std::thread([&]() { RunServer(); });

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(16);
  grpc_arg args[] = {grpc_ssl_session_cache_create_channel_arg(cache)};

  std::string root_cert = ReadFile(kCaCertPath);
  std::string client_key = ReadFile(kClientKeyPath);
  std::string client_cert = ReadFile(kClientCertPath);
  grpc::SslCredentialsOptions sslOpts;
  sslOpts.pem_root_certs = root_cert;
  sslOpts.pem_private_key = client_key;
  sslOpts.pem_cert_chain = client_cert;

  grpc_arg client_args_arr[] = {
      grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
          const_cast<char*>("foo.test.google.fr")),
      grpc_ssl_session_cache_create_channel_arg(cache),
  };

  grpc_channel_args* client_args = grpc_channel_args_copy_and_add(
      nullptr, client_args_arr, GPR_ARRAY_SIZE(args));

  ChannelArguments channel_args;
  channel_args.SetChannelArgs(client_args);
  std::shared_ptr<Channel> channel = grpc::CreateCustomChannel(
      server_addr_, grpc::SslCredentials(sslOpts), channel_args);

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse* response = nullptr;
  request.set_message(kMessage);
  ClientContext context;
  //   context.AddMetadata("key-foo", "foo2");
  context.set_deadline(grpc_timeout_milliseconds_to_deadline(100));
  std::cout << "GREG: before stub echo\n";
  auto result = stub->Echo(&context, request, response);
  std::cout << "GREG: after stub echo\n";
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(response->message(), kMessage);

  if (server_ != nullptr) {
    server_->Shutdown();
    server_thread_->join();
    delete server_thread_;
  }

  //   grpc_channel_args_destroy(client_args);
  //   grpc_ssl_session_cache_destroy(cache);
}  // namespace

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}