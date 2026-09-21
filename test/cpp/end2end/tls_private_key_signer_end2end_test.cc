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

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_certificate_verifier.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/security/tls_private_key_signer.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/end2end/tls_test_private_key_signer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#if defined(OPENSSL_IS_BORINGSSL)

namespace grpc {
namespace testing {
namespace {

constexpr absl::string_view kMessage = "Hello";
constexpr absl::string_view kCaPemPath = "src/core/tsi/test_creds/ca.pem";
constexpr absl::string_view kServerKey0Path =
    "src/core/tsi/test_creds/server0.key";
constexpr absl::string_view kServerCert0Path =
    "src/core/tsi/test_creds/server0.pem";
constexpr absl::string_view kServerKeyPath =
    "src/core/tsi/test_creds/server1.key";
constexpr absl::string_view kServerCertPath =
    "src/core/tsi/test_creds/server1.pem";
constexpr absl::string_view kClientKeyPath =
    "src/core/tsi/test_creds/client.key";
constexpr absl::string_view kClientCertPath =
    "src/core/tsi/test_creds/client.pem";

class TlsPrivateKeyOffloadTest : public ::testing::Test {
 protected:
  void StartServer(std::shared_ptr<experimental::CertificateProviderInterface>
                       server_certificate_provider,
                   int handshake_timeout_ms = 0) {
    absl::Notification notification;
    server_thread_ = std::make_unique<std::thread>([this, &notification,
                                                    server_certificate_provider,
                                                    handshake_timeout_ms]() {
      RunServer(&notification, server_certificate_provider,
                handshake_timeout_ms);
    });
    notification.WaitForNotification();
  }

  void RunServer(absl::Notification* notification,
                 std::shared_ptr<experimental::CertificateProviderInterface>
                     server_certificate_provider,
                 int handshake_timeout_ms = 0) {
    grpc::experimental::TlsServerCredentialsOptions options(
        std::move(server_certificate_provider));
    options.watch_root_certs();
    options.set_root_cert_name("root");
    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("identity");
    options.set_cert_request_type(
        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    auto server_credentials = grpc::experimental::TlsServerCredentials(options);
    CHECK_NE(server_credentials.get(), nullptr);

    grpc::ServerBuilder builder;

    if (handshake_timeout_ms > 0) {
      builder.AddChannelArgument(GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS,
                                 handshake_timeout_ms);
    }

    builder.AddListeningPort(server_addr_, server_credentials);
    builder.RegisterService("foo.test.google.fr", &service_);
    server_ = builder.BuildAndStart();
    notification->Notify();
    server_->Wait();
  }

  void TearDown() override {
    if (server_ != nullptr) {
      server_->Shutdown();
    }
    if (server_thread_ != nullptr) {
      server_thread_->join();
    }
  }

  TestServiceImpl service_;
  std::unique_ptr<Server> server_ = nullptr;
  std::unique_ptr<std::thread> server_thread_;
  std::string server_addr_;
  std::shared_ptr<grpc::experimental::PrivateKeySigner> signer_;
};

void DoRpc(const std::string& server_addr,
           const experimental::TlsChannelCredentialsOptions& tls_options,
           bool expect_sni_match = false) {
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
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/5));
  grpc::Status result = stub->Echo(&context, request, &response);
  if (expect_sni_match) {
    std::vector<grpc::string_ref> san_names =
        context.auth_context()->FindPropertyValues(GRPC_X509_SAN_PROPERTY_NAME);
    bool sni_match = false;
    for (const auto& san : san_names) {
      if (san.ends_with("test.google.fr")) {
        sni_match = true;
        break;
      }
    }
    EXPECT_TRUE(sni_match);
  }
  EXPECT_TRUE(result.ok()) << result.error_message().c_str() << ", "
                           << result.error_details().c_str();
  EXPECT_EQ(response.message(), kMessage);
}

// Performs an RPC and expects it to fail.
// on_rpc_stalled is an optional callback that will be executed after a delay (1
// second) after the RPC is initiated. This is useful for cases where the RPC is
// expected to wait and an action is needed to unblock the test.
void DoRpcAndExpectFailure(
    const std::string& server_addr,
    const experimental::TlsChannelCredentialsOptions& tls_options,
    const std::function<void(ClientContext*)>& on_rpc_stalled = nullptr,
    absl::string_view expected_error_message = "",
    ChannelArguments channel_args = ChannelArguments()) {
  channel_args.SetSslTargetNameOverride("foo.test.google.fr");
  std::shared_ptr<Channel> channel = grpc::CreateCustomChannel(
      server_addr, grpc::experimental::TlsCredentials(tls_options),
      channel_args);

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message(kMessage);
  ClientContext context;
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/5));
  absl::Notification notification;
  bool wait_for_notification = false;
  if (on_rpc_stalled != nullptr) {
    wait_for_notification = true;
    grpc_core::ExecCtx exec_ctx;
    grpc_event_engine::experimental::GetDefaultEventEngine()->RunAfter(
        std::chrono::seconds(1),
        [on_rpc_stalled, &context, notification = &notification]() {
          on_rpc_stalled(&context);
          notification->Notify();
        });
  }
  grpc::Status result = stub->Echo(&context, request, &response);
  if (wait_for_notification) {
    notification.WaitForNotificationWithTimeout(absl::Seconds(10));
  }
  EXPECT_FALSE(result.ok());
  if (!expected_error_message.empty()) {
    EXPECT_THAT(result.error_message(),
                ::testing::HasSubstr(std::string(expected_error_message)));
  }
}

// Verifies that the server can successfully offload signing to an asynchronous
// custom signer.
TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerAsyncWithSniMatch) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key_0 =
      grpc_core::testing::GetFileContents(std::string(kServerKey0Path));
  std::string server_cert_0 =
      grpc_core::testing::GetFileContents(std::string(kServerCert0Path));
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  std::vector<experimental::IdentityKeyOrSignerCertPair>
      server_identity_key_cert_pairs;
  signer_ = std::make_shared<AsyncTestPrivateKeySigner>(server_key);
  // The bad signer should not be used with correct SNI matching.
  std::shared_ptr<experimental::PrivateKeySigner> bad_signer =
      std::make_shared<AsyncTestPrivateKeySigner>(
          server_key_0, AsyncTestPrivateKeySigner::Mode::kError);
  server_identity_key_cert_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{bad_signer,
                                                      server_cert_0});
  server_identity_key_cert_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer_, server_cert});
  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  StartServer(server_certificate_provider);

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{client_key, client_cert});
  ASSERT_TRUE(client_certificate_provider
                  ->UpdateIdentityKeyCertPair(identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpc(server_addr_, options);
}

// Verifies that the server can successfully offload signing to an asynchronous
// custom signer.
TEST_F(TlsPrivateKeyOffloadTest,
       OffloadWithCustomKeySignerAsyncWithoutSniMatch) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key_0 =
      grpc_core::testing::GetFileContents(std::string(kServerKey0Path));
  std::string server_cert_0 =
      grpc_core::testing::GetFileContents(std::string(kServerCert0Path));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  // SNI doesn't match so the default certificate will be used.
  std::vector<experimental::IdentityKeyOrSignerCertPair>
      server_identity_key_cert_pairs;
  signer_ = std::make_shared<AsyncTestPrivateKeySigner>(server_key_0);
  server_identity_key_cert_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer_, server_cert_0});
  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  StartServer(server_certificate_provider);

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{client_key, client_cert});
  ASSERT_TRUE(client_certificate_provider
                  ->UpdateIdentityKeyCertPair(identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  // Skip the host name check so that the handshake can succeed.
  options.set_certificate_verifier(
      std::make_shared<experimental::NoOpCertificateVerifier>());

  DoRpc(server_addr_, options);
}

// Verifies that the server handshake succeeds even when the asynchronous signer
// has a significant delay.
TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerAsyncDelayed) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  std::vector<experimental::IdentityKeyOrSignerCertPair>
      server_identity_key_cert_pairs;
  signer_ = std::make_shared<AsyncTestPrivateKeySigner>(
      server_key, AsyncTestPrivateKeySigner::Mode::kDelayed, absl::Seconds(1));
  server_identity_key_cert_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer_, server_cert});
  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  StartServer(server_certificate_provider);

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{client_key, client_cert});
  ASSERT_TRUE(client_certificate_provider
                  ->UpdateIdentityKeyCertPair(identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpc(server_addr_, options);
}

// Verifies that the client can successfully offload signing to an asynchronous
// custom signer during mTLS.
TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerClientAsync) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  server_identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{server_key, server_cert});
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  StartServer(server_certificate_provider);

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  signer_ = std::make_shared<AsyncTestPrivateKeySigner>(client_key);
  std::vector<grpc::experimental::IdentityKeyOrSignerCertPair> identity_pairs;
  identity_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer_, client_cert});
  ASSERT_TRUE(
      client_certificate_provider->UpdateIdentityKeyCertPair(identity_pairs)
          .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpc(server_addr_, options);
}

// Verifies that the server can successfully offload signing to a synchronous
// custom signer.
TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerSync) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  std::vector<experimental::IdentityKeyOrSignerCertPair>
      server_identity_key_cert_pairs;
  signer_ = std::make_shared<SyncTestPrivateKeySigner>(server_key);
  server_identity_key_cert_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer_, server_cert});
  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  StartServer(server_certificate_provider);

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{client_key, client_cert});
  ASSERT_TRUE(client_certificate_provider
                  ->UpdateIdentityKeyCertPair(identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpc(server_addr_, options);
}

// Verifies that the client can successfully offload signing to a synchronous
// custom signer during mTLS.
TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerClientSync) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  server_identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{server_key, server_cert});
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  StartServer(server_certificate_provider);

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  signer_ = std::make_shared<SyncTestPrivateKeySigner>(client_key);
  std::vector<grpc::experimental::IdentityKeyOrSignerCertPair> identity_pairs;
  identity_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer_, client_cert});
  ASSERT_TRUE(
      client_certificate_provider->UpdateIdentityKeyCertPair(identity_pairs)
          .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpc(server_addr_, options);
}

// Verifies that an immediate error status returned by a synchronous signer
// correctly fails the RPC.
TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerErrorSync) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  server_identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{server_key, server_cert});
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  StartServer(server_certificate_provider);

  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  auto signer = std::make_shared<SyncTestPrivateKeySigner>(
      "", SyncTestPrivateKeySigner::Mode::kError);
  std::vector<grpc::experimental::IdentityKeyOrSignerCertPair> identity_pairs;
  identity_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer, client_cert});
  ASSERT_TRUE(
      client_certificate_provider->UpdateIdentityKeyCertPair(identity_pairs)
          .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpcAndExpectFailure(server_addr_, options, nullptr, "Test error sync");
}

// Verifies that an error status returned via a callback from an asynchronous
// signer correctly fails the RPC.
TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerErrorAsync) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  server_identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{server_key, server_cert});
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  StartServer(server_certificate_provider);

  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  auto signer = std::make_shared<AsyncTestPrivateKeySigner>(
      "", AsyncTestPrivateKeySigner::Mode::kError);
  std::vector<grpc::experimental::IdentityKeyOrSignerCertPair> identity_pairs;
  identity_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer, client_cert});
  ASSERT_TRUE(
      client_certificate_provider->UpdateIdentityKeyCertPair(identity_pairs)
          .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpcAndExpectFailure(server_addr_, options, nullptr, "Test error async");
}

// Verifies that the TLS handshake fails when a synchronous signer provides a
// signature from the wrong key.
TEST_F(TlsPrivateKeyOffloadTest,
       OffloadWithCustomKeySignerInvalidSignatureSync) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  server_identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{server_key, server_cert});
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  StartServer(server_certificate_provider);

  // Load the wrong key for signing.
  std::string server_key_wrong =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  auto signer = std::make_shared<SyncTestPrivateKeySigner>(server_key_wrong);
  std::vector<grpc::experimental::IdentityKeyOrSignerCertPair> identity_pairs;
  identity_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer, client_cert});
  ASSERT_TRUE(
      client_certificate_provider->UpdateIdentityKeyCertPair(identity_pairs)
          .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpcAndExpectFailure(server_addr_, options);
}

// Verifies that the TLS handshake fails when an asynchronous signer provides a
// signature from the wrong key.
TEST_F(TlsPrivateKeyOffloadTest,
       OffloadWithCustomKeySignerInvalidSignatureAsync) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  server_identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{server_key, server_cert});
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  StartServer(server_certificate_provider);

  // Load the WRONG key for signing.
  std::string server_key_wrong =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  auto signer = std::make_shared<AsyncTestPrivateKeySigner>(server_key_wrong);
  std::vector<grpc::experimental::IdentityKeyOrSignerCertPair> identity_pairs;
  identity_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer, client_cert});
  ASSERT_TRUE(
      client_certificate_provider->UpdateIdentityKeyCertPair(identity_pairs)
          .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpcAndExpectFailure(server_addr_, options);
}

// Verifies that the server correctly times out the handshake if the custom
// signer takes too long.
TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerHandshakeTimeout) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  std::vector<experimental::IdentityKeyOrSignerCertPair>
      server_identity_key_cert_pairs;
  // Signer with a long delay.
  signer_ = std::make_shared<AsyncTestPrivateKeySigner>(
      server_key, AsyncTestPrivateKeySigner::Mode::kDelayed, absl::Seconds(10));
  server_identity_key_cert_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer_, server_cert});
  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  // Start server with a short handshake timeout.
  StartServer(server_certificate_provider, /*handshake_timeout_ms=*/500);

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{client_key, client_cert});
  ASSERT_TRUE(client_certificate_provider
                  ->UpdateIdentityKeyCertPair(identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpcAndExpectFailure(server_addr_, options);
}

// Verifies that the client correctly times out the handshake if the custom
// signer takes too long.
TEST_F(TlsPrivateKeyOffloadTest,
       OffloadWithCustomKeySignerClientHandshakeTimeout) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  server_identity_key_cert_pairs.emplace_back(
      experimental::IdentityKeyCertPair{server_key, server_cert});
  ASSERT_TRUE(server_certificate_provider
                  ->UpdateIdentityKeyCertPair(server_identity_key_cert_pairs)
                  .ok());
  ASSERT_TRUE(server_certificate_provider->UpdateRoot(ca_cert).ok());

  StartServer(server_certificate_provider);

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();

  // Signer with a long delay.
  auto signer = std::make_shared<AsyncTestPrivateKeySigner>(
      client_key, AsyncTestPrivateKeySigner::Mode::kDelayed, absl::Seconds(10));
  std::vector<grpc::experimental::IdentityKeyOrSignerCertPair> identity_pairs;
  identity_pairs.emplace_back(
      grpc::experimental::IdentityKeyOrSignerCertPair{signer, client_cert});
  ASSERT_TRUE(
      client_certificate_provider->UpdateIdentityKeyCertPair(identity_pairs)
          .ok());
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  // Set a short handshake timeout on the client.
  ChannelArguments channel_args;
  channel_args.SetInt("grpc.testing.fixed_reconnect_backoff_ms", 500);

  DoRpcAndExpectFailure(server_addr_, options, nullptr, "Handshaker shutdown",
                        channel_args);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

#endif

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
