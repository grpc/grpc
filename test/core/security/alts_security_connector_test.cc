/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/security/security_connector/alts_security_connector.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/transport_security.h"

using grpc_core::internal::grpc_alts_auth_context_from_tsi_peer;

/* This file contains unit tests of grpc_alts_auth_context_from_tsi_peer(). */
static void test_invalid_input_failure() {
  tsi_peer peer;
  grpc_auth_context* ctx;
  GPR_ASSERT(grpc_alts_auth_context_from_tsi_peer(nullptr, &ctx) ==
             GRPC_SECURITY_ERROR);
  GPR_ASSERT(grpc_alts_auth_context_from_tsi_peer(&peer, nullptr) ==
             GRPC_SECURITY_ERROR);
}

static void test_empty_certificate_type_failure() {
  tsi_peer peer;
  grpc_auth_context* ctx = nullptr;
  GPR_ASSERT(tsi_construct_peer(0, &peer) == TSI_OK);
  GPR_ASSERT(grpc_alts_auth_context_from_tsi_peer(&peer, &ctx) ==
             GRPC_SECURITY_ERROR);
  GPR_ASSERT(ctx == nullptr);
  tsi_peer_destruct(&peer);
}

static void test_empty_peer_property_failure() {
  tsi_peer peer;
  grpc_auth_context* ctx;
  GPR_ASSERT(tsi_construct_peer(1, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
                 &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(grpc_alts_auth_context_from_tsi_peer(&peer, &ctx) ==
             GRPC_SECURITY_ERROR);
  GPR_ASSERT(ctx == nullptr);
  tsi_peer_destruct(&peer);
}

static void test_missing_rpc_protocol_versions_property_failure() {
  tsi_peer peer;
  grpc_auth_context* ctx;
  GPR_ASSERT(tsi_construct_peer(kTsiAltsNumOfPeerProperties, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
                 &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, "alice",
                 &peer.properties[1]) == TSI_OK);
  GPR_ASSERT(grpc_alts_auth_context_from_tsi_peer(&peer, &ctx) ==
             GRPC_SECURITY_ERROR);
  GPR_ASSERT(ctx == nullptr);
  tsi_peer_destruct(&peer);
}

static void test_unknown_peer_property_failure() {
  tsi_peer peer;
  grpc_auth_context* ctx;
  GPR_ASSERT(tsi_construct_peer(kTsiAltsNumOfPeerProperties, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
                 &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 "unknown", "alice", &peer.properties[1]) == TSI_OK);
  GPR_ASSERT(grpc_alts_auth_context_from_tsi_peer(&peer, &ctx) ==
             GRPC_SECURITY_ERROR);
  GPR_ASSERT(ctx == nullptr);
  tsi_peer_destruct(&peer);
}

static bool test_identity(const grpc_auth_context* ctx,
                          const char* expected_property_name,
                          const char* expected_identity) {
  grpc_auth_property_iterator it;
  const grpc_auth_property* prop;
  GPR_ASSERT(grpc_auth_context_peer_is_authenticated(ctx));
  it = grpc_auth_context_peer_identity(ctx);
  prop = grpc_auth_property_iterator_next(&it);
  GPR_ASSERT(prop != nullptr);
  if (strcmp(prop->name, expected_property_name) != 0) {
    gpr_log(GPR_ERROR, "Expected peer identity property name %s and got %s.",
            expected_property_name, prop->name);
    return false;
  }
  if (strncmp(prop->value, expected_identity, prop->value_length) != 0) {
    gpr_log(GPR_ERROR, "Expected peer identity %s and got got %s.",
            expected_identity, prop->value);
    return false;
  }
  return true;
}

static void test_alts_peer_to_auth_context_success() {
  tsi_peer peer;
  grpc_auth_context* ctx;
  GPR_ASSERT(tsi_construct_peer(kTsiAltsNumOfPeerProperties, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
                 &peer.properties[0]) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, "alice",
                 &peer.properties[1]) == TSI_OK);
  grpc_gcp_rpc_protocol_versions peer_versions;
  grpc_gcp_rpc_protocol_versions_set_max(&peer_versions,
                                         GRPC_PROTOCOL_VERSION_MAX_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MAX_MINOR);
  grpc_gcp_rpc_protocol_versions_set_min(&peer_versions,
                                         GRPC_PROTOCOL_VERSION_MIN_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MIN_MINOR);
  grpc_slice serialized_peer_versions;
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_encode(&peer_versions,
                                                   &serialized_peer_versions));

  GPR_ASSERT(tsi_construct_string_peer_property(
                 TSI_ALTS_RPC_VERSIONS,
                 reinterpret_cast<char*>(
                     GRPC_SLICE_START_PTR(serialized_peer_versions)),
                 GRPC_SLICE_LENGTH(serialized_peer_versions),
                 &peer.properties[2]) == TSI_OK);
  GPR_ASSERT(grpc_alts_auth_context_from_tsi_peer(&peer, &ctx) ==
             GRPC_SECURITY_OK);
  GPR_ASSERT(
      test_identity(ctx, TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, "alice"));
  GRPC_AUTH_CONTEXT_UNREF(ctx, "test");
  grpc_slice_unref(serialized_peer_versions);
  tsi_peer_destruct(&peer);
}

int main(int argc, char** argv) {
  /* Test. */
  test_invalid_input_failure();
  test_empty_certificate_type_failure();
  test_empty_peer_property_failure();
  test_unknown_peer_property_failure();
  test_missing_rpc_protocol_versions_property_failure();
  test_alts_peer_to_auth_context_success();

  return 0;
}
