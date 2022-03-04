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
#include "src/core/lib/iomgr/load_file.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"
#include "test/cpp/util/tls_test_utils.h"

extern "C" {
#include <openssl/ssl.h>
}

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server0.key"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server0.pem"
#define CLIENT_KEY_PATH "src/core/tsi/test_creds/client.key"
#define CLIENT_CERT_PATH "src/core/tsi/test_creds/client.pem"

using ::grpc::experimental::FileWatcherCertificateProvider;
using ::grpc::experimental::TlsChannelCredentialsOptions;
using ::grpc::experimental::TlsServerCredentialsOptions;
using ::grpc_core::testing::SecurityPrimitives;

namespace grpc {
namespace testing {
namespace {

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
  TestScenario(int num_listening_ports,
               SecurityPrimitives::ProviderType client_provider_type,
               SecurityPrimitives::ProviderType server_provider_type,
               SecurityPrimitives::VerifierType client_verifier_type,
               SecurityPrimitives::VerifierType server_verifier_type)
      : num_listening_ports_(num_listening_ports),
        client_provider_type_(client_provider_type),
        server_provider_type_(server_provider_type),
        client_verifier_type_(client_verifier_type),
        server_verifier_type_(server_verifier_type) {}
  std::string AsString() const {
    return absl::StrCat("TestScenario__num_listening_ports_",
                        num_listening_ports_, "__client_provider_type_",
                        client_provider_type_, "__server_provider_type_",
                        server_provider_type_, "__client_verifier_type_",
                        client_verifier_type_, "__server_verifier_type_",
                        server_verifier_type_);
  }

  int num_listening_ports() const { return num_listening_ports_; }

  SecurityPrimitives::ProviderType client_provider_type() const {
    return client_provider_type_;
  }

  SecurityPrimitives::ProviderType server_provider_type() const {
    return server_provider_type_;
  }

  SecurityPrimitives::VerifierType client_verifier_type() const {
    return client_verifier_type_;
  }

  SecurityPrimitives::VerifierType server_verifier_type() const {
    return server_verifier_type_;
  }

 private:
  int num_listening_ports_;
  SecurityPrimitives::ProviderType client_provider_type_;
  SecurityPrimitives::ProviderType server_provider_type_;
  SecurityPrimitives::VerifierType client_verifier_type_;
  SecurityPrimitives::VerifierType server_verifier_type_;
};

std::string TestScenarioName(
    const ::testing::TestParamInfo<TestScenario>& info) {
  return info.param.AsString();
}

namespace {

// This helper function reads the contents specified by the filename as
// std::string.
std::string GetContentsFromFilePath(const char* filename) {
  grpc_slice slice;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("load_file", grpc_load_file(filename, 1, &slice)));
  std::string contents = std::string(grpc_core::StringViewFromSlice(slice));
  grpc_slice_unref(slice);
  return contents;
}

}  // namespace

class AdvancedTlsEnd2EndTest : public ::testing::TestWithParam<TestScenario> {
 protected:
  void SetUp() override {
    ::grpc::ServerBuilder builder;
    ::grpc::ChannelArguments args;
    // We will need to override the peer name on the certificate if using
    // hostname verification, as we can't connect to that name in a test
    // environment.
    if (GetParam().client_verifier_type() ==
            SecurityPrimitives::HOSTNAME_VERIFIER ||
        GetParam().client_verifier_type() ==
            SecurityPrimitives::DEFAULT_VERIFIER) {
      args.SetSslTargetNameOverride("foo.test.google.com.au");
    }
    // Set up server certificate provider.
    // Hostname verifier on the server side is not applicable.
    GPR_ASSERT(GetParam().server_verifier_type() !=
               SecurityPrimitives::HOSTNAME_VERIFIER);
    std::shared_ptr<experimental::CertificateProviderInterface>
        server_certificate_provider;
    switch (GetParam().server_provider_type()) {
      case SecurityPrimitives::STATIC_PROVIDER: {
        std::string root_certs = GetContentsFromFilePath(CA_CERT_PATH);
        std::string server_key = GetContentsFromFilePath(SERVER_KEY_PATH);
        std::string server_certs = GetContentsFromFilePath(SERVER_CERT_PATH);
        experimental::IdentityKeyCertPair server_pair;
        server_pair.private_key = server_key;
        server_pair.certificate_chain = server_certs;
        std::vector<experimental::IdentityKeyCertPair> server_pair_list;
        server_pair_list.emplace_back(server_pair);
        server_certificate_provider =
            std::make_shared<experimental::StaticDataCertificateProvider>(
                root_certs, server_pair_list);
        break;
      }
      case SecurityPrimitives::FILE_PROVIDER: {
        server_certificate_provider =
            std::make_shared<FileWatcherCertificateProvider>(
                SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
        break;
      }
    }
    // Set up server certificate verifier.
    std::shared_ptr<experimental::CertificateVerifier>
        server_certificate_verifier;
    switch (GetParam().server_verifier_type()) {
      case SecurityPrimitives::EXTERNAL_SYNC_VERIFIER: {
        server_certificate_verifier =
            experimental::ExternalCertificateVerifier::Create<
                SyncCertificateVerifier>(true);
        break;
      }
      case SecurityPrimitives::EXTERNAL_ASYNC_VERIFIER: {
        server_certificate_verifier =
            experimental::ExternalCertificateVerifier::Create<
                AsyncCertificateVerifier>(true);
        break;
      }
      case SecurityPrimitives::HOSTNAME_VERIFIER: {
        server_certificate_verifier =
            std::make_shared<experimental::HostNameCertificateVerifier>();
        break;
      }
      default: {
        break;
      }
    }
    // Build the server and add listening ports.
    /*if (GetParam().num_listening_ports() > 0) {
      ports_.resize(GetParam().num_listening_ports(), 0);
    }*/
    TlsServerCredentialsOptions server_creds_options(
        server_certificate_provider);
    server_creds_options.set_cert_request_type(
        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    server_creds_options.watch_identity_key_cert_pairs();
    server_creds_options.watch_root_certs();
    if (GetParam().server_verifier_type() !=
        SecurityPrimitives::DEFAULT_VERIFIER) {
      server_creds_options.set_certificate_verifier(
          server_certificate_verifier);
    }
    auto server_credentials =
        ::grpc::experimental::TlsServerCredentials(server_creds_options);
    endpoint_info_.resize(GetParam().num_listening_ports());
    for (int i = 0; i < GetParam().num_listening_ports(); ++i) {
      endpoint_info_.push_back(EndPointInfo());
      builder.AddListeningPort("0.0.0.0:0", server_credentials,
                               &endpoint_info_[i].port);
      // builder.AddListeningPort("0.0.0.0:0", server_credentials, &ports_[i]);
    }
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(nullptr, server_);
    server_thread_ = std::thread(&AdvancedTlsEnd2EndTest::RunServerLoop, this);
    // Set up client certificate provider.
    std::shared_ptr<experimental::CertificateProviderInterface>
        channel_certificate_provider;
    switch (GetParam().server_provider_type()) {
      case SecurityPrimitives::STATIC_PROVIDER: {
        std::string root_certs = GetContentsFromFilePath(CA_CERT_PATH);
        std::string client_key = GetContentsFromFilePath(CLIENT_KEY_PATH);
        std::string client_certs = GetContentsFromFilePath(CLIENT_CERT_PATH);
        experimental::IdentityKeyCertPair client_pair;
        client_pair.private_key = client_key;
        client_pair.certificate_chain = client_certs;
        std::vector<experimental::IdentityKeyCertPair> client_pair_list;
        client_pair_list.emplace_back(client_pair);
        channel_certificate_provider =
            std::make_shared<experimental::StaticDataCertificateProvider>(
                root_certs, client_pair_list);
        break;
      }
      case SecurityPrimitives::FILE_PROVIDER: {
        channel_certificate_provider =
            std::make_shared<FileWatcherCertificateProvider>(
                CLIENT_KEY_PATH, CLIENT_CERT_PATH, CA_CERT_PATH, 1);
        break;
      }
    }
    // Set up client certificate verifier.
    std::shared_ptr<experimental::CertificateVerifier>
        client_certificate_verifier;
    switch (GetParam().client_verifier_type()) {
      case SecurityPrimitives::EXTERNAL_SYNC_VERIFIER: {
        client_certificate_verifier =
            experimental::ExternalCertificateVerifier::Create<
                SyncCertificateVerifier>(true);
        break;
      }
      case SecurityPrimitives::EXTERNAL_ASYNC_VERIFIER: {
        client_certificate_verifier =
            experimental::ExternalCertificateVerifier::Create<
                AsyncCertificateVerifier>(true);
        break;
      }
      case SecurityPrimitives::HOSTNAME_VERIFIER: {
        client_certificate_verifier =
            std::make_shared<experimental::HostNameCertificateVerifier>();
        break;
      }
      default: {
        break;
      }
    }
    // Configure tls credential options for each stub. Each stub connects to
    // a separate port on the server.
    TlsChannelCredentialsOptions channel_creds_options;
    channel_creds_options.set_certificate_provider(
        channel_certificate_provider);
    channel_creds_options.watch_identity_key_cert_pairs();
    channel_creds_options.watch_root_certs();
    if (GetParam().client_verifier_type() !=
        SecurityPrimitives::DEFAULT_VERIFIER) {
      channel_creds_options.set_certificate_verifier(
          client_certificate_verifier);
      // When using a customized external verifier, we need to disable the
      // per-host checks.
      if (GetParam().client_verifier_type() !=
          SecurityPrimitives::HOSTNAME_VERIFIER) {
        channel_creds_options.set_check_call_host(false);
      }
    }
    auto channel_credentials =
        ::grpc::experimental::TlsCredentials(channel_creds_options);
    for (int i = 0; i < GetParam().num_listening_ports(); i++) {
      ASSERT_NE(0, endpoint_info_[i].port);
      endpoint_info_[i].server_address =
          absl::StrCat("localhost:", endpoint_info_[i].port);
      // ASSERT_NE(0, ports_[i]);
      // endpoint_info_[i].server_address = absl::StrCat("localhost:",
      // ports_[i]);
      stubs_.push_back(EchoTestService::NewStub(::grpc::CreateCustomChannel(
          endpoint_info_[i].server_address, channel_credentials, args)));
    }
  }

  void TearDown() override {
    server_->Shutdown();
    server_thread_.join();
  }

  void RunServerLoop() { server_->Wait(); }

  struct EndPointInfo {
    int port = 0;
    std::string server_address;
  };
  // std::vector<int> ports_;
  std::vector<EndPointInfo> endpoint_info_;
  std::vector<std::unique_ptr<EchoTestService::Stub>> stubs_;
  EchoServer service_;
  std::unique_ptr<::grpc::Server> server_;
  std::thread server_thread_;
};

TEST_P(AdvancedTlsEnd2EndTest, mTLSTests) {
  for (int i = 0; i < GetParam().num_listening_ports(); ++i) {
    EchoRequest request;
    request.set_message("foo");
    request.mutable_param()->mutable_expected_error()->set_code(0);
    EchoResponse response;
    ::grpc::ClientContext context;
    ::grpc::Status status = stubs_[i]->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok());
  }
}

INSTANTIATE_TEST_SUITE_P(
    TlsKeyLogging, AdvancedTlsEnd2EndTest,
    // We only choose a small subset of all the possible combination of these
    // security primitives for testing, because as we add more primitives, the
    // combination set would grow exponentially.
    // The cases we test here are ones we think most users are likely to run
    // into when building their own applications.
    ::testing::ValuesIn({
        TestScenario(5, SecurityPrimitives::STATIC_PROVIDER,
                     SecurityPrimitives::STATIC_PROVIDER,
                     SecurityPrimitives::DEFAULT_VERIFIER,
                     SecurityPrimitives::DEFAULT_VERIFIER),
        TestScenario(5, SecurityPrimitives::FILE_PROVIDER,
                     SecurityPrimitives::FILE_PROVIDER,
                     SecurityPrimitives::DEFAULT_VERIFIER,
                     SecurityPrimitives::DEFAULT_VERIFIER),
        TestScenario(5, SecurityPrimitives::STATIC_PROVIDER,
                     SecurityPrimitives::FILE_PROVIDER,
                     SecurityPrimitives::DEFAULT_VERIFIER,
                     SecurityPrimitives::DEFAULT_VERIFIER),
        TestScenario(5, SecurityPrimitives::FILE_PROVIDER,
                     SecurityPrimitives::STATIC_PROVIDER,
                     SecurityPrimitives::DEFAULT_VERIFIER,
                     SecurityPrimitives::DEFAULT_VERIFIER),
        TestScenario(5, SecurityPrimitives::STATIC_PROVIDER,
                     SecurityPrimitives::STATIC_PROVIDER,
                     SecurityPrimitives::HOSTNAME_VERIFIER,
                     SecurityPrimitives::DEFAULT_VERIFIER),
        TestScenario(5, SecurityPrimitives::FILE_PROVIDER,
                     SecurityPrimitives::FILE_PROVIDER,
                     SecurityPrimitives::DEFAULT_VERIFIER,
                     SecurityPrimitives::EXTERNAL_SYNC_VERIFIER),
        TestScenario(5, SecurityPrimitives::STATIC_PROVIDER,
                     SecurityPrimitives::STATIC_PROVIDER,
                     SecurityPrimitives::EXTERNAL_SYNC_VERIFIER,
                     SecurityPrimitives::DEFAULT_VERIFIER),
        TestScenario(5, SecurityPrimitives::FILE_PROVIDER,
                     SecurityPrimitives::FILE_PROVIDER,
                     SecurityPrimitives::EXTERNAL_SYNC_VERIFIER,
                     SecurityPrimitives::EXTERNAL_SYNC_VERIFIER),
        TestScenario(5, SecurityPrimitives::STATIC_PROVIDER,
                     SecurityPrimitives::STATIC_PROVIDER,
                     SecurityPrimitives::EXTERNAL_ASYNC_VERIFIER,
                     SecurityPrimitives::DEFAULT_VERIFIER),
        TestScenario(5, SecurityPrimitives::FILE_PROVIDER,
                     SecurityPrimitives::FILE_PROVIDER,
                     SecurityPrimitives::EXTERNAL_ASYNC_VERIFIER,
                     SecurityPrimitives::EXTERNAL_ASYNC_VERIFIER),
    }),
    &TestScenarioName);

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
}
