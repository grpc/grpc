/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <string.h>

#include "src/core/security/security_connector.h"
#include "src/core/security/security_context.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "test/core/util/test_config.h"

#include <grpc/grpc_security.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

static int check_transport_security_type(const grpc_auth_context *ctx) {
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      ctx, GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME);
  const grpc_auth_property *prop = grpc_auth_property_iterator_next(&it);
  if (prop == NULL) return 0;
  if (strncmp(prop->value, GRPC_SSL_TRANSPORT_SECURITY_TYPE,
              prop->value_length) != 0) {
    return 0;
  }
  /* Check that we have only one property with this name. */
  if (grpc_auth_property_iterator_next(&it) != NULL) return 0;
  return 1;
}

static void test_unauthenticated_ssl_peer(void) {
  tsi_peer peer;
  grpc_auth_context *ctx;
  GPR_ASSERT(tsi_construct_peer(1, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
                 &peer.properties[0]) == TSI_OK);
  ctx = tsi_ssl_peer_to_auth_context(&peer);
  GPR_ASSERT(ctx != NULL);
  GPR_ASSERT(!grpc_auth_context_peer_is_authenticated(ctx));
  GPR_ASSERT(check_transport_security_type(ctx));

  tsi_peer_destruct(&peer);
  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
}

static int check_identity(const grpc_auth_context *ctx,
                          const char *expected_property_name,
                          const char **expected_identities,
                          size_t num_identities) {
  grpc_auth_property_iterator it;
  const grpc_auth_property *prop;
  size_t i;
  GPR_ASSERT(grpc_auth_context_peer_is_authenticated(ctx));
  it = grpc_auth_context_peer_identity(ctx);
  for (i = 0; i < num_identities; i++) {
    prop = grpc_auth_property_iterator_next(&it);
    if (prop == NULL) {
      gpr_log(GPR_ERROR, "Expected identity value %s not found.",
              expected_identities[i]);
      return 0;
    }
    if (strcmp(prop->name, expected_property_name) != 0) {
      gpr_log(GPR_ERROR, "Expected peer identity property name %s and got %s.",
              expected_property_name, prop->name);
      return 0;
    }
    if (strncmp(prop->value, expected_identities[i], prop->value_length) != 0) {
      gpr_log(GPR_ERROR, "Expected peer identity %s and got %s.",
              expected_identities[i], prop->value);
      return 0;
    }
  }
  return 1;
}

static int check_x509_cn(const grpc_auth_context *ctx,
                         const char *expected_cn) {
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      ctx, GRPC_X509_CN_PROPERTY_NAME);
  const grpc_auth_property *prop = grpc_auth_property_iterator_next(&it);
  if (prop == NULL) {
    gpr_log(GPR_ERROR, "CN property not found.");
    return 0;
  }
  if (strncmp(prop->value, expected_cn, prop->value_length) != 0) {
    gpr_log(GPR_ERROR, "Expected CN %s and got %s", expected_cn, prop->value);
    return 0;
  }
  if (grpc_auth_property_iterator_next(&it) != NULL) {
    gpr_log(GPR_ERROR, "Expected only one property for CN.");
    return 0;
  }
  return 1;
}

static void test_cn_only_ssl_peer_to_auth_context(void) {
  tsi_peer peer;
  grpc_auth_context *ctx;
  const char *expected_cn = "cn1";
  GPR_ASSERT(tsi_construct_peer(2, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
                 &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, expected_cn,
                 &peer.properties[1]) == TSI_OK);
  ctx = tsi_ssl_peer_to_auth_context(&peer);
  GPR_ASSERT(ctx != NULL);
  GPR_ASSERT(grpc_auth_context_peer_is_authenticated(ctx));
  GPR_ASSERT(check_identity(ctx, GRPC_X509_CN_PROPERTY_NAME, &expected_cn, 1));
  GPR_ASSERT(check_transport_security_type(ctx));
  GPR_ASSERT(check_x509_cn(ctx, expected_cn));

  tsi_peer_destruct(&peer);
  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
}

static void test_cn_and_one_san_ssl_peer_to_auth_context(void) {
  tsi_peer peer;
  grpc_auth_context *ctx;
  const char *expected_cn = "cn1";
  const char *expected_san = "san1";
  GPR_ASSERT(tsi_construct_peer(3, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
                 &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, expected_cn,
                 &peer.properties[1]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, expected_san,
                 &peer.properties[2]) == TSI_OK);
  ctx = tsi_ssl_peer_to_auth_context(&peer);
  GPR_ASSERT(ctx != NULL);
  GPR_ASSERT(grpc_auth_context_peer_is_authenticated(ctx));
  GPR_ASSERT(
      check_identity(ctx, GRPC_X509_SAN_PROPERTY_NAME, &expected_san, 1));
  GPR_ASSERT(check_transport_security_type(ctx));
  GPR_ASSERT(check_x509_cn(ctx, expected_cn));

  tsi_peer_destruct(&peer);
  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
}

static void test_cn_and_multiple_sans_ssl_peer_to_auth_context(void) {
  tsi_peer peer;
  grpc_auth_context *ctx;
  const char *expected_cn = "cn1";
  const char *expected_sans[] = {"san1", "san2", "san3"};
  size_t i;
  GPR_ASSERT(tsi_construct_peer(2 + GPR_ARRAY_SIZE(expected_sans), &peer) ==
             TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
                 &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, expected_cn,
                 &peer.properties[1]) == TSI_OK);
  for (i = 0; i < GPR_ARRAY_SIZE(expected_sans); i++) {
    GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                   TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                   expected_sans[i], &peer.properties[2 + i]) == TSI_OK);
  }
  ctx = tsi_ssl_peer_to_auth_context(&peer);
  GPR_ASSERT(ctx != NULL);
  GPR_ASSERT(grpc_auth_context_peer_is_authenticated(ctx));
  GPR_ASSERT(check_identity(ctx, GRPC_X509_SAN_PROPERTY_NAME, expected_sans,
                            GPR_ARRAY_SIZE(expected_sans)));
  GPR_ASSERT(check_transport_security_type(ctx));
  GPR_ASSERT(check_x509_cn(ctx, expected_cn));

  tsi_peer_destruct(&peer);
  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
}

static void test_cn_and_multiple_sans_and_others_ssl_peer_to_auth_context(
    void) {
  tsi_peer peer;
  grpc_auth_context *ctx;
  const char *expected_cn = "cn1";
  const char *expected_sans[] = {"san1", "san2", "san3"};
  size_t i;
  GPR_ASSERT(tsi_construct_peer(4 + GPR_ARRAY_SIZE(expected_sans), &peer) ==
             TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
                 &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 "foo", "bar", &peer.properties[1]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, expected_cn,
                 &peer.properties[2]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 "chapi", "chapo", &peer.properties[3]) == TSI_OK);
  for (i = 0; i < GPR_ARRAY_SIZE(expected_sans); i++) {
    GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                   TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
                   expected_sans[i], &peer.properties[4 + i]) == TSI_OK);
  }
  ctx = tsi_ssl_peer_to_auth_context(&peer);
  GPR_ASSERT(ctx != NULL);
  GPR_ASSERT(grpc_auth_context_peer_is_authenticated(ctx));
  GPR_ASSERT(check_identity(ctx, GRPC_X509_SAN_PROPERTY_NAME, expected_sans,
                            GPR_ARRAY_SIZE(expected_sans)));
  GPR_ASSERT(check_transport_security_type(ctx));
  GPR_ASSERT(check_x509_cn(ctx, expected_cn));

  tsi_peer_destruct(&peer);
  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  test_unauthenticated_ssl_peer();
  test_cn_only_ssl_peer_to_auth_context();
  test_cn_and_one_san_ssl_peer_to_auth_context();
  test_cn_and_multiple_sans_ssl_peer_to_auth_context();
  test_cn_and_multiple_sans_and_others_ssl_peer_to_auth_context();

  grpc_shutdown();
  return 0;
}
