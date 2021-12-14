#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/channelz_service_plugin.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_utils.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::experimental::FileWatcherCertificateProvider;
using ::grpc::experimental::TlsChannelCredentialsOptions;
using ::grpc::experimental::TlsServerCredentialsOptions;
using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;
using grpc::testing::EchoTestService;

namespace grpc {
namespace testing {
namespace {

constexpr char kCredentialsDir[] = "src/core/tsi/test_creds/crl_supported/";

void CallEchoRPC(const std::string& server_addr, bool revoked_client_certs,
                 bool revoked_server_certs) {
  std::string certificate_file;
  std::string key_file;
  if (revoked_client_certs) {
    certificate_file = absl::StrCat(kCredentialsDir, "/revoked.pem");
    key_file = absl::StrCat(kCredentialsDir, "/revoked.key");
  } else {
    certificate_file = absl::StrCat(kCredentialsDir, "/valid.pem");
    key_file = absl::StrCat(kCredentialsDir, "/valid.key");
  }
  const std::string ca_bundle_file = absl::StrCat(kCredentialsDir, "/ca.pem");

  auto certificate_provider = std::make_shared<FileWatcherCertificateProvider>(
      key_file, certificate_file, ca_bundle_file,
      /*refresh_interval_sec=*/10);
  TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.watch_identity_key_cert_pairs();
  auto channel_creds = grpc::experimental::TlsCredentials(options);
  grpc::ChannelArguments args;
  if (revoked_server_certs) {
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, "revoked");
  } else {
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, "valid");
  }
  auto channel = grpc::CreateCustomChannel(server_addr, channel_creds, args);
  std::unique_ptr<EchoTestService::Stub> stub =
      EchoTestService::NewStub(channel);
  EchoRequest request;
  request.set_message("This is a test.");
  EchoResponse reply;
  std::cout << "Sending test message" << std::endl;
  ClientContext context;
  Status s = stub->Echo(&context, request, &reply);
  EXPECT_TRUE(s.ok()) << "s.error_message() = " << s.error_message();
}

class TestServerWrapper {
 public:
  TestServerWrapper()
      : server_address_("localhost:" +
                        std::to_string(grpc_pick_unused_port_or_die())) {}

  void Start(std::string certificate_file = absl::StrCat(kCredentialsDir,
                                                         "/valid.pem"),
             std::string key_file = absl::StrCat(kCredentialsDir, "/valid.key"),
             std::string ca_bundle_file = absl::StrCat(kCredentialsDir,
                                                       "/ca.pem")) {
    auto certificate_provider =
        std::make_shared<FileWatcherCertificateProvider>(
            key_file, certificate_file, ca_bundle_file,
            /*refresh_interval_sec=*/10);
    TlsServerCredentialsOptions options(certificate_provider);
    options.watch_root_certs();
    options.watch_identity_key_cert_pairs();
    options.set_cert_request_type(
        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    // options.set_crl_directory("");
    auto creds = grpc::experimental::TlsServerCredentials(options);
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, creds);
    TestServiceImpl service;
    builder.RegisterService(&service);
    server_ = builder.BuildAndStart();
    std::cout << "Server listening at " << server_address_.c_str() << std::endl;
  }

  ~TestServerWrapper() { server_->Shutdown(); }

  const std::string server_address_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
};

class CrlTest : public ::testing::Test {
 protected:
  CrlTest() {}
};

TEST_F(CrlTest, ValidTraffic) {
  TestServerWrapper wrapper;
  wrapper.Start();
  CallEchoRPC(wrapper.server_address_, false, false);
}

TEST_F(CrlTest, RevokedTraffic) {
  TestServerWrapper wrapper;
  wrapper.Start();
  CallEchoRPC(wrapper.server_address_, true, false);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
