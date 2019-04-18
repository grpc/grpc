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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "test/core/tsi/alts/handshaker/alts_handshaker_service_api_test_lib.h"

int main(int argc, char** argv) {
  const char in_bytes[] = "HELLO GOOGLE!";
  const char out_frames[] = "HELLO WORLD!";
  const char key_data[] = "THIS IS KEY DATA.";
  const char details[] = "DETAILS NEED TO BE POPULATED";
  const uint32_t max_rpc_version_major = 3;
  const uint32_t max_rpc_version_minor = 2;
  const uint32_t min_rpc_version_major = 2;
  const uint32_t min_rpc_version_minor = 1;

  /* handshaker_req_next. */
  grpc_gcp_handshaker_req* req = grpc_gcp_handshaker_req_create(NEXT_REQ);
  grpc_gcp_handshaker_req* decoded_req =
      grpc_gcp_handshaker_decoded_req_create(NEXT_REQ);
  GPR_ASSERT(
      grpc_gcp_handshaker_req_set_in_bytes(req, in_bytes, strlen(in_bytes)));
  grpc_slice encoded_req;
  GPR_ASSERT(grpc_gcp_handshaker_req_encode(req, &encoded_req));
  GPR_ASSERT(grpc_gcp_handshaker_req_decode(encoded_req, decoded_req));
  GPR_ASSERT(grpc_gcp_handshaker_req_equals(req, decoded_req));
  grpc_gcp_handshaker_req_destroy(req);
  grpc_gcp_handshaker_req_destroy(decoded_req);
  grpc_slice_unref(encoded_req);

  /* handshaker_req_client_start. */
  req = grpc_gcp_handshaker_req_create(CLIENT_START_REQ);
  decoded_req = grpc_gcp_handshaker_decoded_req_create(CLIENT_START_REQ);
  GPR_ASSERT(grpc_gcp_handshaker_req_set_handshake_protocol(
      req, grpc_gcp_HandshakeProtocol_TLS));
  GPR_ASSERT(grpc_gcp_handshaker_req_set_local_identity_hostname(
      req, "www.google.com"));
  GPR_ASSERT(grpc_gcp_handshaker_req_set_local_endpoint(
      req, "2001:db8::8:800:200C:417a", 9876, grpc_gcp_NetworkProtocol_TCP));
  GPR_ASSERT(grpc_gcp_handshaker_req_set_remote_endpoint(
      req, "2001:db8::bac5::fed0:84a2", 1234, grpc_gcp_NetworkProtocol_TCP));
  GPR_ASSERT(grpc_gcp_handshaker_req_add_application_protocol(req, "grpc"));
  GPR_ASSERT(grpc_gcp_handshaker_req_add_application_protocol(req, "http2"));
  GPR_ASSERT(
      grpc_gcp_handshaker_req_add_record_protocol(req, "ALTSRP_GCM_AES256"));
  GPR_ASSERT(
      grpc_gcp_handshaker_req_add_record_protocol(req, "ALTSRP_GCM_AES384"));
  GPR_ASSERT(grpc_gcp_handshaker_req_add_target_identity_service_account(
      req, "foo@google.com"));
  GPR_ASSERT(grpc_gcp_handshaker_req_set_target_name(
      req, "google.example.library.service"));
  GPR_ASSERT(grpc_gcp_handshaker_req_set_rpc_versions(
      req, max_rpc_version_major, max_rpc_version_minor, min_rpc_version_major,
      min_rpc_version_minor));
  GPR_ASSERT(grpc_gcp_handshaker_req_encode(req, &encoded_req));
  GPR_ASSERT(grpc_gcp_handshaker_req_decode(encoded_req, decoded_req));
  GPR_ASSERT(grpc_gcp_handshaker_req_equals(req, decoded_req));
  grpc_gcp_handshaker_req_destroy(req);
  grpc_gcp_handshaker_req_destroy(decoded_req);
  grpc_slice_unref(encoded_req);

  /* handshaker_req_server_start. */
  req = grpc_gcp_handshaker_req_create(SERVER_START_REQ);
  decoded_req = grpc_gcp_handshaker_decoded_req_create(SERVER_START_REQ);
  GPR_ASSERT(grpc_gcp_handshaker_req_add_application_protocol(req, "grpc"));
  GPR_ASSERT(grpc_gcp_handshaker_req_add_application_protocol(req, "http2"));
  GPR_ASSERT(grpc_gcp_handshaker_req_set_local_endpoint(
      req, "2001:db8::8:800:200C:417a", 9876, grpc_gcp_NetworkProtocol_TCP));
  GPR_ASSERT(grpc_gcp_handshaker_req_set_remote_endpoint(
      req, "2001:db8::bac5::fed0:84a2", 1234, grpc_gcp_NetworkProtocol_UDP));
  GPR_ASSERT(
      grpc_gcp_handshaker_req_set_in_bytes(req, in_bytes, strlen(in_bytes)));
  GPR_ASSERT(grpc_gcp_handshaker_req_param_add_record_protocol(
      req, grpc_gcp_HandshakeProtocol_TLS, "ALTSRP_GCM_AES128"));
  GPR_ASSERT(grpc_gcp_handshaker_req_param_add_local_identity_service_account(
      req, grpc_gcp_HandshakeProtocol_TLS, "foo@google.com"));
  GPR_ASSERT(grpc_gcp_handshaker_req_param_add_local_identity_hostname(
      req, grpc_gcp_HandshakeProtocol_TLS, "yihuaz0.mtv.corp.google.com"));
  GPR_ASSERT(grpc_gcp_handshaker_req_param_add_record_protocol(
      req, grpc_gcp_HandshakeProtocol_ALTS, "ALTSRP_GCM_AES128"));
  GPR_ASSERT(grpc_gcp_handshaker_req_param_add_local_identity_hostname(
      req, grpc_gcp_HandshakeProtocol_ALTS, "www.amazon.com"));
  GPR_ASSERT(grpc_gcp_handshaker_req_set_rpc_versions(
      req, max_rpc_version_major, max_rpc_version_minor, min_rpc_version_major,
      min_rpc_version_minor));

  GPR_ASSERT(grpc_gcp_handshaker_req_encode(req, &encoded_req));
  GPR_ASSERT(grpc_gcp_handshaker_req_decode(encoded_req, decoded_req));
  GPR_ASSERT(grpc_gcp_handshaker_req_equals(req, decoded_req));
  grpc_gcp_handshaker_req_destroy(req);
  grpc_gcp_handshaker_req_destroy(decoded_req);
  grpc_slice_unref(encoded_req);

  /* handshaker_resp. */
  grpc_gcp_handshaker_resp* resp = grpc_gcp_handshaker_resp_create();
  grpc_gcp_handshaker_resp* decoded_resp = grpc_gcp_handshaker_resp_create();
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_out_frames(resp, out_frames,
                                                     strlen(out_frames)));
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_bytes_consumed(resp, 1024));
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_application_protocol(resp, "http"));
  GPR_ASSERT(
      grpc_gcp_handshaker_resp_set_record_protocol(resp, "ALTSRP_GCM_AES128"));
  GPR_ASSERT(
      grpc_gcp_handshaker_resp_set_key_data(resp, key_data, strlen(key_data)));
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_local_identity_hostname(
      resp, "www.faceboook.com"));
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_peer_identity_hostname(
      resp, "www.amazon.com"));
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_channel_open(
      resp, false /* channel_open */));
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_code(resp, 1023));
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_details(resp, details));
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_peer_rpc_versions(
      resp, max_rpc_version_major, max_rpc_version_minor, min_rpc_version_major,
      min_rpc_version_minor));
  grpc_slice encoded_resp;
  GPR_ASSERT(grpc_gcp_handshaker_resp_encode(resp, &encoded_resp));
  GPR_ASSERT(grpc_gcp_handshaker_resp_decode(encoded_resp, decoded_resp));
  GPR_ASSERT(grpc_gcp_handshaker_resp_equals(resp, decoded_resp));
  grpc_gcp_handshaker_resp_destroy(resp);
  grpc_gcp_handshaker_resp_destroy(decoded_resp);
  grpc_slice_unref(encoded_resp);
  /* Test invalid arguments. */
  GPR_ASSERT(!grpc_gcp_handshaker_req_set_in_bytes(nullptr, in_bytes,
                                                   strlen(in_bytes)));
  GPR_ASSERT(!grpc_gcp_handshaker_req_param_add_record_protocol(
      req, grpc_gcp_HandshakeProtocol_TLS, nullptr));
  GPR_ASSERT(!grpc_gcp_handshaker_req_param_add_local_identity_service_account(
      nullptr, grpc_gcp_HandshakeProtocol_TLS, nullptr));
  GPR_ASSERT(!grpc_gcp_handshaker_resp_set_record_protocol(nullptr, nullptr));
}
