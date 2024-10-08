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

#include <grpc++/grpc++.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/support/channel_arguments.h>

#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/util/tmpfile.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"

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

// TODO(gtcooke94) - Tests current failing with OpenSSL 1.1.1 and 3.0. Fix and
// re-enable.
#ifdef OPENSSL_IS_BORINGSSL

namespace grpc {
namespace testing {
namespace {

class EchoServer final : public EchoTestService::Service {
  grpc::Status Echo(grpc::ServerContext* /*context*/,
                    const EchoRequest* request,
                    EchoResponse* response) override {
    if (request->param().expected_error().code() == 0) {
      response->set_message(request->message());
      return grpc::Status::OK;
    } else {
      return grpc::Status(static_cast<grpc::StatusCode>(
                              request->param().expected_error().code()),
                          "");
    }
  }
};

class TestScenario {
 public:
  TestScenario(int num_listening_ports, bool share_tls_key_log_file,
               bool enable_tls_key_logging)
      : num_listening_ports_(num_listening_ports),
        share_tls_key_log_file_(share_tls_key_log_file),
        enable_tls_key_logging_(enable_tls_key_logging) {}
  std::string AsString() const {
    return absl::StrCat("TestScenario__num_listening_ports_",
                        num_listening_ports_, "__share_tls_key_log_file_",
                        (share_tls_key_log_file_ ? "true" : "false"),
                        "__enable_tls_key_logging_",
                        (enable_tls_key_logging_ ? "true" : "false"));
  }

  int num_listening_ports() const { return num_listening_ports_; }

  bool share_tls_key_log_file() const { return share_tls_key_log_file_; }

  bool enable_tls_key_logging() const { return enable_tls_key_logging_; }

 private:
  int num_listening_ports_;
  bool share_tls_key_log_file_;
  bool enable_tls_key_logging_;
};

std::string TestScenarioName(
    const ::testing::TestParamInfo<TestScenario>& info) {
  return info.param.AsString();
}

int CountOccurrencesInFileContents(std::string file_contents,
                                   std::string search_string) {
  int occurrences = 0;
  std::string::size_type pos = 0;
  while ((pos = file_contents.find(search_string, pos)) != std::string::npos) {
    ++occurrences;
    pos += search_string.length();
  }
  return occurrences;
}

class TlsKeyLoggingEnd2EndTest : public ::testing::TestWithParam<TestScenario> {
 protected:
  std::string CreateTmpFile() {
    char* name = nullptr;
    FILE* file_descriptor = gpr_tmpfile("GrpcTlsKeyLoggerTest", &name);
    CHECK_EQ(fclose(file_descriptor), 0);
    CHECK_NE(file_descriptor, nullptr);
    CHECK_NE(name, nullptr);
    std::string name_to_return = name;
    gpr_free(name);
    return name_to_return;
  }

  void SetUp() override {
    grpc::ServerBuilder builder;
    grpc::ChannelArguments args;
    args.SetSslTargetNameOverride("foo.test.google.com.au");

    if (GetParam().num_listening_ports() > 0) {
      ports_.resize(GetParam().num_listening_ports(), 0);
    }

    std::string shared_key_log_file_server;
    std::string shared_key_log_file_channel;

    if (GetParam().share_tls_key_log_file()) {
      shared_key_log_file_server = CreateTmpFile();
      shared_key_log_file_channel = CreateTmpFile();
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

      // Set a separate ssl key log file for each port if not shared
      if (GetParam().share_tls_key_log_file()) {
        tmp_server_tls_key_log_file_by_port_.push_back(
            shared_key_log_file_server);
      } else {
        tmp_server_tls_key_log_file_by_port_.push_back(CreateTmpFile());
      }

      if (GetParam().enable_tls_key_logging()) {
        server_creds_options.set_tls_session_key_log_file_path(
            tmp_server_tls_key_log_file_by_port_[i]);
      }

      builder.AddListeningPort(
          "0.0.0.0:0",
          grpc::experimental::TlsServerCredentials(server_creds_options),
          &ports_[i]);
    }

    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(nullptr, server_);
    server_thread_ =
        std::thread(&TlsKeyLoggingEnd2EndTest::RunServerLoop, this);

    for (int i = 0; i < GetParam().num_listening_ports(); i++) {
      ASSERT_NE(0, ports_[i]);
      server_addresses_.push_back(grpc_core::LocalIpAndPort(ports_[i]));

      // Configure tls credential options for each stub. Each stub connects to
      // a separate port on the server.
      TlsChannelCredentialsOptions channel_creds_options;
      channel_creds_options.set_certificate_provider(
          channel_certificate_provider);
      channel_creds_options.watch_identity_key_cert_pairs();
      channel_creds_options.watch_root_certs();

      // Set a separate ssl key log file for each port if not shared.
      if (GetParam().share_tls_key_log_file()) {
        tmp_stub_tls_key_log_file_.push_back(shared_key_log_file_channel);
      } else {
        tmp_stub_tls_key_log_file_.push_back(CreateTmpFile());
      }

      if (GetParam().enable_tls_key_logging()) {
        channel_creds_options.set_tls_session_key_log_file_path(
            tmp_stub_tls_key_log_file_[i]);
      }

      stubs_.push_back(EchoTestService::NewStub(grpc::CreateCustomChannel(
          server_addresses_[i],
          grpc::experimental::TlsCredentials(channel_creds_options), args)));
    }
  }

  void TearDown() override {
    server_->Shutdown();
    server_thread_.join();

    // Remove all created files.
    for (int i = 0; i < GetParam().num_listening_ports(); i++) {
      remove(tmp_stub_tls_key_log_file_[i].c_str());
      remove(tmp_server_tls_key_log_file_by_port_[i].c_str());
      if (GetParam().share_tls_key_log_file()) {
        break;
      }
    }
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
  std::unique_ptr<grpc::Server> server_;
  std::thread server_thread_;
};

TEST_P(TlsKeyLoggingEnd2EndTest, KeyLogging) {
  // Cover all valid statuses.
  for (int i = 0; i <= NUM_REQUESTS_PER_CHANNEL; ++i) {
    for (int j = 0; j < GetParam().num_listening_ports(); ++j) {
      EchoRequest request;
      request.set_message("foo");
      request.mutable_param()->mutable_expected_error()->set_code(0);
      EchoResponse response;
      grpc::ClientContext context;
      grpc::Status status = stubs_[j]->Echo(&context, request, &response);
      EXPECT_TRUE(status.ok());
    }
  }

  for (int i = 0; i < GetParam().num_listening_ports(); i++) {
    std::string server_key_log = grpc_core::testing::GetFileContents(
        tmp_server_tls_key_log_file_by_port_[i].c_str());
    std::string channel_key_log = grpc_core::testing::GetFileContents(
        tmp_stub_tls_key_log_file_[i].c_str());

    if (!GetParam().enable_tls_key_logging()) {
      EXPECT_THAT(server_key_log, ::testing::IsEmpty());
      EXPECT_THAT(channel_key_log, ::testing::IsEmpty());
    }

#ifdef TLS_KEY_LOGGING_AVAILABLE
    std::vector<absl::string_view> server_separated =
        absl::StrSplit(server_key_log, '\r');
    std::vector<absl::string_view> client_separated =
        absl::StrSplit(channel_key_log, '\r');
    EXPECT_THAT(server_separated,
                ::testing::UnorderedElementsAreArray(client_separated));

    if (GetParam().share_tls_key_log_file() &&
        GetParam().enable_tls_key_logging()) {
      EXPECT_EQ(CountOccurrencesInFileContents(
                    server_key_log, "CLIENT_HANDSHAKE_TRAFFIC_SECRET"),
                GetParam().num_listening_ports());
      EXPECT_EQ(CountOccurrencesInFileContents(
                    server_key_log, "SERVER_HANDSHAKE_TRAFFIC_SECRET"),
                GetParam().num_listening_ports());
      EXPECT_EQ(CountOccurrencesInFileContents(server_key_log,
                                               "CLIENT_TRAFFIC_SECRET_0"),
                GetParam().num_listening_ports());
      EXPECT_EQ(CountOccurrencesInFileContents(server_key_log,
                                               "SERVER_TRAFFIC_SECRET_0"),
                GetParam().num_listening_ports());
      EXPECT_EQ(
          CountOccurrencesInFileContents(server_key_log, "EXPORTER_SECRET"),
          GetParam().num_listening_ports());
    } else if (GetParam().enable_tls_key_logging()) {
      EXPECT_EQ(CountOccurrencesInFileContents(
                    server_key_log, "CLIENT_HANDSHAKE_TRAFFIC_SECRET"),
                1);
      EXPECT_EQ(CountOccurrencesInFileContents(
                    server_key_log, "SERVER_HANDSHAKE_TRAFFIC_SECRET"),
                1);
      EXPECT_EQ(CountOccurrencesInFileContents(server_key_log,
                                               "CLIENT_TRAFFIC_SECRET_0"),
                1);
      EXPECT_EQ(CountOccurrencesInFileContents(server_key_log,
                                               "SERVER_TRAFFIC_SECRET_0"),
                1);
      EXPECT_EQ(
          CountOccurrencesInFileContents(server_key_log, "EXPORTER_SECRET"), 1);
    }
#else
    // If TLS Key logging is not available, the files should be empty.
    if (GetParam().enable_tls_key_logging()) {
      EXPECT_THAT(server_key_log, ::testing::IsEmpty());
      EXPECT_THAT(channel_key_log, ::testing::IsEmpty());
    }
#endif

    if (GetParam().share_tls_key_log_file()) {
      break;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(TlsKeyLogging, TlsKeyLoggingEnd2EndTest,
                         ::testing::ValuesIn({TestScenario(5, false, true),
                                              TestScenario(5, true, true),
                                              TestScenario(5, true, false),
                                              TestScenario(5, false, false)}),
                         &TestScenarioName);

}  // namespace
}  // namespace testing
}  // namespace grpc

#endif  // OPENSSL_IS_BORING_SSL

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
