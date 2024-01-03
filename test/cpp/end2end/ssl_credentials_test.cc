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

#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/notification.h"

#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/cpp/client/secure_credentials.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

// TODO(matthewstevenson88): More test cases to add:
// - Use P256, P384, P512 credentials.
// - Use a long certificate chain.
// - Use a large certificate.
// - Cert verification failure.
// - Large trust bundle.
// - Bad ALPN.
// - In TLS 1.2, play with the ciphersuites.
// - More failure modes.
// - Certs containing more SANs.
// - Copy all of this over to tls_credentials_test.cc.
// - Client doesn't have cert but server requests one.

namespace grpc {
namespace testing {
namespace {

constexpr char kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
constexpr char kServerCertPath[] = "src/core/tsi/test_creds/server1.pem";
constexpr char kServerKeyPath[] = "src/core/tsi/test_creds/server1.key";
constexpr char kClientCertPath[] = "src/core/tsi/test_creds/client.pem";
constexpr char kClientKeyPath[] = "src/core/tsi/test_creds/client.key";
constexpr char kMessage[] = "Hello";

std::string ReadFile(const std::string& file_path) {
  grpc_slice slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(file_path.c_str(), 0, &slice)));
  std::string file_contents(grpc_core::StringViewFromSlice(slice));
  grpc_slice_unref(slice);
  return file_contents;
}

void SetTlsVersion(grpc_tls_version tls_version, ChannelCredentials* creds) {
  grpc_channel_credentials* secure_creds =
      reinterpret_cast<SecureChannelCredentials*>(creds)->GetRawCreds();
  grpc_ssl_credentials* ssl_creds =
      reinterpret_cast<grpc_ssl_credentials*>(secure_creds);
  ssl_creds->set_min_tls_version(tls_version);
  ssl_creds->set_max_tls_version(tls_version);
}

struct SslOptions {
  grpc_ssl_client_certificate_request_type request_type =
      GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
  grpc_tls_version tls_version;
  bool use_session_cache;
};

class SslCredentialsTest : public ::testing::TestWithParam<SslOptions> {
 protected:
  void RunServer(absl::Notification* notification) {
    std::string root_cert = ReadFile(kCaCertPath);
    grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair = {
        ReadFile(kServerKeyPath), ReadFile(kServerCertPath)};
    grpc::SslServerCredentialsOptions ssl_options(GetParam().request_type);
    ssl_options.pem_key_cert_pairs.push_back(key_cert_pair);
    ssl_options.pem_root_certs = root_cert;

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
      const SslCredentialsOptions& ssl_options, grpc_ssl_session_cache* cache) {
    ChannelArguments channel_args;
    if (GetParam().use_session_cache) {
      channel_args.SetPointer(std::string(GRPC_SSL_SESSION_CACHE_ARG), cache);
    }
    channel_args.SetSslTargetNameOverride("foo.test.google.fr");

    auto creds = grpc::SslCredentials(ssl_options);
    SetTlsVersion(GetParam().tls_version, creds.get());
    std::shared_ptr<Channel> channel = grpc::CreateCustomChannel(
        server_addr_, grpc::SslCredentials(ssl_options), channel_args);

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

  void ExpectResumedSession(std::shared_ptr<const AuthContext> auth_context) {
    std::vector<grpc::string_ref> properties =
        auth_context->FindPropertyValues(GRPC_SSL_SESSION_REUSED_PROPERTY);
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ("true", ToString(properties[0]));
  }

  void ExpectNonResumedSession(
      std::shared_ptr<const AuthContext> auth_context) {
    std::vector<grpc::string_ref> properties =
        auth_context->FindPropertyValues(GRPC_SSL_SESSION_REUSED_PROPERTY);
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ("false", ToString(properties[0]));
  }

  void ExpectOk(const absl::Status& status) {
    EXPECT_EQ(status.code(), absl::StatusCode::kOk);
    EXPECT_EQ(status.message(), "");
  }

  void ExpectTransportSecurityType(
      std::shared_ptr<const AuthContext> auth_context) {
    std::vector<grpc::string_ref> properties = auth_context->FindPropertyValues(
        GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ(ToString(properties[0]), GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  }

  void ExpectTransportSecurityLevel(
      std::shared_ptr<const AuthContext> auth_context) {
    std::vector<grpc::string_ref> properties = auth_context->FindPropertyValues(
        GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ(ToString(properties[0]), "TSI_PRIVACY_AND_INTEGRITY");
  }

  void ExpectPeerCommonName(std::shared_ptr<const AuthContext> auth_context,
                            absl::string_view common_name) {
    std::vector<grpc::string_ref> properties =
        auth_context->FindPropertyValues(GRPC_X509_CN_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ(ToString(properties[0]), common_name);
  }

  void ExpectPeerSubject(std::shared_ptr<const AuthContext> auth_context,
                         absl::string_view subject) {
    std::vector<grpc::string_ref> properties =
        auth_context->FindPropertyValues(GRPC_X509_SUBJECT_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ(ToString(properties[0]), subject);
  }

  void ExpectPeerSubjectAltName(std::shared_ptr<const AuthContext> auth_context,
                                const absl::flat_hash_set<std::string>& sans) {
    std::vector<grpc::string_ref> properties =
        auth_context->FindPropertyValues(GRPC_X509_SAN_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), sans.size());
    absl::flat_hash_set<std::string> observed_sans;
    for (int i = 0; i < properties.size(); ++i) {
      observed_sans.insert(ToString(properties[i]));
    }
    EXPECT_EQ(sans, observed_sans);
  }

  void ExpectPeerCertPem(std::shared_ptr<const AuthContext> auth_context,
                         absl::string_view pem) {
    std::vector<grpc::string_ref> properties =
        auth_context->FindPropertyValues(GRPC_X509_PEM_CERT_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ(ToString(properties[0]), pem);
  }

  // TODO(mattstevenson88): Check this property when there are >= 2 untrusted
  // certs in the chain.
  void ExpectPeerCertChainPem(std::shared_ptr<const AuthContext> auth_context,
                              absl::string_view pem) {
    std::vector<grpc::string_ref> properties = auth_context->FindPropertyValues(
        GRPC_X509_PEM_CERT_CHAIN_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ(ToString(properties[0]), pem);
  }

  void ExpectPeerDnsSans(std::shared_ptr<const AuthContext> auth_context,
                         const absl::flat_hash_set<std::string>& dns_sans) {
    std::vector<grpc::string_ref> properties =
        auth_context->FindPropertyValues(GRPC_PEER_DNS_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), dns_sans.size());
    absl::flat_hash_set<std::string> observed_sans;
    for (int i = 0; i < properties.size(); ++i) {
      observed_sans.insert(ToString(properties[i]));
    }
    EXPECT_EQ(dns_sans, observed_sans);
  }

  // TODO(matthewstevenson88): Check this property when there are URI SANs in
  // the peer leaf cert.
  void ExpectPeerUriSans(std::shared_ptr<const AuthContext> auth_context,
                         const absl::flat_hash_set<std::string>& uri_sans) {
    std::vector<grpc::string_ref> properties =
        auth_context->FindPropertyValues(GRPC_PEER_URI_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), uri_sans.size());
    absl::flat_hash_set<std::string> observed_sans;
    for (int i = 0; i < properties.size(); ++i) {
      observed_sans.insert(ToString(properties[i]));
    }
    EXPECT_EQ(uri_sans, observed_sans);
  }

  // TODO(matthewstevenson88): Check this property when there are email SANs in
  // the peer leaf cert.
  void ExpectPeerEmailSans(std::shared_ptr<const AuthContext> auth_context,
                           const absl::flat_hash_set<std::string>& email_sans) {
    std::vector<grpc::string_ref> properties =
        auth_context->FindPropertyValues(GRPC_PEER_EMAIL_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), email_sans.size());
    absl::flat_hash_set<std::string> observed_sans;
    for (int i = 0; i < properties.size(); ++i) {
      observed_sans.insert(ToString(properties[i]));
    }
    EXPECT_EQ(email_sans, observed_sans);
  }

  // TODO(matthewstevenson88): Check this property when there are IP SANs in
  // the peer leaf cert.
  void ExpectPeerIpSans(std::shared_ptr<const AuthContext> auth_context,
                        const absl::flat_hash_set<std::string>& ip_sans) {
    std::vector<grpc::string_ref> properties =
        auth_context->FindPropertyValues(GRPC_PEER_IP_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), ip_sans.size());
    absl::flat_hash_set<std::string> observed_sans;
    for (int i = 0; i < properties.size(); ++i) {
      observed_sans.insert(ToString(properties[i]));
    }
    EXPECT_EQ(ip_sans, observed_sans);
  }

  // TODO(matthewstevenson88): Check this property when there is a SPIFFE ID in
  // the peer leaf cert.
  void ExpectNoSpiffeId(std::shared_ptr<const AuthContext> auth_context) {
    std::vector<grpc::string_ref> properties =
        auth_context->FindPropertyValues(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), 0);
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

  auto full_handshake_context = DoRpc(ssl_options, cache);
  ExpectOk(full_handshake_context.status());
  ExpectNonResumedSession(*full_handshake_context);
  ExpectTransportSecurityType(*full_handshake_context);
  ExpectTransportSecurityLevel(*full_handshake_context);
  ExpectPeerCommonName(*full_handshake_context, "*.test.google.com");
  ExpectPeerSubject(
      *full_handshake_context,
      ""
      "CN=*.test.google.com,O=Example\\, Co.,L=Chicago,ST=Illinois,C=US");
  ExpectPeerSubjectAltName(*full_handshake_context,
                           {"*.test.google.fr", "waterzooi.test.google.be",
                            "*.test.youtube.com", "192.168.1.3"});
  ExpectPeerCertPem(*full_handshake_context, ReadFile(kServerCertPath));
  ExpectPeerCertChainPem(*full_handshake_context, ReadFile(kServerCertPath));
  ExpectPeerDnsSans(
      *full_handshake_context,
      {"*.test.google.fr", "waterzooi.test.google.be", "*.test.youtube.com"});
  ExpectPeerUriSans(*full_handshake_context, /*uri_sans=*/{});
  ExpectPeerEmailSans(*full_handshake_context, /*email_sans=*/{});
  ExpectPeerIpSans(*full_handshake_context, {"192.168.1.3"});
  ExpectNoSpiffeId(*full_handshake_context);

  grpc_ssl_session_cache_destroy(cache);
}

TEST_P(SslCredentialsTest, ResumedHandshake) {
  // Skip this test if session caching is disabled.
  if (!GetParam().use_session_cache) return;

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

  auto full_handshake_context = DoRpc(ssl_options, cache);
  ExpectOk(full_handshake_context.status());
  ExpectNonResumedSession(*full_handshake_context);

  auto resumed_handshake_context = DoRpc(ssl_options, cache);
  ExpectOk(resumed_handshake_context.status());
  ExpectResumedSession(*resumed_handshake_context);
  ExpectTransportSecurityType(*resumed_handshake_context);
  ExpectTransportSecurityLevel(*resumed_handshake_context);
  ExpectPeerCommonName(*resumed_handshake_context, "*.test.google.com");
  ExpectPeerSubject(
      *resumed_handshake_context,
      ""
      "CN=*.test.google.com,O=Example\\, Co.,L=Chicago,ST=Illinois,C=US");
  ExpectPeerSubjectAltName(*resumed_handshake_context,
                           {"*.test.google.fr", "waterzooi.test.google.be",
                            "*.test.youtube.com", "192.168.1.3"});
  ExpectPeerCertPem(*resumed_handshake_context, ReadFile(kServerCertPath));
  ExpectPeerCertChainPem(*resumed_handshake_context, ReadFile(kServerCertPath));
  ExpectPeerDnsSans(
      *resumed_handshake_context,
      {"*.test.google.fr", "waterzooi.test.google.be", "*.test.youtube.com"});
  ExpectPeerUriSans(*resumed_handshake_context, /*uri_sans=*/{});
  ExpectPeerEmailSans(*resumed_handshake_context, /*email_sans=*/{});
  ExpectPeerIpSans(*resumed_handshake_context, {"192.168.1.3"});
  ExpectNoSpiffeId(*resumed_handshake_context);

  grpc_ssl_session_cache_destroy(cache);
}

TEST_P(SslCredentialsTest, SequentialResumption) {
  // Skip this test if session caching is disabled.
  if (!GetParam().use_session_cache) return;

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

  auto full_handshake_context = DoRpc(ssl_options, cache);
  ExpectOk(full_handshake_context.status());
  ExpectNonResumedSession(*full_handshake_context);
  for (int i = 0; i < 10; i++) {
    auto resumed_handshake_context = DoRpc(ssl_options, cache);
    ExpectOk(resumed_handshake_context.status());
    ExpectResumedSession(*resumed_handshake_context);
  }

  grpc_ssl_session_cache_destroy(cache);
}

TEST_P(SslCredentialsTest, ConcurrentResumption) {
  // Skip this test if session caching is disabled.
  if (!GetParam().use_session_cache) return;

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

  auto full_handshake_context = DoRpc(ssl_options, cache);
  ExpectOk(full_handshake_context.status());
  ExpectNonResumedSession(*full_handshake_context);
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; i++) {
    threads.push_back(std::thread([&]() {
      auto resumed_handshake_context = DoRpc(ssl_options, cache);
      ExpectOk(resumed_handshake_context.status());
      ExpectResumedSession(*resumed_handshake_context);
    }));
  }
  for (auto& t : threads) {
    t.join();
  }

  grpc_ssl_session_cache_destroy(cache);
}

TEST_P(SslCredentialsTest, ResumptionFailsDueToNoCapacityInCache) {
  // Skip this test if session caching is disabled.
  if (!GetParam().use_session_cache) return;

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

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(0);

  for (int i = 0; i < 2; ++i) {
    auto full_handshake_context = DoRpc(ssl_options, cache);
    ExpectOk(full_handshake_context.status());
    ExpectNonResumedSession(*full_handshake_context);
  }

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
    for (grpc_tls_version version :
         {grpc_tls_version::TLS1_2, grpc_tls_version::TLS1_3}) {
      for (bool use_session_cache : {false, true}) {
        SslOptions option = {type, version, use_session_cache};
        ssl_options.push_back(option);
      }
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
