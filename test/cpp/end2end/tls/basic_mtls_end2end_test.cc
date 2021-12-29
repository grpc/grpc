// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc++/grpc++.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

extern "C" {
#include <openssl/ssl.h>
}

#if OPENSSL_VERSION_NUMBER >= 0x10101000 && !defined(LIBRESSL_VERSION_NUMBER)
#define TLS_KEY_LOGGING_AVAILABLE
#endif

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server0.key"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server0.pem"
#define CLIENT_KEY_PATH "src/core/tsi/test_creds/client.key"
#define CLIENT_CERT_PATH "src/core/tsi/test_creds/client.pem"

#define NUM_REQUESTS_PER_CHANNEL 5

using ::grpc::experimental::FileWatcherCertificateProvider;
using ::grpc::experimental::TlsChannelCredentialsOptions;
using ::grpc::experimental::TlsServerCredentialsOptions;

namespace grpc {
namespace testing {
namespace {

struct SecurityPrimitives {
  enum ProviderType { STATIC_PROVIDER = 0, FILE_PROVIDER = 1 } provider_type;
  enum VerifierType {
    EXTERNAL_SYNC_VERIFIER = 0,
    EXTERNAL_ASYNC_VERIFIER = 1,
    HOSTNAME_VERIFIER = 2
  } verifier_type;
  enum TlsVersion { V_12 = 0, V_13 = 1 } tls_version;
};

class EchoServer final : public EchoTestService::Service {
  ::grpc::Status Echo(::grpc::ServerContext* /*context*/,
                      const EchoRequest* request,
                      EchoResponse* response) override {
    if (request->param().expected_error().code() == 0) {
      response->set_message(request->message());
      return ::grpc::Status::OK;
    } else {
      return ::grpc::Status(static_cast<::grpc::StatusCode>(
                                request->param().expected_error().code()),
                            "");
    }
  }
};

class TestScenario {
 public:
  TestScenario(int num_listening_ports, SecurityPrimitives::ProviderType provider_type)
      : num_listening_ports_(num_listening_ports),
        provider_type_(provider_type) {}
  std::string AsString() const {
    return absl::StrCat("TestScenario__num_listening_ports_",
                        num_listening_ports_, "__provider_type_",
                        (provider_type_));
  }

  int num_listening_ports() const { return num_listening_ports_; }

  SecurityPrimitives::ProviderType provider_type() const { return provider_type_; }

 private:
  int num_listening_ports_;
  SecurityPrimitives::ProviderType provider_type_;
};

std::string TestScenarioName(
    const ::testing::TestParamInfo<TestScenario>& info) {
  return info.param.AsString();
}

class AdvancedTlsEnd2EndTest : public ::testing::TestWithParam<TestScenario> {
 protected:
  AdvancedTlsEnd2EndTest() = default;

  void SetUp() override {
    ::grpc::ServerBuilder builder;
    ::grpc::ChannelArguments args;
    args.SetSslTargetNameOverride("foo.test.google.com.au");

    if (GetParam().num_listening_ports() > 0) {
      ports_.resize(GetParam().num_listening_ports(), 0);
    }

    auto server_certificate_provider =
        std::make_shared<FileWatcherCertificateProvider>(
            SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);

    auto channel_certificate_provider =
        std::make_shared<FileWatcherCertificateProvider>(
            CLIENT_KEY_PATH, CLIENT_CERT_PATH, CA_CERT_PATH, 1);

    for (int i = 0; i < GetParam().num_listening_ports(); i++) {
      // Configure tls credential options for each port
      TlsServerCredentialsOptions server_creds_options(
          server_certificate_provider);
      server_creds_options.set_cert_request_type(
          GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
      server_creds_options.watch_identity_key_cert_pairs();
      server_creds_options.watch_root_certs();

      builder.AddListeningPort(
          "0.0.0.0:0",
          ::grpc::experimental::TlsServerCredentials(server_creds_options),
          &ports_[i]);
    }

    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(nullptr, server_);
    server_thread_ =
        std::thread(&AdvancedTlsEnd2EndTest::RunServerLoop, this);

    for (int i = 0; i < GetParam().num_listening_ports(); i++) {
      ASSERT_NE(0, ports_[i]);
      server_addresses_.push_back(absl::StrCat("localhost:", ports_[i]));

      // Configure tls credential options for each stub. Each stub connects to
      // a separate port on the server.
      TlsChannelCredentialsOptions channel_creds_options;
      channel_creds_options.set_certificate_provider(
          channel_certificate_provider);
      channel_creds_options.watch_identity_key_cert_pairs();
      channel_creds_options.watch_root_certs();

      stubs_.push_back(EchoTestService::NewStub(::grpc::CreateCustomChannel(
          server_addresses_[i],
          ::grpc::experimental::TlsCredentials(channel_creds_options), args)));
    }
  }

  void TearDown() override {
    server_->Shutdown();
    server_thread_.join();

    /*// Remove all created files.
    for (int i = 0; i < GetParam().num_listening_ports(); i++) {
      remove(tmp_stub_tls_key_log_file_[i].c_str());
      remove(tmp_server_tls_key_log_file_by_port_[i].c_str());
      if (GetParam().share_tls_key_log_file()) {
        break;
      }
    }*/
  }

  void RunServerLoop() { server_->Wait(); }

  const std::string client_method_name_ = "grpc.testing.EchoTestService/Echo";
  const std::string server_method_name_ = "grpc.testing.EchoTestService/Echo";

  std::vector<int> ports_;
  std::vector<std::string> tmp_server_tls_key_log_file_by_port_;
  std::vector<std::string> tmp_stub_tls_key_log_file_;
  std::vector<std::string> server_addresses_;
  std::vector<std::unique_ptr<EchoTestService::Stub>> stubs_;
  EchoServer service_;
  std::unique_ptr<::grpc::Server> server_;
  std::thread server_thread_;
};

TEST_P(AdvancedTlsEnd2EndTest, mTLSTests) {
  // Cover all valid statuses.
  for (int i = 0; i <= NUM_REQUESTS_PER_CHANNEL; ++i) {
    for (int j = 0; j < GetParam().num_listening_ports(); ++j) {
      EchoRequest request;
      request.set_message("foo");
      request.mutable_param()->mutable_expected_error()->set_code(0);
      EchoResponse response;
      ::grpc::ClientContext context;
      ::grpc::Status status = stubs_[j]->Echo(&context, request, &response);
      EXPECT_TRUE(status.ok());
    }
  }
}

INSTANTIATE_TEST_SUITE_P(TlsKeyLogging, AdvancedTlsEnd2EndTest,
                         ::testing::ValuesIn({TestScenario(5, SecurityPrimitives::STATIC_PROVIDER),
                                              TestScenario(5, SecurityPrimitives::FILE_PROVIDER)}),
                         &TestScenarioName);

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
}