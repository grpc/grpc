//
//
// Copyright 2015 gRPC authors.
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

#include "src/core/lib/security/security_connector/security_connector.h"

#include <stdio.h>
#include <string.h>

#include <gtest/gtest.h>

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "test/core/util/test_config.h"

#ifndef TSI_OPENSSL_ALPN_SUPPORT
#define TSI_OPENSSL_ALPN_SUPPORT 1
#endif

static int check_peer_property(const tsi_peer* peer,
                               const tsi_peer_property* expected) {
  size_t i;
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property* prop = &peer->properties[i];
    if ((strcmp(prop->name, expected->name) == 0) &&
        (prop->value.length == expected->value.length) &&
        (memcmp(prop->value.data, expected->value.data,
                expected->value.length) == 0)) {
      return 1;
    }
  }
  return 0;  // Not found...
}

static int check_ssl_peer_equivalence(const tsi_peer* original,
                                      const tsi_peer* reconstructed) {
  // The reconstructed peer only has CN, SAN and pem cert properties.
  size_t i;
  for (i = 0; i < original->property_count; i++) {
    const tsi_peer_property* prop = &original->properties[i];
    if ((strcmp(prop->name, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY) == 0) ||
        (strcmp(prop->name, TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) ==
         0) ||
        (strcmp(prop->name, TSI_X509_PEM_CERT_PROPERTY) == 0)) {
      if (!check_peer_property(reconstructed, prop)) return 0;
    }
  }
  return 1;
}

static int check_property(const grpc_auth_context* ctx,
                          const char* expected_property_name,
                          const char* expected_property_value) {
  grpc_auth_property_iterator it =
      grpc_auth_context_find_properties_by_name(ctx, expected_property_name);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (prop == nullptr) {
    gpr_log(GPR_ERROR, "Expected value %s not found.", expected_property_value);
    return 0;
  }
  if (strncmp(prop->value, expected_property_value, prop->value_length) != 0) {
    gpr_log(GPR_ERROR, "Expected value %s and got %s for property %s.",
            expected_property_value, prop->value, expected_property_name);
    return 0;
  }
  if (grpc_auth_property_iterator_next(&it) != nullptr) {
    gpr_log(GPR_ERROR, "Expected only one property for %s.",
            expected_property_name);
    return 0;
  }
  return 1;
}

static int check_properties(
    const grpc_auth_context* ctx, const char* expected_property_name,
    const std::vector<std::string>& expected_property_values) {
  grpc_auth_property_iterator it =
      grpc_auth_context_find_properties_by_name(ctx, expected_property_name);
  for (const auto& property_value : expected_property_values) {
    const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
    if (prop == nullptr) {
      gpr_log(GPR_ERROR, "Expected value %s not found.",
              property_value.c_str());
      return 0;
    }
    if (strcmp(prop->name, expected_property_name) != 0) {
      gpr_log(GPR_ERROR, "Expected peer property name %s and got %s.",
              expected_property_name, prop->name);
      return 0;
    }
    if (strncmp(prop->value, property_value.c_str(), prop->value_length) != 0) {
      gpr_log(GPR_ERROR, "Expected peer property value %s and got %s.",
              property_value.c_str(), prop->value);
      return 0;
    }
  }
  if (grpc_auth_property_iterator_next(&it) != nullptr) {
    gpr_log(GPR_ERROR, "Expected only %zu property values.",
            expected_property_values.size());
    return 0;
  }
  return 1;
}

static int check_spiffe_id(const grpc_auth_context* ctx,
                           const char* expected_spiffe_id,
                           bool expect_spiffe_id) {
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      ctx, GRPC_PEER_SPIFFE_ID_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (prop == nullptr && !expect_spiffe_id) {
    return 1;
  }
  if (prop != nullptr && !expect_spiffe_id) {
    gpr_log(GPR_ERROR, "SPIFFE ID not expected, but got %s.", prop->value);
    return 0;
  }
  if (prop == nullptr && expect_spiffe_id) {
    gpr_log(GPR_ERROR, "SPIFFE ID expected, but got nullptr.");
    return 0;
  }
  if (strncmp(prop->value, expected_spiffe_id, prop->value_length) != 0) {
    gpr_log(GPR_ERROR, "Expected SPIFFE ID %s but got %s.", expected_spiffe_id,
            prop->value);
    return 0;
  }
  if (grpc_auth_property_iterator_next(&it) != nullptr) {
    gpr_log(GPR_ERROR, "Expected only one property for SPIFFE ID.");
    return 0;
  }
  return 1;
}

static void test_unauthenticated_ssl_peer(void) {
  tsi_peer peer;
  tsi_peer rpeer;
  ASSERT_EQ(tsi_construct_peer(2, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
                &peer.properties[0]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_SECURITY_LEVEL_PEER_PROPERTY,
                tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
                &peer.properties[1]),
            TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(ctx, nullptr);
  ASSERT_FALSE(grpc_auth_context_peer_is_authenticated(ctx.get()));
  ASSERT_TRUE(check_property(ctx.get(),
                             GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                             GRPC_SSL_TRANSPORT_SECURITY_TYPE));

  rpeer = grpc_shallow_peer_from_ssl_auth_context(ctx.get());
  ASSERT_TRUE(check_ssl_peer_equivalence(&peer, &rpeer));

  grpc_shallow_peer_destruct(&rpeer);
  tsi_peer_destruct(&peer);
  ctx.reset(DEBUG_LOCATION, "test");
}

static void test_cn_only_ssl_peer_to_auth_context(void) {
  tsi_peer peer;
  tsi_peer rpeer;
  const char* expected_cn = "cn1";
  const char* expected_pem_cert = "pem_cert1";
  const char* expected_pem_cert_chain = "pem_cert1_chain";
  ASSERT_EQ(tsi_construct_peer(5, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
                &peer.properties[0]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, expected_cn,
                &peer.properties[1]),
            TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_X509_PEM_CERT_PROPERTY, expected_pem_cert, &peer.properties[2]),
      TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_SECURITY_LEVEL_PEER_PROPERTY,
                tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
                &peer.properties[3]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_PEM_CERT_CHAIN_PROPERTY, expected_pem_cert_chain,
                &peer.properties[4]),
            TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(ctx, nullptr);
  ASSERT_TRUE(grpc_auth_context_peer_is_authenticated(ctx.get()));
  ASSERT_TRUE(
      check_property(ctx.get(), GRPC_X509_CN_PROPERTY_NAME, expected_cn));
  ASSERT_TRUE(check_property(ctx.get(),
                             GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                             GRPC_SSL_TRANSPORT_SECURITY_TYPE));
  ASSERT_TRUE(
      check_property(ctx.get(), GRPC_X509_CN_PROPERTY_NAME, expected_cn));
  ASSERT_TRUE(check_property(ctx.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME,
                             expected_pem_cert));
  ASSERT_TRUE(check_property(ctx.get(), GRPC_X509_PEM_CERT_CHAIN_PROPERTY_NAME,
                             expected_pem_cert_chain));

  rpeer = grpc_shallow_peer_from_ssl_auth_context(ctx.get());
  ASSERT_TRUE(check_ssl_peer_equivalence(&peer, &rpeer));

  grpc_shallow_peer_destruct(&rpeer);
  tsi_peer_destruct(&peer);
  ctx.reset(DEBUG_LOCATION, "test");
}

static void test_cn_and_one_san_ssl_peer_to_auth_context(void) {
  tsi_peer peer;
  tsi_peer rpeer;
  const char* expected_cn = "cn1";
  const std::vector<std::string> expected_sans = {"san1"};
  const char* expected_pem_cert = "pem_cert1";
  const char* expected_pem_cert_chain = "pem_cert1_chain";
  ASSERT_EQ(tsi_construct_peer(6, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
                &peer.properties[0]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, expected_cn,
                &peer.properties[1]),
            TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_X509_PEM_CERT_PROPERTY, expected_pem_cert, &peer.properties[2]),
      TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_SECURITY_LEVEL_PEER_PROPERTY,
                tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
                &peer.properties[3]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_PEM_CERT_CHAIN_PROPERTY, expected_pem_cert_chain,
                &peer.properties[4]),
            TSI_OK);
  for (size_t i = 0; i < expected_sans.size(); i++) {
    ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                  TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                  expected_sans[i].c_str(), &peer.properties[5 + i]),
              TSI_OK);
  }
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(ctx, nullptr);
  ASSERT_TRUE(grpc_auth_context_peer_is_authenticated(ctx.get()));
  ASSERT_TRUE(
      check_properties(ctx.get(), GRPC_X509_SAN_PROPERTY_NAME, expected_sans));
  ASSERT_TRUE(check_property(ctx.get(),
                             GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                             GRPC_SSL_TRANSPORT_SECURITY_TYPE));
  ASSERT_TRUE(
      check_property(ctx.get(), GRPC_X509_CN_PROPERTY_NAME, expected_cn));
  ASSERT_TRUE(check_property(ctx.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME,
                             expected_pem_cert));
  ASSERT_TRUE(check_property(ctx.get(), GRPC_X509_PEM_CERT_CHAIN_PROPERTY_NAME,
                             expected_pem_cert_chain));

  rpeer = grpc_shallow_peer_from_ssl_auth_context(ctx.get());
  ASSERT_TRUE(check_ssl_peer_equivalence(&peer, &rpeer));

  grpc_shallow_peer_destruct(&rpeer);
  tsi_peer_destruct(&peer);
  ctx.reset(DEBUG_LOCATION, "test");
}

static void test_cn_and_multiple_sans_ssl_peer_to_auth_context(void) {
  tsi_peer peer;
  tsi_peer rpeer;
  const char* expected_cn = "cn1";
  const std::vector<std::string> expected_sans = {"san1", "san2", "san3"};
  const char* expected_pem_cert = "pem_cert1";
  const char* expected_pem_cert_chain = "pem_cert1_chain";
  size_t i;
  ASSERT_EQ(tsi_construct_peer(5 + expected_sans.size(), &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
                &peer.properties[0]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, expected_cn,
                &peer.properties[1]),
            TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_X509_PEM_CERT_PROPERTY, expected_pem_cert, &peer.properties[2]),
      TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_SECURITY_LEVEL_PEER_PROPERTY,
                tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
                &peer.properties[3]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_PEM_CERT_CHAIN_PROPERTY, expected_pem_cert_chain,
                &peer.properties[4]),
            TSI_OK);
  for (i = 0; i < expected_sans.size(); i++) {
    ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                  TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                  expected_sans[i].c_str(), &peer.properties[5 + i]),
              TSI_OK);
  }
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(ctx, nullptr);
  ASSERT_TRUE(grpc_auth_context_peer_is_authenticated(ctx.get()));
  ASSERT_TRUE(
      check_properties(ctx.get(), GRPC_X509_SAN_PROPERTY_NAME, expected_sans));
  ASSERT_TRUE(check_property(ctx.get(),
                             GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                             GRPC_SSL_TRANSPORT_SECURITY_TYPE));
  ASSERT_TRUE(
      check_property(ctx.get(), GRPC_X509_CN_PROPERTY_NAME, expected_cn));
  ASSERT_TRUE(check_property(ctx.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME,
                             expected_pem_cert));
  ASSERT_TRUE(check_property(ctx.get(), GRPC_X509_PEM_CERT_CHAIN_PROPERTY_NAME,
                             expected_pem_cert_chain));

  rpeer = grpc_shallow_peer_from_ssl_auth_context(ctx.get());
  ASSERT_TRUE(check_ssl_peer_equivalence(&peer, &rpeer));

  grpc_shallow_peer_destruct(&rpeer);
  tsi_peer_destruct(&peer);
  ctx.reset(DEBUG_LOCATION, "test");
}

static void test_cn_and_multiple_sans_and_others_ssl_peer_to_auth_context(
    void) {
  tsi_peer peer;
  tsi_peer rpeer;
  const char* expected_cn = "cn1";
  const char* expected_pem_cert = "pem_cert1";
  const char* expected_pem_cert_chain = "pem_cert1_chain";
  const std::vector<std::string> expected_sans = {"san1", "san2", "san3"};
  size_t i;
  ASSERT_EQ(tsi_construct_peer(7 + expected_sans.size(), &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
                &peer.properties[0]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                "foo", "bar", &peer.properties[1]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, expected_cn,
                &peer.properties[2]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                "chapi", "chapo", &peer.properties[3]),
            TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_X509_PEM_CERT_PROPERTY, expected_pem_cert, &peer.properties[4]),
      TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_SECURITY_LEVEL_PEER_PROPERTY,
                tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
                &peer.properties[5]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_PEM_CERT_CHAIN_PROPERTY, expected_pem_cert_chain,
                &peer.properties[6]),
            TSI_OK);
  for (i = 0; i < expected_sans.size(); i++) {
    ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                  TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                  expected_sans[i].c_str(), &peer.properties[7 + i]),
              TSI_OK);
  }
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(ctx, nullptr);
  ASSERT_TRUE(grpc_auth_context_peer_is_authenticated(ctx.get()));
  ASSERT_TRUE(
      check_properties(ctx.get(), GRPC_X509_SAN_PROPERTY_NAME, expected_sans));
  ASSERT_TRUE(check_property(ctx.get(),
                             GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                             GRPC_SSL_TRANSPORT_SECURITY_TYPE));
  ASSERT_TRUE(
      check_property(ctx.get(), GRPC_X509_CN_PROPERTY_NAME, expected_cn));
  ASSERT_TRUE(check_property(ctx.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME,
                             expected_pem_cert));
  ASSERT_TRUE(check_property(ctx.get(), GRPC_X509_PEM_CERT_CHAIN_PROPERTY_NAME,
                             expected_pem_cert_chain));

  rpeer = grpc_shallow_peer_from_ssl_auth_context(ctx.get());
  ASSERT_TRUE(check_ssl_peer_equivalence(&peer, &rpeer));

  grpc_shallow_peer_destruct(&rpeer);
  tsi_peer_destruct(&peer);
  ctx.reset(DEBUG_LOCATION, "test");
}

static void test_dns_peer_to_auth_context(void) {
  tsi_peer peer;
  const std::vector<std::string> expected_dns = {"dns1", "dns2", "dns3"};
  ASSERT_EQ(tsi_construct_peer(expected_dns.size(), &peer), TSI_OK);
  for (size_t i = 0; i < expected_dns.size(); ++i) {
    ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                  TSI_X509_DNS_PEER_PROPERTY, expected_dns[i].c_str(),
                  &peer.properties[i]),
              TSI_OK);
  }
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(ctx, nullptr);
  ASSERT_TRUE(
      check_properties(ctx.get(), GRPC_PEER_DNS_PROPERTY_NAME, expected_dns));
  tsi_peer_destruct(&peer);
  ctx.reset(DEBUG_LOCATION, "test");
}

static void test_uri_peer_to_auth_context(void) {
  tsi_peer peer;
  const std::vector<std::string> expected_uri = {"uri1", "uri2", "uri3"};
  ASSERT_EQ(tsi_construct_peer(expected_uri.size(), &peer), TSI_OK);
  for (size_t i = 0; i < expected_uri.size(); ++i) {
    ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                  TSI_X509_URI_PEER_PROPERTY, expected_uri[i].c_str(),
                  &peer.properties[i]),
              TSI_OK);
  }
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(ctx, nullptr);
  ASSERT_TRUE(
      check_properties(ctx.get(), GRPC_PEER_URI_PROPERTY_NAME, expected_uri));
  tsi_peer_destruct(&peer);
  ctx.reset(DEBUG_LOCATION, "test");
}

static void test_email_peer_to_auth_context(void) {
  tsi_peer peer;
  const std::vector<std::string> expected_emails = {"email1", "email2"};
  ASSERT_EQ(tsi_construct_peer(expected_emails.size(), &peer), TSI_OK);
  for (size_t i = 0; i < expected_emails.size(); ++i) {
    ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                  TSI_X509_EMAIL_PEER_PROPERTY, expected_emails[i].c_str(),
                  &peer.properties[i]),
              TSI_OK);
  }
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(ctx, nullptr);
  ASSERT_TRUE(check_properties(ctx.get(), GRPC_PEER_EMAIL_PROPERTY_NAME,
                               expected_emails));
  tsi_peer_destruct(&peer);
  ctx.reset(DEBUG_LOCATION, "test");
}

static void test_ip_peer_to_auth_context(void) {
  tsi_peer peer;
  const std::vector<std::string> expected_ips = {"128.128.128.128",
                                                 "255.255.255.255"};
  ASSERT_EQ(tsi_construct_peer(expected_ips.size(), &peer), TSI_OK);
  for (size_t i = 0; i < expected_ips.size(); ++i) {
    ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                  TSI_X509_IP_PEER_PROPERTY, expected_ips[i].c_str(),
                  &peer.properties[i]),
              TSI_OK);
  }
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(ctx, nullptr);
  ASSERT_TRUE(
      check_properties(ctx.get(), GRPC_PEER_IP_PROPERTY_NAME, expected_ips));
  tsi_peer_destruct(&peer);
  ctx.reset(DEBUG_LOCATION, "test");
}

static void test_spiffe_id_peer_to_auth_context(void) {
  // Invalid SPIFFE IDs should not be plumbed.
  std::string long_id(2050, 'x');
  std::string long_domain(256, 'x');
  tsi_peer invalid_peer;
  std::vector<std::string> invalid_spiffe_id = {
      "",
      "spi://",
      "sfiffe://domain/wl",
      "spiffe://domain",
      "spiffe://domain/",
      long_id,
      "spiffe://" + long_domain + "/wl"};
  size_t i;
  ASSERT_EQ(tsi_construct_peer(invalid_spiffe_id.size(), &invalid_peer),
            TSI_OK);
  for (i = 0; i < invalid_spiffe_id.size(); i++) {
    ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                  TSI_X509_URI_PEER_PROPERTY, invalid_spiffe_id[i].c_str(),
                  &invalid_peer.properties[i]),
              TSI_OK);
  }
  grpc_core::RefCountedPtr<grpc_auth_context> invalid_ctx =
      grpc_ssl_peer_to_auth_context(&invalid_peer,
                                    GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(invalid_ctx, nullptr);
  ASSERT_TRUE(check_spiffe_id(invalid_ctx.get(), nullptr, false));
  tsi_peer_destruct(&invalid_peer);
  invalid_ctx.reset(DEBUG_LOCATION, "test");
  // A valid SPIFFE ID should be plumbed.
  tsi_peer valid_peer;
  std::string valid_spiffe_id = "spiffe://foo.bar.com/wl";
  ASSERT_EQ(tsi_construct_peer(1, &valid_peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_URI_PEER_PROPERTY, valid_spiffe_id.c_str(),
                &valid_peer.properties[0]),
            TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> valid_ctx =
      grpc_ssl_peer_to_auth_context(&valid_peer,
                                    GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(valid_ctx, nullptr);
  ASSERT_TRUE(
      check_spiffe_id(valid_ctx.get(), "spiffe://foo.bar.com/wl", true));
  tsi_peer_destruct(&valid_peer);
  valid_ctx.reset(DEBUG_LOCATION, "test");
  // Multiple SPIFFE IDs should not be plumbed.
  tsi_peer multiple_peer;
  std::vector<std::string> multiple_spiffe_id = {
      "spiffe://foo.bar.com/wl", "https://xyz", "spiffe://foo.bar.com/wl2"};
  ASSERT_EQ(tsi_construct_peer(multiple_spiffe_id.size(), &multiple_peer),
            TSI_OK);
  for (i = 0; i < multiple_spiffe_id.size(); i++) {
    ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                  TSI_X509_URI_PEER_PROPERTY, multiple_spiffe_id[i].c_str(),
                  &multiple_peer.properties[i]),
              TSI_OK);
  }
  grpc_core::RefCountedPtr<grpc_auth_context> multiple_ctx =
      grpc_ssl_peer_to_auth_context(&multiple_peer,
                                    GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(multiple_ctx, nullptr);
  ASSERT_TRUE(check_spiffe_id(multiple_ctx.get(), nullptr, false));
  tsi_peer_destruct(&multiple_peer);
  multiple_ctx.reset(DEBUG_LOCATION, "test");
  // A valid SPIFFE certificate should only has one URI SAN field.
  // SPIFFE ID should not be plumbed if there are multiple URIs.
  tsi_peer multiple_uri_peer;
  std::vector<std::string> multiple_uri = {"spiffe://foo.bar.com/wl",
                                           "https://xyz", "ssh://foo.bar.com/"};
  ASSERT_EQ(tsi_construct_peer(multiple_uri.size(), &multiple_uri_peer),
            TSI_OK);
  for (i = 0; i < multiple_spiffe_id.size(); i++) {
    ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                  TSI_X509_URI_PEER_PROPERTY, multiple_uri[i].c_str(),
                  &multiple_uri_peer.properties[i]),
              TSI_OK);
  }
  grpc_core::RefCountedPtr<grpc_auth_context> multiple_uri_ctx =
      grpc_ssl_peer_to_auth_context(&multiple_uri_peer,
                                    GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(multiple_uri_ctx, nullptr);
  ASSERT_TRUE(check_spiffe_id(multiple_uri_ctx.get(), nullptr, false));
  tsi_peer_destruct(&multiple_uri_peer);
  multiple_uri_ctx.reset(DEBUG_LOCATION, "test");
}

static void test_subject_to_auth_context(void) {
  tsi_peer peer;
  const char* expected_subject = "subject1";
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_X509_SUBJECT_PEER_PROPERTY, expected_subject,
                &peer.properties[0]),
            TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  ASSERT_NE(ctx, nullptr);
  ASSERT_TRUE(check_property(ctx.get(), GRPC_X509_SUBJECT_PROPERTY_NAME,
                             expected_subject));
  tsi_peer_destruct(&peer);
  ctx.reset(DEBUG_LOCATION, "test");
}

static const char* roots_for_override_api = "roots for override api";

static grpc_ssl_roots_override_result override_roots_success(
    char** pem_root_certs) {
  *pem_root_certs = gpr_strdup(roots_for_override_api);
  return GRPC_SSL_ROOTS_OVERRIDE_OK;
}

static grpc_ssl_roots_override_result override_roots_permanent_failure(
    char** /*pem_root_certs*/) {
  return GRPC_SSL_ROOTS_OVERRIDE_FAIL_PERMANENTLY;
}

static void test_ipv6_address_san(void) {
  const char* addresses[] = {
      "2001:db8::1",     "fe80::abcd:ef65:4321%em0", "fd11:feed:beef:0:cafe::4",
      "128.10.0.1:8888", "[2001:db8::1]:8080",       "[2001:db8::1%em1]:8080",
  };
  const char* san_ips[] = {
      "2001:db8::1", "fe80::abcd:ef65:4321", "fd11:feed:beef:0:cafe::4",
      "128.10.0.1",  "2001:db8::1",          "2001:db8::1",
  };
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(addresses); i++) {
    ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                  TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, san_ips[i],
                  &peer.properties[0]),
              TSI_OK);
    ASSERT_TRUE(grpc_ssl_host_matches_name(&peer, addresses[i]));
    tsi_peer_property_destruct(&peer.properties[0]);
  }
  tsi_peer_destruct(&peer);
}

namespace grpc_core {
namespace {

class TestDefaultSslRootStore : public DefaultSslRootStore {
 public:
  static grpc_slice ComputePemRootCertsForTesting() {
    return ComputePemRootCerts();
  }
};

}  // namespace
}  // namespace grpc_core

// TODO(unknown): Convert this test to C++ test when security_connector
// implementation is converted to C++.
static void test_default_ssl_roots(void) {
  const char* roots_for_env_var = "roots for env var";

  char* roots_env_var_file_path;
  FILE* roots_env_var_file =
      gpr_tmpfile("test_roots_for_env_var", &roots_env_var_file_path);
  fwrite(roots_for_env_var, 1, strlen(roots_for_env_var), roots_env_var_file);
  fclose(roots_env_var_file);

  grpc_core::ConfigVars::Overrides overrides;

  // First let's get the root through the override: override the config to an
  // invalid value.
  overrides.default_ssl_roots_file_path = "";
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_set_ssl_roots_override_callback(override_roots_success);
  grpc_slice roots =
      grpc_core::TestDefaultSslRootStore::ComputePemRootCertsForTesting();
  char* roots_contents = grpc_slice_to_c_string(roots);
  grpc_slice_unref(roots);
  ASSERT_STREQ(roots_contents, roots_for_override_api);
  gpr_free(roots_contents);

  // Now let's set the config: We should get the contents pointed value
  // instead
  overrides.default_ssl_roots_file_path = roots_env_var_file_path;
  grpc_core::ConfigVars::SetOverrides(overrides);
  roots = grpc_core::TestDefaultSslRootStore::ComputePemRootCertsForTesting();
  roots_contents = grpc_slice_to_c_string(roots);
  grpc_slice_unref(roots);
  ASSERT_STREQ(roots_contents, roots_for_env_var);
  gpr_free(roots_contents);

  // Now reset the config. We should fall back to the value overridden using
  // the api.
  overrides.default_ssl_roots_file_path = "";
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_set_ssl_roots_override_callback(override_roots_success);
  roots = grpc_core::TestDefaultSslRootStore::ComputePemRootCertsForTesting();
  roots_contents = grpc_slice_to_c_string(roots);
  grpc_slice_unref(roots);
  ASSERT_STREQ(roots_contents, roots_for_override_api);
  gpr_free(roots_contents);

  // Now setup a permanent failure for the overridden roots and we should get
  // an empty slice.
  overrides.not_use_system_ssl_roots = true;
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_set_ssl_roots_override_callback(override_roots_permanent_failure);
  roots = grpc_core::TestDefaultSslRootStore::ComputePemRootCertsForTesting();
  ASSERT_TRUE(GRPC_SLICE_IS_EMPTY(roots));
  const tsi_ssl_root_certs_store* root_store =
      grpc_core::TestDefaultSslRootStore::GetRootStore();
  ASSERT_EQ(root_store, nullptr);

  // Cleanup.
  remove(roots_env_var_file_path);
  gpr_free(roots_env_var_file_path);
}

static void test_peer_alpn_check(void) {
#if TSI_OPENSSL_ALPN_SUPPORT
  tsi_peer peer;
  const char* alpn = "h2";
  const char* wrong_alpn = "wrong";
  // peer does not have a TSI_SSL_ALPN_SELECTED_PROTOCOL property.
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property("wrong peer property name", alpn,
                                         strlen(alpn), &peer.properties[0]),
      TSI_OK);
  grpc_error_handle error = grpc_ssl_check_alpn(&peer);
  ASSERT_FALSE(error.ok());
  tsi_peer_destruct(&peer);
  // peer has a TSI_SSL_ALPN_SELECTED_PROTOCOL property but with an incorrect
  // property value.
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL,
                                               wrong_alpn, strlen(wrong_alpn),
                                               &peer.properties[0]),
            TSI_OK);
  error = grpc_ssl_check_alpn(&peer);
  ASSERT_FALSE(error.ok());
  tsi_peer_destruct(&peer);
  // peer has a TSI_SSL_ALPN_SELECTED_PROTOCOL property with a correct property
  // value.
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property(TSI_SSL_ALPN_SELECTED_PROTOCOL, alpn,
                                         strlen(alpn), &peer.properties[0]),
      TSI_OK);
  ASSERT_EQ(grpc_ssl_check_alpn(&peer), absl::OkStatus());
  tsi_peer_destruct(&peer);
#else
  ASSERT_EQ(grpc_ssl_check_alpn(nullptr), absl::OkStatus());
#endif
}

TEST(SecurityConnectorTest, MainTest) {
  grpc_init();
  test_unauthenticated_ssl_peer();
  test_cn_only_ssl_peer_to_auth_context();
  test_cn_and_one_san_ssl_peer_to_auth_context();
  test_cn_and_multiple_sans_ssl_peer_to_auth_context();
  test_cn_and_multiple_sans_and_others_ssl_peer_to_auth_context();
  test_dns_peer_to_auth_context();
  test_uri_peer_to_auth_context();
  test_email_peer_to_auth_context();
  test_ip_peer_to_auth_context();
  test_spiffe_id_peer_to_auth_context();
  test_subject_to_auth_context();
  test_ipv6_address_san();
  test_default_ssl_roots();
  test_peer_alpn_check();
  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
