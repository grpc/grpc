#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/synchronization/notification.h"

#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/iomgr/load_file.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
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
  void RunServer(absl::Notification* notification) {
    std::string root_cert = ReadFile(kCaCertPath);
    grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair = {
        ReadFile(kServerKeyPath), ReadFile(kServerCertPath)};
    grpc::SslServerCredentialsOptions ssl_options;
    ssl_options.pem_key_cert_pairs.push_back(key_cert_pair);
    ssl_options.pem_root_certs = root_cert;
    ssl_options.force_client_auth = true;

    grpc::ServerBuilder builder;
    TestServiceImpl service_;

    builder.AddListeningPort(server_addr_,
                             grpc::SslServerCredentials(ssl_options));
    builder.RegisterService("foo.test.google.fr", &service_);
    server_ = builder.BuildAndStart();
    notification->Notify();
    server_->Wait();
  }

  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
  std::thread* server_thread_;
  std::string server_addr_;
};

void DoRpc(std::string server_addr, const SslCredentialsOptions& ssl_options,
           grpc_ssl_session_cache* cache, bool expect_session_reuse) {
  ChannelArguments channel_args;
  channel_args.SetPointer(std::string(GRPC_SSL_SESSION_CACHE_ARG), cache);
  channel_args.SetSslTargetNameOverride("foo.test.google.fr");

  std::shared_ptr<Channel> channel = grpc::CreateCustomChannel(
      server_addr, grpc::SslCredentials(ssl_options), channel_args);

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message(kMessage);
  ClientContext context;
  context.set_deadline(grpc_timeout_milliseconds_to_deadline(1000));
  grpc::Status result = stub->Echo(&context, request, &response);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(response.message(), kMessage);
  std::shared_ptr<const AuthContext> auth_context = context.auth_context();
  std::vector<grpc::string_ref> properties =
      auth_context->FindPropertyValues(GRPC_SSL_SESSION_REUSED_PROPERTY);
  ASSERT_EQ(1u, properties.size());
  if (expect_session_reuse) {
    EXPECT_EQ("true", ToString(properties[0]));
  } else {
    EXPECT_EQ("false", ToString(properties[0]));
  }
}

TEST_F(GrpcResumptionTest, ConcurrentResumption) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() { RunServer(&notification); });
  notification.WaitForNotification();

  std::string root_cert = ReadFile(kCaCertPath);
  std::string client_key = ReadFile(kClientKeyPath);
  std::string client_cert = ReadFile(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(16);

  DoRpc(server_addr_, ssl_options, cache, false);
  std::vector<std::thread*> threads;
  for (int i = 0; i < 10; i++) {
    threads.push_back(new std::thread(
        [&]() { DoRpc(server_addr_, ssl_options, cache, true); }));
  }
  for (int i = 0; i < 10; i++) {
    threads[i]->join();
  }
  for (int i = 0; i < 10; i++) {
    delete threads[i];
  }

  if (server_ != nullptr) {
    server_->Shutdown();
    server_thread_->join();
    delete server_thread_;
  }
  grpc_ssl_session_cache_destroy(cache);
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