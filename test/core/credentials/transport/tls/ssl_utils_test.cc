//
//
// Copyright 2026 gRPC authors.
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

#include "src/core/credentials/transport/tls/ssl_utils.h"

#include <grpc/grpc_crl_provider.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/private_key_signer.h>
#include <grpc/support/alloc.h>

#include <cstring>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "src/core/lib/debug/trace.h"
#include "src/core/transport/auth_context.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/log_severity.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace {

class FakePrivateKeySigner : public PrivateKeySigner {
 public:
  std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
  Sign(absl::string_view /*data_to_sign*/,
       SignatureAlgorithm /*signature_algorithm*/,
       OnSignComplete /*on_sign_complete*/) override {
    return absl::UnimplementedError("Sign is unimplemented");
  }
  void Cancel(std::shared_ptr<AsyncSigningHandle> /*handle*/) override {}
};

void FreeAlpnStrings(const char** protocols, size_t num_protocols) {
  for (size_t i = 0; i < num_protocols; ++i) {
    gpr_free((void*)protocols[i]);
  }
  gpr_free((void*)protocols);
}

TEST(SslUtilsTest, IsPrivateKeyEmpty) {
  EXPECT_FALSE(IsPrivateKeyEmpty(tsi::PrivateKey("fake-pem-key")));
  EXPECT_TRUE(IsPrivateKeyEmpty(tsi::PrivateKey("")));
  EXPECT_FALSE(IsPrivateKeyEmpty(
      tsi::PrivateKey(std::make_shared<FakePrivateKeySigner>())));
  EXPECT_TRUE(IsPrivateKeyEmpty(
      tsi::PrivateKey(std::shared_ptr<PrivateKeySigner>(nullptr))));
}

TEST(SslUtilsTest, GetTsiClientCertificateRequestType) {
  EXPECT_EQ(grpc_get_tsi_client_certificate_request_type(
                GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE),
            TSI_DONT_REQUEST_CLIENT_CERTIFICATE);
  EXPECT_EQ(grpc_get_tsi_client_certificate_request_type(
                GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY),
            TSI_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY);
  EXPECT_EQ(grpc_get_tsi_client_certificate_request_type(
                GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY),
            TSI_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
  EXPECT_EQ(
      grpc_get_tsi_client_certificate_request_type(
          GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY),
      TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY);
  EXPECT_EQ(grpc_get_tsi_client_certificate_request_type(
                GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY),
            TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
}

TEST(SslUtilsTest, GetTsiTlsVersion) {
  EXPECT_EQ(grpc_get_tsi_tls_version(grpc_tls_version::TLS1_2),
            tsi_tls_version::TSI_TLS1_2);
  EXPECT_EQ(grpc_get_tsi_tls_version(grpc_tls_version::TLS1_3),
            tsi_tls_version::TSI_TLS1_3);
}

TEST(SslUtilsTest, ParseAlpnStringIntoArrayEmpty) {
  size_t num_protocols;
  const char** protocols = ParseAlpnStringIntoArray("", &num_protocols);
  EXPECT_EQ(num_protocols, 0);
  EXPECT_EQ(protocols, nullptr);
}

TEST(SslUtilsTest, ParseAlpnStringIntoArraySingle) {
  size_t num_protocols;
  const char** protocols = ParseAlpnStringIntoArray("h2", &num_protocols);
  EXPECT_EQ(num_protocols, 1);
  ASSERT_NE(protocols, nullptr);
  EXPECT_STREQ(protocols[0], "h2");
  FreeAlpnStrings(protocols, num_protocols);
}

TEST(SslUtilsTest, ParseAlpnStringIntoArrayMultiple) {
  size_t num_protocols;
  const char** protocols =
      ParseAlpnStringIntoArray("grpc-exp,h2", &num_protocols);
  EXPECT_EQ(num_protocols, 2);
  ASSERT_NE(protocols, nullptr);
  EXPECT_STREQ(protocols[0], "grpc-exp");
  EXPECT_STREQ(protocols[1], "h2");
  FreeAlpnStrings(protocols, num_protocols);
}

TEST(SslUtilsTest, SslHostMatchesName) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                &peer.properties[0]),
            TSI_OK);
  EXPECT_EQ(grpc_ssl_host_matches_name(&peer, "foo.bar.com"), 1);
  EXPECT_EQ(grpc_ssl_host_matches_name(&peer, "bad.bar.com"), 0);
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, SslCheckAlpnSuccess) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_SSL_ALPN_SELECTED_PROTOCOL, "h2", &peer.properties[0]),
            TSI_OK);
  EXPECT_EQ(grpc_ssl_check_alpn(&peer), absl::OkStatus());
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, SslCheckAlpnFailure) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_SSL_ALPN_SELECTED_PROTOCOL, "bad", &peer.properties[0]),
            TSI_OK);
  EXPECT_NE(grpc_ssl_check_alpn(&peer), absl::OkStatus());
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, SslCheckPeerNameMatch) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                &peer.properties[0]),
            TSI_OK);
  EXPECT_EQ(grpc_ssl_check_peer_name("foo.bar.com", &peer), absl::OkStatus());
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, SslCheckPeerNameMismatch) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                &peer.properties[0]),
            TSI_OK);
  EXPECT_NE(grpc_ssl_check_peer_name("bad.bar.com", &peer), absl::OkStatus());
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, SslCheckPeerNameEmpty) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                &peer.properties[0]),
            TSI_OK);
  EXPECT_EQ(grpc_ssl_check_peer_name("", &peer), absl::OkStatus());
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, SslCmpTargetNameTargetNameMismatch) {
  EXPECT_EQ(grpc_ssl_cmp_target_name("a", "b", "c", "d"), -1);
  EXPECT_EQ(grpc_ssl_cmp_target_name("b", "a", "c", "d"), 1);
}

TEST(SslUtilsTest, SslCmpTargetNameOverriddenTargetNameMismatch) {
  EXPECT_EQ(grpc_ssl_cmp_target_name("a", "a", "c", "d"), -1);
  EXPECT_EQ(grpc_ssl_cmp_target_name("a", "a", "d", "c"), 1);
}

TEST(SslUtilsTest, SslCmpTargetNameMatch) {
  EXPECT_EQ(grpc_ssl_cmp_target_name("a", "a", "c", "c"), 0);
}

TEST(SslUtilsTest, GetSslCipherSuites) {
  const char* cipher_suites = grpc_get_ssl_cipher_suites();
  EXPECT_NE(cipher_suites, nullptr);
  EXPECT_GT(strlen(cipher_suites), 0);
}

TEST(SslUtilsTest, FillAlpnProtocolStrings) {
  size_t num_protocols;
  const char** protocols = grpc_fill_alpn_protocol_strings(&num_protocols);
  EXPECT_GT(num_protocols, 0);
  ASSERT_NE(protocols, nullptr);
  bool h2_found = false;
  // Right now h2 is the only default support alpn protocol.
  for (size_t i = 0; i < num_protocols; ++i) {
    if (strcmp(protocols[i], "h2") == 0) {
      h2_found = true;
    }
  }
  EXPECT_TRUE(h2_found);
  gpr_free((void*)protocols);
}

TEST(SslUtilsTest, PemKeyCertPair) {
  PemKeyCertPair pair1(PrivateKey("key1"), "cert1");
  PemKeyCertPair pair2(PrivateKey("key2"), "cert2");
  PemKeyCertPair pair1_copy = pair1;
  EXPECT_EQ(std::get<std::string>(pair1.private_key()), "key1");
  EXPECT_EQ(pair1.cert_chain(), "cert1");
  EXPECT_EQ(pair1, pair1_copy);
  EXPECT_NE(pair1, pair2);
}

TEST(SslUtilsTest, PeerToAuthContextSubject) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_PEER_PROPERTY, "subject", &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_X509_SUBJECT_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("subject", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextCn) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "cn",
                &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_X509_CN_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("cn", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextSan) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, "san",
                &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_X509_SAN_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("san", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextPemCert) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_PEM_CERT_PROPERTY, "pem_cert", &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("pem_cert", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextPemCertChain) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_PEM_CERT_CHAIN_PROPERTY, "pem_cert_chain",
                &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_X509_PEM_CERT_CHAIN_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("pem_cert_chain", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextSessionReused) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_SSL_SESSION_REUSED_PEER_PROPERTY, "true", &peer.properties[0]),
      TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_SSL_SESSION_REUSED_PROPERTY);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("true", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextSecurityLevel) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_SECURITY_LEVEL_PEER_PROPERTY, "TSI_SECURITY_NONE",
                &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("TSI_SECURITY_NONE", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextDns) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_DNS_PEER_PROPERTY, "dns", &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_PEER_DNS_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("dns", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextUri) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_URI_PEER_PROPERTY, "uri", &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_PEER_URI_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("uri", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextEmail) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_EMAIL_PEER_PROPERTY, "email", &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_PEER_EMAIL_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("email", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextIp) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_IP_PEER_PROPERTY, "ip", &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_PEER_IP_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("ip", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextSpiffeId) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_X509_URI_PEER_PROPERTY, "spiffe://foo/bar", &peer.properties[0]),
      TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_PEER_SPIFFE_ID_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_EQ("spiffe://foo/bar", std::string(prop->value, prop->value_length));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextSpiffeIdMultipleUri) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(2, &peer), TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_X509_URI_PEER_PROPERTY, "spiffe://foo/bar", &peer.properties[0]),
      TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_URI_PEER_PROPERTY, "uri", &peer.properties[1]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_PEER_SPIFFE_ID_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  EXPECT_EQ(prop, nullptr);
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, PeerToAuthContextPeerIdentity) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(2, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "cn",
                &peer.properties[0]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, "san",
                &peer.properties[1]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(auth_context, nullptr);
  EXPECT_TRUE(grpc_auth_context_set_peer_identity_property_name(
      auth_context.get(), GRPC_X509_SAN_PROPERTY_NAME));
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, ShallowPeerFromSslAuthContext) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "cn",
                &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  tsi_peer shallow_peer =
      grpc_shallow_peer_from_ssl_auth_context(auth_context.get());
  bool cn_found = false;
  for (size_t i = 0; i < shallow_peer.property_count; ++i) {
    if (strcmp(shallow_peer.properties[i].name,
               TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY) == 0) {
      cn_found = true;
      EXPECT_EQ("cn", std::string(shallow_peer.properties[i].value.data,
                                  shallow_peer.properties[i].value.length));
    }
  }
  EXPECT_TRUE(cn_found);
  grpc_shallow_peer_destruct(&shallow_peer);
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, SslCheckCallHostMatch) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_EQ(SslCheckCallHost("foo.bar.com", "", "", auth_context.get()),
            absl::OkStatus());
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, SslCheckCallHostMismatch) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_NE(SslCheckCallHost("bad.bar.com", "", "", auth_context.get()),
            absl::OkStatus());
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, SslCheckCallHostWithOverride) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, "foo.bar.com",
                &peer.properties[0]),
            TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  EXPECT_EQ(SslCheckCallHost("target.name.com", "target.name.com",
                               "foo.bar.com", auth_context.get()),
            absl::OkStatus());
  tsi_peer_destruct(&peer);
}

TEST(SslUtilsTest, DefaultSslRootStore) {
  const char* pem_root_certs = DefaultSslRootStore::GetPemRootCerts();
  const tsi_ssl_root_certs_store* root_store =
      DefaultSslRootStore::GetRootStore();
  if (pem_root_certs != nullptr) {
    EXPECT_GT(strlen(pem_root_certs), 0);
    EXPECT_NE(root_store, nullptr);
  } else {
    EXPECT_EQ(root_store, nullptr);
  }
}

TEST(SslUtilsTest, ClientHandshakerFactoryInitSuccess) {
  std::string root_cert =
      testing::GetFileContents("src/core/tsi/test_creds/ca.pem");
  auto root_cert_info = std::make_shared<tsi::RootCertInfo>(root_cert);
  std::string client_key =
      testing::GetFileContents("src/core/tsi/test_creds/client.key");
  std::string client_cert =
      testing::GetFileContents("src/core/tsi/test_creds/client.pem");
  tsi_ssl_pem_key_cert_pair key_cert_pair;
  key_cert_pair.private_key = client_key;
  key_cert_pair.cert_chain = client_cert;
  tsi_ssl_client_handshaker_factory* factory = nullptr;
  EXPECT_EQ(grpc_ssl_tsi_client_handshaker_factory_init(
                &key_cert_pair, root_cert_info,
                /*skip_server_certificate_verification=*/false,
                tsi_tls_version::TSI_TLS1_2, tsi_tls_version::TSI_TLS1_3,
                /*ssl_session_cache=*/nullptr,
                /*tls_session_key_logger=*/nullptr,
                /*crl_directory=*/nullptr,
                /*crl_provider=*/nullptr, &factory),
            GRPC_SECURITY_OK);
  ASSERT_NE(factory, nullptr);
  tsi_ssl_client_handshaker_factory_unref(factory);
}

TEST(SslUtilsTest, ClientHandshakerFactoryInitWithoutKeyCertPair) {
  std::string root_cert =
      testing::GetFileContents("src/core/tsi/test_creds/ca.pem");
  auto root_cert_info = std::make_shared<tsi::RootCertInfo>(root_cert);
  tsi_ssl_client_handshaker_factory* factory = nullptr;
  EXPECT_EQ(grpc_ssl_tsi_client_handshaker_factory_init(
                /*key_cert_pair=*/nullptr, root_cert_info,
                /*skip_server_certificate_verification=*/false,
                tsi_tls_version::TSI_TLS1_2, tsi_tls_version::TSI_TLS1_3,
                /*ssl_session_cache=*/nullptr,
                /*tls_session_key_logger=*/nullptr,
                /*crl_directory=*/nullptr,
                /*crl_provider=*/nullptr, &factory),
            GRPC_SECURITY_OK);
  ASSERT_NE(factory, nullptr);
  tsi_ssl_client_handshaker_factory_unref(factory);
}

TEST(SslUtilsTest, ClientHandshakerFactoryInitSkipServerVerification) {
  tsi_ssl_client_handshaker_factory* factory = nullptr;
  EXPECT_EQ(grpc_ssl_tsi_client_handshaker_factory_init(
                /*key_cert_pair=*/nullptr, /*root_cert_info=*/nullptr,
                /*skip_server_certificate_verification=*/true,
                tsi_tls_version::TSI_TLS1_2, tsi_tls_version::TSI_TLS1_3,
                /*ssl_session_cache=*/nullptr,
                /*tls_session_key_logger=*/nullptr,
                /*crl_directory=*/nullptr,
                /*crl_provider=*/nullptr, &factory),
            GRPC_SECURITY_OK);
  ASSERT_NE(factory, nullptr);
  tsi_ssl_client_handshaker_factory_unref(factory);
}

TEST(SslUtilsTest, ClientHandshakerFactoryInitNoRootCertInfoAndNoSkipFails) {
  tsi_ssl_client_handshaker_factory* factory = nullptr;
  EXPECT_EQ(grpc_ssl_tsi_client_handshaker_factory_init(
                /*key_cert_pair=*/nullptr, /*root_cert_info=*/nullptr,
                /*skip_server_certificate_verification=*/false,
                tsi_tls_version::TSI_TLS1_2, tsi_tls_version::TSI_TLS1_3,
                /*ssl_session_cache=*/nullptr,
                /*tls_session_key_logger=*/nullptr,
                /*crl_directory=*/nullptr,
                /*crl_provider=*/nullptr, &factory),
            GRPC_SECURITY_ERROR);
  ASSERT_EQ(factory, nullptr);
}

TEST(SslUtilsTest, ClientHandshakerFactoryInitBadRootCertFails) {
  auto root_cert_info = std::make_shared<tsi::RootCertInfo>("bad root cert");
  tsi_ssl_client_handshaker_factory* factory = nullptr;
  EXPECT_EQ(grpc_ssl_tsi_client_handshaker_factory_init(
                /*key_cert_pair=*/nullptr, root_cert_info,
                /*skip_server_certificate_verification=*/false,
                tsi_tls_version::TSI_TLS1_2, tsi_tls_version::TSI_TLS1_3,
                /*ssl_session_cache=*/nullptr,
                /*tls_session_key_logger=*/nullptr,
                /*crl_directory=*/nullptr,
                /*crl_provider=*/nullptr, &factory),
            GRPC_SECURITY_ERROR);
  ASSERT_EQ(factory, nullptr);
}

TEST(SslUtilsTest, ServerHandshakerFactoryInitSuccess) {
  std::string root_cert =
      testing::GetFileContents("src/core/tsi/test_creds/ca.pem");
  auto root_cert_info = std::make_shared<tsi::RootCertInfo>(root_cert);
  std::string server_key =
      testing::GetFileContents("src/core/tsi/test_creds/server1.key");
  std::string server_cert =
      testing::GetFileContents("src/core/tsi/test_creds/server1.pem");
  tsi_ssl_pem_key_cert_pair key_cert_pair;
  key_cert_pair.private_key = server_key;
  key_cert_pair.cert_chain = server_cert;
  std::vector<tsi_ssl_pem_key_cert_pair> key_cert_pairs;
  key_cert_pairs.push_back(key_cert_pair);
  tsi_ssl_server_handshaker_factory* factory = nullptr;
  EXPECT_EQ(grpc_ssl_tsi_server_handshaker_factory_init(
                key_cert_pairs, root_cert_info,
                GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
                tsi_tls_version::TSI_TLS1_2, tsi_tls_version::TSI_TLS1_3,
                /*tls_session_key_logger=*/nullptr,
                /*crl_directory=*/nullptr, /*send_client_ca_list=*/true,
                /*crl_provider=*/nullptr, &factory),
            GRPC_SECURITY_OK);
  ASSERT_NE(factory, nullptr);
  tsi_ssl_server_handshaker_factory_unref(factory);
}

TEST(SslUtilsTest, ServerHandshakerFactoryInitBadKeyCert) {
  std::string root_cert =
      testing::GetFileContents("src/core/tsi/test_creds/ca.pem");
  auto root_cert_info = std::make_shared<tsi::RootCertInfo>(root_cert);
  tsi_ssl_pem_key_cert_pair key_cert_pair;
  key_cert_pair.private_key = "bad key";
  key_cert_pair.cert_chain = "bad cert";
  std::vector<tsi_ssl_pem_key_cert_pair> key_cert_pairs;
  key_cert_pairs.push_back(key_cert_pair);
  tsi_ssl_server_handshaker_factory* factory = nullptr;
  EXPECT_EQ(grpc_ssl_tsi_server_handshaker_factory_init(
                key_cert_pairs, root_cert_info,
                GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
                tsi_tls_version::TSI_TLS1_2, tsi_tls_version::TSI_TLS1_3,
                /*tls_session_key_logger=*/nullptr,
                /*crl_directory=*/nullptr, /*send_client_ca_list=*/true,
                /*crl_provider=*/nullptr, &factory),
            GRPC_SECURITY_ERROR);
  ASSERT_EQ(factory, nullptr);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
