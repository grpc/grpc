//
//
// Copyright 2023 gRPC authors.
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
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"

#include <memory>
#include <string>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/synchronization/notification.h"

#include <grpc/credentials.h>
#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/gprpp/cpp_impl_of.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/cpp/client/secure_credentials.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"

// TODO(matthewstevenson88): More test cases to add:
// - Use P256, P384, P512 credentials.
// - Use a long certificate chain.
// - Use a large certificate.
// - Large trust bundle.
// - Bad ALPN.
// - More failure modes.
// - Certs containing more SANs.
// - Copy all of this over to tls_credentials_test.cc.
// - Client doesn't have cert but server requests one.
// - Bad session ticket in cache.
// - Use same channel creds object on sequential/concurrent handshakes.
// - Do successful handshake with a localhost server cert.
// - Missing or malformed roots on both sides.

namespace grpc {
namespace testing {
namespace {

using ::grpc_core::testing::GetFileContents;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

constexpr char kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
constexpr char kServerCertPath[] = "src/core/tsi/test_creds/server1.pem";
constexpr char kServerKeyPath[] = "src/core/tsi/test_creds/server1.key";
constexpr char kClientCertPath[] = "src/core/tsi/test_creds/client.pem";
constexpr char kClientKeyPath[] = "src/core/tsi/test_creds/client.key";
constexpr char kMessage[] = "Hello";
constexpr char kTargetNameOverride[] = "foo.test.google.fr";

std::size_t GetSessionCacheSize(grpc_ssl_session_cache* cache) {
  tsi_ssl_session_cache* tsi_cache =
      reinterpret_cast<tsi_ssl_session_cache*>(cache);
  return tsi_ssl_session_cache_size(tsi_cache);
}

struct SslOptions {
  grpc_ssl_client_certificate_request_type request_type =
      GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
  bool use_session_cache;
};

class SslCredentialsTest : public ::testing::TestWithParam<SslOptions> {
 protected:
  void RunServer(absl::Notification* notification,
                 absl::string_view pem_root_certs) {
    grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair = {
        GetFileContents(kServerKeyPath), GetFileContents(kServerCertPath)};
    grpc::SslServerCredentialsOptions ssl_options(GetParam().request_type);
    ssl_options.pem_key_cert_pairs.push_back(key_cert_pair);
    ssl_options.pem_root_certs = std::string(pem_root_certs);

    grpc::ServerBuilder builder;
    TestServiceImpl service_;

    builder.AddListeningPort(server_addr_,
                             grpc::SslServerCredentials(ssl_options));
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

  absl::StatusOr<std::shared_ptr<const AuthContext>> DoRpc(
      const SslCredentialsOptions& options, grpc_ssl_session_cache* cache,
      bool override_ssl_target_name = true) {
    ChannelArguments channel_args;
    if (GetParam().use_session_cache) {
      channel_args.SetPointer(std::string(GRPC_SSL_SESSION_CACHE_ARG), cache);
    }
    if (override_ssl_target_name) {
      channel_args.SetSslTargetNameOverride(kTargetNameOverride);
    }

    auto creds = SslCredentials(options);
    std::shared_ptr<Channel> channel =
        grpc::CreateCustomChannel(server_addr_, creds, channel_args);

    auto stub = grpc::testing::EchoTestService::NewStub(channel);
    grpc::testing::EchoRequest request;
    grpc::testing::EchoResponse response;
    request.set_message(kMessage);
    ClientContext context;
    context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/10));
    grpc::Status result = stub->Echo(&context, request, &response);
    if (!result.ok()) {
      return absl::Status(static_cast<absl::StatusCode>(result.error_code()),
                          result.error_message());
    }
    EXPECT_EQ(response.message(), kMessage);
    return context.auth_context();
  }

  static std::vector<absl::string_view> GetAuthContextPropertyAsList(
      const AuthContext& auth_context, const std::string& property) {
    std::vector<absl::string_view> properties;
    for (const grpc::string_ref& property :
         auth_context.FindPropertyValues(property)) {
      properties.push_back(absl::string_view(property.data(), property.size()));
    }
    return properties;
  }

  static absl::string_view GetAuthContextProperty(
      const AuthContext& auth_context, const std::string& property) {
    std::vector<absl::string_view> properties =
        GetAuthContextPropertyAsList(auth_context, property);
    return properties.size() == 1 ? properties[0] : "";
  }

  TestServiceImpl service_;
  std::unique_ptr<Server> server_ = nullptr;
  std::thread* server_thread_ = nullptr;
  std::string server_addr_;
};

TEST_P(SslCredentialsTest, FullHandshake) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    std::string root_cert = GetFileContents(kCaCertPath);
    RunServer(&notification, root_cert);
  });
  notification.WaitForNotification();

  std::string root_cert = GetFileContents(kCaCertPath);
  std::string client_key = GetFileContents(kClientKeyPath);
  std::string client_cert = GetFileContents(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(16);

  auto full_handshake_context = DoRpc(ssl_options, cache);
  EXPECT_EQ(full_handshake_context.status(), absl::OkStatus());
  EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                   GRPC_SSL_SESSION_REUSED_PROPERTY),
            "false");
  EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                   GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME),
            GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                   GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME),
            "TSI_PRIVACY_AND_INTEGRITY");
  EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                   GRPC_X509_CN_PROPERTY_NAME),
            "*.test.google.com");
  EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                   GRPC_X509_SUBJECT_PROPERTY_NAME),
            "CN=*.test.google.com,O=Example\\, Co.,L=Chicago,ST=Illinois,C=US");
  EXPECT_THAT(
      GetAuthContextPropertyAsList(**full_handshake_context,
                                   GRPC_X509_SAN_PROPERTY_NAME),
      UnorderedElementsAre("*.test.google.fr", "waterzooi.test.google.be",
                           "*.test.youtube.com", "192.168.1.3"));
  EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                   GRPC_X509_PEM_CERT_PROPERTY_NAME),
            GetFileContents(kServerCertPath));
  EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                   GRPC_X509_PEM_CERT_CHAIN_PROPERTY_NAME),
            GetFileContents(kServerCertPath));
  EXPECT_THAT(
      GetAuthContextPropertyAsList(**full_handshake_context,
                                   GRPC_PEER_DNS_PROPERTY_NAME),
      UnorderedElementsAre("*.test.google.fr", "waterzooi.test.google.be",
                           "*.test.youtube.com"));
  EXPECT_THAT(GetAuthContextPropertyAsList(**full_handshake_context,
                                           GRPC_PEER_URI_PROPERTY_NAME),
              IsEmpty());
  EXPECT_THAT(GetAuthContextPropertyAsList(**full_handshake_context,
                                           GRPC_PEER_EMAIL_PROPERTY_NAME),
              IsEmpty());
  EXPECT_THAT(GetAuthContextPropertyAsList(**full_handshake_context,
                                           GRPC_PEER_IP_PROPERTY_NAME),
              UnorderedElementsAre("192.168.1.3"));
  EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                   GRPC_PEER_SPIFFE_ID_PROPERTY_NAME),
            "");
  if (GetParam().use_session_cache) {
    EXPECT_EQ(GetSessionCacheSize(cache), 1);
  }

  grpc_ssl_session_cache_destroy(cache);
}

TEST_P(SslCredentialsTest, ResumedHandshake) {
  // Skip this test if session caching is disabled.
  if (!GetParam().use_session_cache) return;

  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    std::string root_cert = GetFileContents(kCaCertPath);
    RunServer(&notification, root_cert);
  });
  notification.WaitForNotification();

  std::string root_cert = GetFileContents(kCaCertPath);
  std::string client_key = GetFileContents(kClientKeyPath);
  std::string client_cert = GetFileContents(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(16);

  auto full_handshake_context = DoRpc(ssl_options, cache);
  EXPECT_EQ(full_handshake_context.status(), absl::OkStatus());
  EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                   GRPC_SSL_SESSION_REUSED_PROPERTY),
            "false");

  auto resumed_handshake_context = DoRpc(ssl_options, cache);
  EXPECT_EQ(resumed_handshake_context.status(), absl::OkStatus());
  EXPECT_EQ(GetAuthContextProperty(**resumed_handshake_context,
                                   GRPC_SSL_SESSION_REUSED_PROPERTY),
            "true");
  EXPECT_EQ(GetAuthContextProperty(**resumed_handshake_context,
                                   GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME),
            GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  EXPECT_EQ(GetAuthContextProperty(**resumed_handshake_context,
                                   GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME),
            "TSI_PRIVACY_AND_INTEGRITY");
  EXPECT_EQ(GetAuthContextProperty(**resumed_handshake_context,
                                   GRPC_X509_CN_PROPERTY_NAME),
            "*.test.google.com");
  EXPECT_EQ(GetAuthContextProperty(**resumed_handshake_context,
                                   GRPC_X509_SUBJECT_PROPERTY_NAME),
            "CN=*.test.google.com,O=Example\\, Co.,L=Chicago,ST=Illinois,C=US");
  EXPECT_THAT(
      GetAuthContextPropertyAsList(**resumed_handshake_context,
                                   GRPC_X509_SAN_PROPERTY_NAME),
      UnorderedElementsAre("*.test.google.fr", "waterzooi.test.google.be",
                           "*.test.youtube.com", "192.168.1.3"));
  EXPECT_EQ(GetAuthContextProperty(**resumed_handshake_context,
                                   GRPC_X509_PEM_CERT_PROPERTY_NAME),
            GetFileContents(kServerCertPath));
  EXPECT_EQ(GetAuthContextProperty(**resumed_handshake_context,
                                   GRPC_X509_PEM_CERT_CHAIN_PROPERTY_NAME),
            GetFileContents(kServerCertPath));
  EXPECT_THAT(
      GetAuthContextPropertyAsList(**resumed_handshake_context,
                                   GRPC_PEER_DNS_PROPERTY_NAME),
      UnorderedElementsAre("*.test.google.fr", "waterzooi.test.google.be",
                           "*.test.youtube.com"));
  EXPECT_THAT(GetAuthContextPropertyAsList(**resumed_handshake_context,
                                           GRPC_PEER_URI_PROPERTY_NAME),
              IsEmpty());
  EXPECT_THAT(GetAuthContextPropertyAsList(**resumed_handshake_context,
                                           GRPC_PEER_EMAIL_PROPERTY_NAME),
              IsEmpty());
  EXPECT_THAT(GetAuthContextPropertyAsList(**resumed_handshake_context,
                                           GRPC_PEER_IP_PROPERTY_NAME),
              UnorderedElementsAre("192.168.1.3"));
  EXPECT_EQ(GetAuthContextProperty(**resumed_handshake_context,
                                   GRPC_PEER_SPIFFE_ID_PROPERTY_NAME),
            "");
  EXPECT_EQ(GetSessionCacheSize(cache), 1);

  grpc_ssl_session_cache_destroy(cache);
}

TEST_P(SslCredentialsTest, SequentialResumption) {
  // Skip this test if session caching is disabled.
  if (!GetParam().use_session_cache) return;

  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    std::string root_cert = GetFileContents(kCaCertPath);
    RunServer(&notification, root_cert);
  });
  notification.WaitForNotification();

  std::string root_cert = GetFileContents(kCaCertPath);
  std::string client_key = GetFileContents(kClientKeyPath);
  std::string client_cert = GetFileContents(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(16);

  auto full_handshake_context = DoRpc(ssl_options, cache);
  EXPECT_EQ(full_handshake_context.status(), absl::OkStatus());
  EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                   GRPC_SSL_SESSION_REUSED_PROPERTY),
            "false");
  for (int i = 0; i < 10; i++) {
    auto resumed_handshake_context = DoRpc(ssl_options, cache);
    EXPECT_EQ(resumed_handshake_context.status(), absl::OkStatus());
    EXPECT_EQ(GetAuthContextProperty(**resumed_handshake_context,
                                     GRPC_SSL_SESSION_REUSED_PROPERTY),
              "true");
  }

  grpc_ssl_session_cache_destroy(cache);
}

TEST_P(SslCredentialsTest, ConcurrentResumption) {
  // Skip this test if session caching is disabled.
  if (!GetParam().use_session_cache) return;

  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    std::string root_cert = GetFileContents(kCaCertPath);
    RunServer(&notification, root_cert);
  });
  notification.WaitForNotification();

  std::string root_cert = GetFileContents(kCaCertPath);
  std::string client_key = GetFileContents(kClientKeyPath);
  std::string client_cert = GetFileContents(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(16);

  auto full_handshake_context = DoRpc(ssl_options, cache);
  EXPECT_EQ(full_handshake_context.status(), absl::OkStatus());
  EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                   GRPC_SSL_SESSION_REUSED_PROPERTY),
            "false");
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; i++) {
    threads.push_back(std::thread([&]() {
      auto resumed_handshake_context = DoRpc(ssl_options, cache);
      EXPECT_EQ(resumed_handshake_context.status(), absl::OkStatus());
      EXPECT_EQ(GetAuthContextProperty(**resumed_handshake_context,
                                       GRPC_SSL_SESSION_REUSED_PROPERTY),
                "true");
    }));
  }
  for (auto& t : threads) {
    t.join();
  }

  grpc_ssl_session_cache_destroy(cache);
}

TEST_P(SslCredentialsTest, ResumptionFailsDueToNoCapacityInCache) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    std::string root_cert = GetFileContents(kCaCertPath);
    RunServer(&notification, root_cert);
  });
  notification.WaitForNotification();

  std::string root_cert = GetFileContents(kCaCertPath);
  std::string client_key = GetFileContents(kClientKeyPath);
  std::string client_cert = GetFileContents(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(0);

  for (int i = 0; i < 2; ++i) {
    auto full_handshake_context = DoRpc(ssl_options, cache);
    EXPECT_EQ(full_handshake_context.status(), absl::OkStatus());
    EXPECT_EQ(GetAuthContextProperty(**full_handshake_context,
                                     GRPC_SSL_SESSION_REUSED_PROPERTY),
              "false");
  }

  grpc_ssl_session_cache_destroy(cache);
}

TEST_P(SslCredentialsTest, ServerCertificateIsUntrusted) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    std::string root_cert = GetFileContents(kCaCertPath);
    RunServer(&notification, root_cert);
  });
  notification.WaitForNotification();

  // Use the client's own leaf cert as the root cert, so that the server's cert
  // will not be trusted by the client.
  std::string root_cert = GetFileContents(kClientCertPath);
  std::string client_key = GetFileContents(kClientKeyPath);
  std::string client_cert = GetFileContents(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(0);

  auto auth_context = DoRpc(ssl_options, cache);
  EXPECT_EQ(auth_context.status().code(), absl::StatusCode::kUnavailable);
  EXPECT_THAT(auth_context.status().message(),
              HasSubstr("CERTIFICATE_VERIFY_FAILED"));
  EXPECT_EQ(GetSessionCacheSize(cache), 0);

  grpc_ssl_session_cache_destroy(cache);
}

TEST_P(SslCredentialsTest, ClientCertificateIsUntrusted) {
  // Skip this test if the client certificate is not requested.
  if (GetParam().request_type == grpc_ssl_client_certificate_request_type::
                                     GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE) {
    return;
  }

  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    // Use the server's own leaf cert as the root cert, so that the client's
    // cert will not be trusted by the server.
    std::string root_cert = GetFileContents(kServerCertPath);
    RunServer(&notification, root_cert);
  });
  notification.WaitForNotification();

  std::string root_cert = GetFileContents(kCaCertPath);
  std::string client_key = GetFileContents(kClientKeyPath);
  std::string client_cert = GetFileContents(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(0);

  auto auth_context = DoRpc(ssl_options, cache);
  if (GetParam().request_type ==
          grpc_ssl_client_certificate_request_type::
              GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY ||
      GetParam().request_type ==
          grpc_ssl_client_certificate_request_type::
              GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY) {
    EXPECT_EQ(auth_context.status().code(), absl::StatusCode::kUnavailable);
    // TODO(matthewstevenson88): Investigate having a more descriptive error
    // message for the client.
    EXPECT_THAT(auth_context.status().message(),
                HasSubstr("failed to connect"));
    EXPECT_EQ(GetSessionCacheSize(cache), 0);
  } else {
    // TODO(matthewstevenson88): The handshake fails with a certificate
    // verification error in these cases. This is a bug. Fix this.
  }

  grpc_ssl_session_cache_destroy(cache);
}

TEST_P(SslCredentialsTest, ServerHostnameVerificationFails) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    std::string root_cert = GetFileContents(kCaCertPath);
    RunServer(&notification, root_cert);
  });
  notification.WaitForNotification();

  std::string root_cert = GetFileContents(kCaCertPath);
  std::string client_key = GetFileContents(kClientKeyPath);
  std::string client_cert = GetFileContents(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(0);

  auto auth_context =
      DoRpc(ssl_options, cache, /*override_ssl_target_name=*/false);
  EXPECT_EQ(auth_context.status().code(), absl::StatusCode::kUnavailable);
  // TODO(matthewstevenson88): Logs say "No match found for server name:
  // localhost." but this error is not propagated to the user. Fix this.
  EXPECT_FALSE(auth_context.status().message().empty());
  EXPECT_EQ(GetSessionCacheSize(cache), 0);

  grpc_ssl_session_cache_destroy(cache);
}

std::vector<SslOptions> GetSslOptions() {
  std::vector<SslOptions> ssl_options;
  for (grpc_ssl_client_certificate_request_type type :
       {grpc_ssl_client_certificate_request_type::
            GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
        grpc_ssl_client_certificate_request_type::
            GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
        grpc_ssl_client_certificate_request_type::
            GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY,
        grpc_ssl_client_certificate_request_type::
            GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
        grpc_ssl_client_certificate_request_type::
            GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY}) {
    for (bool use_session_cache : {false, true}) {
      SslOptions option = {type, use_session_cache};
      ssl_options.push_back(option);
    }
  }
  return ssl_options;
}

INSTANTIATE_TEST_SUITE_P(SslCredentials, SslCredentialsTest,
                         ::testing::ValuesIn(GetSslOptions()));

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
