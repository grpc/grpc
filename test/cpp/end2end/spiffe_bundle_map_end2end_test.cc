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
#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_certificate_verifier.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>

#include <memory>
#include <string>
#include <vector>

#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"

namespace grpc {
namespace testing {
namespace {

constexpr absl::string_view kMessage = "Hello";
constexpr absl::string_view kCaPemPath =
    "test/core/tsi/test_creds/spiffe_end2end/ca.pem";
constexpr absl::string_view kClientKeyPath =
    "test/core/tsi/test_creds/spiffe_end2end/client.key";
constexpr absl::string_view kClientCertPath =
    "test/core/tsi/test_creds/spiffe_end2end/client_spiffe.pem";
constexpr absl::string_view kServerKeyPath =
    "test/core/tsi/test_creds/spiffe_end2end/server.key";
constexpr absl::string_view kServerCertPath =
    "test/core/tsi/test_creds/spiffe_end2end/server_spiffe.pem";
constexpr absl::string_view kServerChainKeyPath =
    "test/core/tsi/test_creds/spiffe_end2end/leaf_signed_by_intermediate.key";
constexpr absl::string_view kServerChainCertPath =
    "test/core/tsi/test_creds/spiffe_end2end/leaf_and_intermediate_chain.pem";
constexpr absl::string_view kClientSpiffeBundleMapPath =
    "test/core/tsi/test_creds/spiffe_end2end/client_spiffebundle.json";
constexpr absl::string_view kServerSpiffeBundleMapPath =
    "test/core/tsi/test_creds/spiffe_end2end/server_spiffebundle.json";

std::string MakeConnectionFailureRegex(absl::string_view prefix) {
  return absl::StrCat(
      prefix,
      "(UNKNOWN|UNAVAILABLE): "
      // IP address
      "(ipv6:%5B::1%5D|ipv4:127.0.0.1):[0-9]+: "
      // Prefixes added for context
      "(Failed to connect to remote host: )?"
      "(Timeout occurred: )?"
      // Parenthetical wrappers
      "( ?\\(*("
      "Secure read failed|"
      "Handshake (read|write) failed|"
      "Delayed close due to in-progress write|"
      // Syscall
      "((connect|sendmsg|recvmsg|getsockopt\\(SO\\_ERROR\\)): ?)?"
      // strerror() output or other message
      "(Connection refused"
      "|Connection reset by peer"
      "|Socket closed"
      "|Broken pipe"
      "|FD shutdown"
      "|Endpoint closing)"
      // errno value
      "( \\([0-9]+\\))?"
      // close paren from wrappers above
      ")\\)*)+");
}

std::string MakeTlsHandshakeFailureRegex(absl::string_view prefix) {
  return absl::StrCat(
      prefix,
      "(UNKNOWN|UNAVAILABLE): "
      // IP address
      "(ipv6:%5B::1%5D|ipv4:127.0.0.1):[0-9]+: "
      // Prefixes added for context
      "(Failed to connect to remote host: )?"
      // Tls handshake failure
      "Tls handshake failed \\(TSI_PROTOCOL_FAILURE\\): SSL_ERROR_SSL: "
      "error:1000007d:SSL routines:OPENSSL_internal:CERTIFICATE_VERIFY_FAILED"
      // Detailed reason for certificate verify failure
      "(: .*)?");
}

class SpiffeBundleMapTest : public ::testing::Test {
 protected:
  void RunServer(absl::Notification* notification, absl::string_view key_path,
                 absl::string_view cert_path, absl::string_view root_path,
                 absl::string_view spiffe_bundle_map_path) {
    auto certificate_provider =
        std::make_shared<experimental::FileWatcherCertificateProvider>(
            std::string(key_path), std::string(cert_path),
            std::string(root_path), std::string(spiffe_bundle_map_path), 1);
    grpc::experimental::TlsServerCredentialsOptions options(
        certificate_provider);
    options.watch_root_certs();
    options.set_root_cert_name("root");
    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("identity");
    options.set_cert_request_type(
        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    auto server_credentials = grpc::experimental::TlsServerCredentials(options);
    CHECK_NE(server_credentials.get(), nullptr);

    grpc::ServerBuilder builder;

    builder.AddListeningPort(server_addr_, server_credentials);
    builder.RegisterService("foo.test.google.fr", &service_);
    server_ = builder.BuildAndStart();
    notification->Notify();
    server_->Wait();
  }

  void TearDown() override {
    if (server_ != nullptr) {
      server_->Shutdown();
      server_thread_->join();
      delete server_thread_;
    }
  }

  TestServiceImpl service_;
  std::unique_ptr<Server> server_ = nullptr;
  std::thread* server_thread_ = nullptr;
  std::string server_addr_;
};

void DoRpc(const std::string& server_addr,
           const experimental::TlsChannelCredentialsOptions& tls_options,
           bool expect_success, absl::string_view failure_message_regex = "",
           StatusCode failure_code = StatusCode::OK) {
  ChannelArguments channel_args;
  channel_args.SetSslTargetNameOverride("foo.test.google.fr");
  std::shared_ptr<Channel> channel = grpc::CreateCustomChannel(
      server_addr, grpc::experimental::TlsCredentials(tls_options),
      channel_args);

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message(kMessage);
  ClientContext context;
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/15));
  grpc::Status result = stub->Echo(&context, request, &response);
  if (expect_success) {
    EXPECT_TRUE(result.ok()) << result.error_message().c_str() << ", "
                             << result.error_details().c_str();
    EXPECT_EQ(response.message(), kMessage);
  } else {
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error_code(), failure_code);
// The expected failure message only matches when building against BoringSSL or
// OpenSSL < 3.0.
#if GTEST_USES_POSIX_RE && OPENSSL_VERSION_NUMBER < 0x30000000L && \
    defined(OPENSSL_IS_BORINGSSL)
    EXPECT_THAT(result.error_message(),
                ::testing::MatchesRegex(failure_message_regex));
#endif
  }
}

TEST_F(SpiffeBundleMapTest, ServerSideSpiffeTLS) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification, kServerKeyPath, kServerCertPath, "",
              kServerSpiffeBundleMapPath);
  });
  notification.WaitForNotification();

  std::string root_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));
  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = client_key;
  key_cert_pair.certificate_chain = client_cert;
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  auto certificate_provider =
      std::make_shared<experimental::StaticDataCertificateProvider>(
          root_cert, identity_key_cert_pairs);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");

  options.set_check_call_host(false);

  DoRpc(server_addr_, options, true);
}

TEST_F(SpiffeBundleMapTest, ClientSideSpiffeTLS) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification, kServerKeyPath, kServerCertPath, kCaPemPath, "");
  });
  notification.WaitForNotification();

  auto certificate_provider =
      std::make_shared<experimental::FileWatcherCertificateProvider>(
          std::string(kClientKeyPath), std::string(kClientCertPath),
          /*root_cert_path=*/"", std::string(kClientSpiffeBundleMapPath), 1);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");

  options.set_check_call_host(false);
  auto verifier = std::make_shared<experimental::NoOpCertificateVerifier>();
  options.set_certificate_verifier(verifier);

  DoRpc(server_addr_, options, true);
}

TEST_F(SpiffeBundleMapTest, SpiffeMTLS) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification, kServerKeyPath, kServerCertPath, "",
              kServerSpiffeBundleMapPath);
  });
  notification.WaitForNotification();

  auto certificate_provider =
      std::make_shared<experimental::FileWatcherCertificateProvider>(
          std::string(kClientKeyPath), std::string(kClientCertPath),
          /*root_cert_path=*/"", std::string(kClientSpiffeBundleMapPath), 1);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");

  options.set_check_call_host(false);
  auto verifier = std::make_shared<experimental::NoOpCertificateVerifier>();
  options.set_certificate_verifier(verifier);

  DoRpc(server_addr_, options, true);
}

TEST_F(SpiffeBundleMapTest, SpiffeWithCertChain) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification, kServerChainKeyPath, kServerChainCertPath, "",
              kServerSpiffeBundleMapPath);
  });
  notification.WaitForNotification();

  auto certificate_provider =
      std::make_shared<experimental::FileWatcherCertificateProvider>(
          std::string(kClientKeyPath), std::string(kClientCertPath),
          /*root_cert_path=*/"", std::string(kClientSpiffeBundleMapPath), 1);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");

  options.set_check_call_host(false);
  auto verifier = std::make_shared<experimental::NoOpCertificateVerifier>();
  options.set_certificate_verifier(verifier);

  DoRpc(server_addr_, options, true);
}

TEST_F(SpiffeBundleMapTest, ServerSpiffeReload) {
  auto server_bundle_map = grpc_core::testing::GetFileContents(
      std::string(kServerSpiffeBundleMapPath));
  auto client_bundle_map = grpc_core::testing::GetFileContents(
      std::string(kClientSpiffeBundleMapPath));
  grpc_core::testing::TmpFile tmp_bundle_map(server_bundle_map);
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification, kServerChainKeyPath, kServerChainCertPath, "",
              tmp_bundle_map.name());
  });
  notification.WaitForNotification();

  auto certificate_provider =
      std::make_shared<experimental::FileWatcherCertificateProvider>(
          std::string(kClientKeyPath), std::string(kClientCertPath),
          /*root_cert_path=*/"", std::string(kClientSpiffeBundleMapPath), 1);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");

  options.set_check_call_host(false);
  auto verifier = std::make_shared<experimental::NoOpCertificateVerifier>();
  options.set_certificate_verifier(verifier);
  DoRpc(server_addr_, options, true);

  // Update the spiffe bundle map to something that will fail
  tmp_bundle_map.RewriteFile(client_bundle_map);
  // Wait 2 seconds to ensure a refresh happens
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  constexpr absl::string_view expected_message_start =
      "failed to connect to all addresses; last error: ";
  const StatusCode expected_status = StatusCode::UNAVAILABLE;
  DoRpc(server_addr_, options, false,
        MakeConnectionFailureRegex(expected_message_start), expected_status);
}

TEST_F(SpiffeBundleMapTest, ClientSpiffeReload) {
  auto server_bundle_map = grpc_core::testing::GetFileContents(
      std::string(kServerSpiffeBundleMapPath));
  auto client_bundle_map = grpc_core::testing::GetFileContents(
      std::string(kClientSpiffeBundleMapPath));
  grpc_core::testing::TmpFile tmp_bundle_map(client_bundle_map);
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification, kServerChainKeyPath, kServerChainCertPath, "",
              kServerSpiffeBundleMapPath);
  });
  notification.WaitForNotification();

  auto certificate_provider =
      std::make_shared<experimental::FileWatcherCertificateProvider>(
          std::string(kClientKeyPath), std::string(kClientCertPath),
          /*root_cert_path=*/"", tmp_bundle_map.name(), 1);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");

  options.set_check_call_host(false);
  auto verifier = std::make_shared<experimental::NoOpCertificateVerifier>();
  options.set_certificate_verifier(verifier);
  DoRpc(server_addr_, options, true);

  // Update the spiffe bundle map to something that will fail
  tmp_bundle_map.RewriteFile(server_bundle_map);
  // Wait 2 seconds to ensure a refresh happens
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  constexpr absl::string_view expected_message_start =
      "failed to connect to all addresses; last error: ";
  const StatusCode expected_status = StatusCode::UNAVAILABLE;
  DoRpc(server_addr_, options, false,
        MakeTlsHandshakeFailureRegex(expected_message_start), expected_status);
}

TEST_F(SpiffeBundleMapTest, ServerSideSpiffeVerificationFailure) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  // Use the client-side spiffe bundle map on the server side to force a failure
  server_thread_ = new std::thread([&]() {
    RunServer(&notification, kServerKeyPath, kServerCertPath, "",
              kClientSpiffeBundleMapPath);
  });
  notification.WaitForNotification();

  auto certificate_provider =
      std::make_shared<experimental::FileWatcherCertificateProvider>(
          std::string(kClientKeyPath), std::string(kClientCertPath),
          /*root_cert_path=*/"", std::string(kClientSpiffeBundleMapPath), 1);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");

  options.set_check_call_host(false);
  auto verifier = std::make_shared<experimental::NoOpCertificateVerifier>();
  options.set_certificate_verifier(verifier);

  constexpr absl::string_view expected_message_start =
      "failed to connect to all addresses; last error: ";
  const StatusCode expected_status = StatusCode::UNAVAILABLE;
  DoRpc(server_addr_, options, false,
        MakeConnectionFailureRegex(expected_message_start), expected_status);
}

TEST_F(SpiffeBundleMapTest, ClientSideSpiffeVerificationFailure) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification, kServerKeyPath, kServerCertPath, "",
              kServerSpiffeBundleMapPath);
  });
  notification.WaitForNotification();

  // Use the server-side spiffe bundle map on the client side to force a failure
  auto certificate_provider =
      std::make_shared<experimental::FileWatcherCertificateProvider>(
          std::string(kClientKeyPath), std::string(kClientCertPath),
          /*root_cert_path=*/"", std::string(kServerSpiffeBundleMapPath), 1);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");

  options.set_check_call_host(false);
  auto verifier = std::make_shared<experimental::NoOpCertificateVerifier>();
  options.set_certificate_verifier(verifier);

  constexpr absl::string_view expected_message_start =
      "failed to connect to all addresses; last error: ";
  const StatusCode expected_status = StatusCode::UNAVAILABLE;
  DoRpc(server_addr_, options, false,
        MakeTlsHandshakeFailureRegex(expected_message_start), expected_status);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
