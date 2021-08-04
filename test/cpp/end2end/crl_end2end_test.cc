#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/channelz_service_plugin.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

#include <gtest/gtest.h>

using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::experimental::FileWatcherCertificateProvider;
using ::grpc::experimental::TlsChannelCredentialsOptions;
using ::grpc::experimental::TlsServerAuthorizationCheckArg;
using ::grpc::experimental::TlsServerAuthorizationCheckConfig;
using ::grpc::experimental::TlsServerAuthorizationCheckInterface;
using ::grpc::experimental::TlsServerCredentialsOptions;
using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;
using grpc::testing::EchoTestService;

namespace grpc {
namespace testing {
namespace {

constexpr char kCredentialsDir[] = "src/core/tsi/test_creds/crl_supported/";

std::string ReadFile(const char* file_path) {
  grpc_slice slice;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("load_file", grpc_load_file(file_path, 0, &slice)));
  std::string file_contents(grpc_core::StringViewFromSlice(slice));
  grpc_slice_unref(slice);
  return file_contents;
}

class TestTlsServerAuthorizationCheck
    : public TlsServerAuthorizationCheckInterface {
  int Schedule(TlsServerAuthorizationCheckArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    arg->set_success(1);
    arg->set_status(GRPC_STATUS_OK);
    return 0;
  }
};

void CallEchoRPC(const std::string& server_addr,
                 const std::string& certificate_file,
                 const std::string& key_file,
                 const std::string& ca_bundle_file) {
  auto certificate_provider = std::make_shared<FileWatcherCertificateProvider>(
      key_file, certificate_file, ca_bundle_file,
      /*refresh_interval_sec=*/10);
  TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.watch_identity_key_cert_pairs();
  options.set_server_verification_option(GRPC_TLS_SKIP_HOSTNAME_VERIFICATION);
  std::shared_ptr<TestTlsServerAuthorizationCheck>
      test_server_authorization_check(new TestTlsServerAuthorizationCheck());
  std::shared_ptr<TlsServerAuthorizationCheckConfig>
      server_authorization_check_config(new TlsServerAuthorizationCheckConfig(
          test_server_authorization_check));
  options.set_server_authorization_check_config(
      server_authorization_check_config);
  auto channel_creds = grpc::experimental::TlsCredentials(options);
  grpc::ChannelArguments args;
  args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, "testserver");
  auto channel = grpc::CreateCustomChannel(server_addr, channel_creds, args);
  std::unique_ptr<EchoTestService::Stub> stub =
      EchoTestService::NewStub(channel);
  EchoRequest request;
  request.set_message("This is a test.");
  EchoResponse reply;
  std::cout << "Sending test message" << std::endl;
  while (true) {
    ClientContext context;
    Status status = stub->Echo(&context, request, &reply);
    if (status.ok()) {
      gpr_log(GPR_INFO, "Client: received message: %s",
              reply.message().c_str());
    } else {
      gpr_log(GPR_INFO, "Client: errorCode: %d error: %s", status.error_code(),
              reply.message().c_str());
      break;
    }
    sleep(10 * 60);
  }
}

class TestServerWrapper {
 public:
  TestServerWrapper()
      : server_address_("localhost:" +
                        std::to_string(grpc_pick_unused_port_or_die())) {}
  void Start() {
    std::string certificate_file = absl::StrCat(kCredentialsDir, "/server.pem");
    std::string key_file = absl::StrCat(kCredentialsDir, "/server.key");
    std::string ca_bundle_file = absl::StrCat(kCredentialsDir, "/ca.pem");
    std::string certificate_pem = ReadFile(certificate_file);
    GPR_ASSERT(!certificate_pem.empty());
    std::string key_pem = ReadFile(key_file);
    GPR_ASSERT(!key_pem.empty());
    std::string ca_bundle_pem = ReadFile(ca_bundle_file);
    GPR_ASSERT(!ca_bundle_pem.empty());
    grpc_cpp_test::RunServer(certificate_file, key_file, ca_bundle_file);
  }

  ~TestServerWrapper() { server_->Shutdown(); }

  const std::string server_address_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;

 private:
  void Start(const std::string& certificate_file, const std::string& key_file,
             const std::string& ca_bundle_file) {
    auto certificate_provider =
        std::make_shared<FileWatcherCertificateProvider>(
            key_file, certificate_file, ca_bundle_file,
            /*refresh_interval_sec=*/10);
    TlsServerCredentialsOptions options(certificate_provider);
    // options.watch_root_certs();
    options.watch_identity_key_cert_pairs();
    options.set_cert_request_type(
        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    options.set_crl_directory("");
    auto creds = grpc::experimental::TlsServerCredentials(options);
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, creds);
    EchoServiceImpl service;
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());

    std::cout << "Server listening at " << server_address_.c_str() << std::endl;
  }
}

class CrlTest : public ::testing::Test {
 protected:
  CrlTest() {}

 private:
};

TEST_F(CrlTest, ValidTraffic) {}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
