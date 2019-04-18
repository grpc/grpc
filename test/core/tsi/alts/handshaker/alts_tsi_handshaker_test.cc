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

#include <grpc/grpc.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/thd.h"
#include "src/core/tsi/alts/handshaker/alts_handshaker_client.h"
#include "src/core/tsi/alts/handshaker/alts_shared_resource.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker_private.h"
#include "test/core/tsi/alts/handshaker/alts_handshaker_service_api_test_lib.h"

#define ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES "Hello World"
#define ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME "Hello Google"
#define ALTS_TSI_HANDSHAKER_TEST_CONSUMED_BYTES "Hello "
#define ALTS_TSI_HANDSHAKER_TEST_REMAIN_BYTES "Google"
#define ALTS_TSI_HANDSHAKER_TEST_PEER_IDENTITY "chapi@service.google.com"
#define ALTS_TSI_HANDSHAKER_TEST_KEY_DATA \
  "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKL"
#define ALTS_TSI_HANDSHAKER_TEST_BUFFER_SIZE 100
#define ALTS_TSI_HANDSHAKER_TEST_SLEEP_TIME_IN_SECONDS 2
#define ALTS_TSI_HANDSHAKER_TEST_MAX_RPC_VERSION_MAJOR 3
#define ALTS_TSI_HANDSHAKER_TEST_MAX_RPC_VERSION_MINOR 2
#define ALTS_TSI_HANDSHAKER_TEST_MIN_RPC_VERSION_MAJOR 2
#define ALTS_TSI_HANDSHAKER_TEST_MIN_RPC_VERSION_MINOR 1

using grpc_core::internal::alts_handshaker_client_check_fields_for_testing;
using grpc_core::internal::alts_handshaker_client_get_handshaker_for_testing;
using grpc_core::internal::
    alts_handshaker_client_get_recv_buffer_addr_for_testing;
using grpc_core::internal::alts_handshaker_client_set_cb_for_testing;
using grpc_core::internal::alts_handshaker_client_set_fields_for_testing;
using grpc_core::internal::alts_handshaker_client_set_recv_bytes_for_testing;
using grpc_core::internal::alts_handshaker_client_set_vtable_for_testing;
using grpc_core::internal::alts_tsi_handshaker_get_client_for_testing;
using grpc_core::internal::alts_tsi_handshaker_get_is_client_for_testing;
using grpc_core::internal::alts_tsi_handshaker_set_client_vtable_for_testing;
static bool should_handshaker_client_api_succeed = true;

/* ALTS mock notification. */
typedef struct notification {
  gpr_cv cv;
  gpr_mu mu;
  bool notified;
} notification;

/* Type of ALTS handshaker response. */
typedef enum {
  INVALID,
  FAILED,
  CLIENT_START,
  SERVER_START,
  CLIENT_NEXT,
  SERVER_NEXT,
} alts_handshaker_response_type;

static alts_handshaker_client* cb_event = nullptr;
static notification caller_to_tsi_notification;
static notification tsi_to_caller_notification;

static void notification_init(notification* n) {
  gpr_mu_init(&n->mu);
  gpr_cv_init(&n->cv);
  n->notified = false;
}

static void notification_destroy(notification* n) {
  gpr_mu_destroy(&n->mu);
  gpr_cv_destroy(&n->cv);
}

static void signal(notification* n) {
  gpr_mu_lock(&n->mu);
  n->notified = true;
  gpr_cv_signal(&n->cv);
  gpr_mu_unlock(&n->mu);
}

static void wait(notification* n) {
  gpr_mu_lock(&n->mu);
  while (!n->notified) {
    gpr_cv_wait(&n->cv, &n->mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  n->notified = false;
  gpr_mu_unlock(&n->mu);
}

/**
 * This method mocks ALTS handshaker service to generate handshaker response
 * for a specific request.
 */
static grpc_byte_buffer* generate_handshaker_response(
    alts_handshaker_response_type type) {
  grpc_gcp_handshaker_resp* resp = grpc_gcp_handshaker_resp_create();
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_code(resp, 0));
  switch (type) {
    case INVALID:
      break;
    case CLIENT_START:
    case SERVER_START:
      GPR_ASSERT(grpc_gcp_handshaker_resp_set_out_frames(
          resp, ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME,
          strlen(ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME)));
      break;
    case CLIENT_NEXT:
      GPR_ASSERT(grpc_gcp_handshaker_resp_set_out_frames(
          resp, ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME,
          strlen(ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME)));
      GPR_ASSERT(grpc_gcp_handshaker_resp_set_peer_identity_service_account(
          resp, ALTS_TSI_HANDSHAKER_TEST_PEER_IDENTITY));
      GPR_ASSERT(grpc_gcp_handshaker_resp_set_bytes_consumed(
          resp, strlen(ALTS_TSI_HANDSHAKER_TEST_CONSUMED_BYTES)));
      GPR_ASSERT(grpc_gcp_handshaker_resp_set_key_data(
          resp, ALTS_TSI_HANDSHAKER_TEST_KEY_DATA,
          strlen(ALTS_TSI_HANDSHAKER_TEST_KEY_DATA)));
      GPR_ASSERT(grpc_gcp_handshaker_resp_set_peer_rpc_versions(
          resp, ALTS_TSI_HANDSHAKER_TEST_MAX_RPC_VERSION_MAJOR,
          ALTS_TSI_HANDSHAKER_TEST_MAX_RPC_VERSION_MINOR,
          ALTS_TSI_HANDSHAKER_TEST_MIN_RPC_VERSION_MAJOR,
          ALTS_TSI_HANDSHAKER_TEST_MIN_RPC_VERSION_MINOR));
      break;
    case SERVER_NEXT:
      GPR_ASSERT(grpc_gcp_handshaker_resp_set_peer_identity_service_account(
          resp, ALTS_TSI_HANDSHAKER_TEST_PEER_IDENTITY));
      GPR_ASSERT(grpc_gcp_handshaker_resp_set_bytes_consumed(
          resp, strlen(ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME)));
      GPR_ASSERT(grpc_gcp_handshaker_resp_set_key_data(
          resp, ALTS_TSI_HANDSHAKER_TEST_KEY_DATA,
          strlen(ALTS_TSI_HANDSHAKER_TEST_KEY_DATA)));
      GPR_ASSERT(grpc_gcp_handshaker_resp_set_peer_rpc_versions(
          resp, ALTS_TSI_HANDSHAKER_TEST_MAX_RPC_VERSION_MAJOR,
          ALTS_TSI_HANDSHAKER_TEST_MAX_RPC_VERSION_MINOR,
          ALTS_TSI_HANDSHAKER_TEST_MIN_RPC_VERSION_MAJOR,
          ALTS_TSI_HANDSHAKER_TEST_MIN_RPC_VERSION_MINOR));
      break;
    case FAILED:
      GPR_ASSERT(
          grpc_gcp_handshaker_resp_set_code(resp, 3 /* INVALID ARGUMENT */));
      break;
  }
  grpc_slice slice;
  GPR_ASSERT(grpc_gcp_handshaker_resp_encode(resp, &slice));
  if (type == INVALID) {
    grpc_slice bad_slice =
        grpc_slice_split_head(&slice, GRPC_SLICE_LENGTH(slice) - 1);
    grpc_slice_unref(slice);
    slice = grpc_slice_ref(bad_slice);
    grpc_slice_unref(bad_slice);
  }
  grpc_byte_buffer* buffer =
      grpc_raw_byte_buffer_create(&slice, 1 /* number of slices */);
  grpc_slice_unref(slice);
  grpc_gcp_handshaker_resp_destroy(resp);
  return buffer;
}

static void check_must_not_be_called(tsi_result status, void* user_data,
                                     const unsigned char* bytes_to_send,
                                     size_t bytes_to_send_size,
                                     tsi_handshaker_result* result) {
  GPR_ASSERT(0);
}

static void on_client_start_success_cb(tsi_result status, void* user_data,
                                       const unsigned char* bytes_to_send,
                                       size_t bytes_to_send_size,
                                       tsi_handshaker_result* result) {
  GPR_ASSERT(status == TSI_OK);
  GPR_ASSERT(user_data == nullptr);
  GPR_ASSERT(bytes_to_send_size == strlen(ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME));
  GPR_ASSERT(memcmp(bytes_to_send, ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME,
                    bytes_to_send_size) == 0);
  GPR_ASSERT(result == nullptr);
  /* Validate peer identity. */
  tsi_peer peer;
  GPR_ASSERT(tsi_handshaker_result_extract_peer(result, &peer) ==
             TSI_INVALID_ARGUMENT);
  /* Validate frame protector. */
  tsi_frame_protector* protector = nullptr;
  GPR_ASSERT(tsi_handshaker_result_create_frame_protector(
                 result, nullptr, &protector) == TSI_INVALID_ARGUMENT);
  /* Validate unused bytes. */
  const unsigned char* unused_bytes = nullptr;
  size_t unused_bytes_size = 0;
  GPR_ASSERT(tsi_handshaker_result_get_unused_bytes(result, &unused_bytes,
                                                    &unused_bytes_size) ==
             TSI_INVALID_ARGUMENT);
  signal(&tsi_to_caller_notification);
}

static void on_server_start_success_cb(tsi_result status, void* user_data,
                                       const unsigned char* bytes_to_send,
                                       size_t bytes_to_send_size,
                                       tsi_handshaker_result* result) {
  GPR_ASSERT(status == TSI_OK);
  GPR_ASSERT(user_data == nullptr);
  GPR_ASSERT(bytes_to_send_size == strlen(ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME));
  GPR_ASSERT(memcmp(bytes_to_send, ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME,
                    bytes_to_send_size) == 0);
  GPR_ASSERT(result == nullptr);
  /* Validate peer identity. */
  tsi_peer peer;
  GPR_ASSERT(tsi_handshaker_result_extract_peer(result, &peer) ==
             TSI_INVALID_ARGUMENT);
  /* Validate frame protector. */
  tsi_frame_protector* protector = nullptr;
  GPR_ASSERT(tsi_handshaker_result_create_frame_protector(
                 result, nullptr, &protector) == TSI_INVALID_ARGUMENT);
  /* Validate unused bytes. */
  const unsigned char* unused_bytes = nullptr;
  size_t unused_bytes_size = 0;
  GPR_ASSERT(tsi_handshaker_result_get_unused_bytes(result, &unused_bytes,
                                                    &unused_bytes_size) ==
             TSI_INVALID_ARGUMENT);
  signal(&tsi_to_caller_notification);
}

static void on_client_next_success_cb(tsi_result status, void* user_data,
                                      const unsigned char* bytes_to_send,
                                      size_t bytes_to_send_size,
                                      tsi_handshaker_result* result) {
  GPR_ASSERT(status == TSI_OK);
  GPR_ASSERT(user_data == nullptr);
  GPR_ASSERT(bytes_to_send_size == strlen(ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME));
  GPR_ASSERT(memcmp(bytes_to_send, ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME,
                    bytes_to_send_size) == 0);
  GPR_ASSERT(result != nullptr);
  /* Validate peer identity. */
  tsi_peer peer;
  GPR_ASSERT(tsi_handshaker_result_extract_peer(result, &peer) == TSI_OK);
  GPR_ASSERT(peer.property_count == kTsiAltsNumOfPeerProperties);
  GPR_ASSERT(memcmp(TSI_ALTS_CERTIFICATE_TYPE, peer.properties[0].value.data,
                    peer.properties[0].value.length) == 0);
  GPR_ASSERT(memcmp(ALTS_TSI_HANDSHAKER_TEST_PEER_IDENTITY,
                    peer.properties[1].value.data,
                    peer.properties[1].value.length) == 0);
  tsi_peer_destruct(&peer);
  /* Validate unused bytes. */
  const unsigned char* bytes = nullptr;
  size_t bytes_size = 0;
  GPR_ASSERT(tsi_handshaker_result_get_unused_bytes(result, &bytes,
                                                    &bytes_size) == TSI_OK);
  GPR_ASSERT(bytes_size == strlen(ALTS_TSI_HANDSHAKER_TEST_REMAIN_BYTES));
  GPR_ASSERT(memcmp(bytes, ALTS_TSI_HANDSHAKER_TEST_REMAIN_BYTES, bytes_size) ==
             0);
  /* Validate frame protector. */
  tsi_frame_protector* protector = nullptr;
  GPR_ASSERT(tsi_handshaker_result_create_frame_protector(
                 result, nullptr, &protector) == TSI_OK);
  GPR_ASSERT(protector != nullptr);
  tsi_frame_protector_destroy(protector);
  tsi_handshaker_result_destroy(result);
  signal(&tsi_to_caller_notification);
}

static void on_server_next_success_cb(tsi_result status, void* user_data,
                                      const unsigned char* bytes_to_send,
                                      size_t bytes_to_send_size,
                                      tsi_handshaker_result* result) {
  GPR_ASSERT(status == TSI_OK);
  GPR_ASSERT(user_data == nullptr);
  GPR_ASSERT(bytes_to_send_size == 0);
  GPR_ASSERT(bytes_to_send == nullptr);
  GPR_ASSERT(result != nullptr);
  /* Validate peer identity. */
  tsi_peer peer;
  GPR_ASSERT(tsi_handshaker_result_extract_peer(result, &peer) == TSI_OK);
  GPR_ASSERT(peer.property_count == kTsiAltsNumOfPeerProperties);
  GPR_ASSERT(memcmp(TSI_ALTS_CERTIFICATE_TYPE, peer.properties[0].value.data,
                    peer.properties[0].value.length) == 0);
  GPR_ASSERT(memcmp(ALTS_TSI_HANDSHAKER_TEST_PEER_IDENTITY,
                    peer.properties[1].value.data,
                    peer.properties[1].value.length) == 0);
  tsi_peer_destruct(&peer);
  /* Validate unused bytes. */
  const unsigned char* bytes = nullptr;
  size_t bytes_size = 0;
  GPR_ASSERT(tsi_handshaker_result_get_unused_bytes(result, &bytes,
                                                    &bytes_size) == TSI_OK);
  GPR_ASSERT(bytes_size == 0);
  GPR_ASSERT(bytes == nullptr);
  /* Validate frame protector. */
  tsi_frame_protector* protector = nullptr;
  GPR_ASSERT(tsi_handshaker_result_create_frame_protector(
                 result, nullptr, &protector) == TSI_OK);
  GPR_ASSERT(protector != nullptr);
  tsi_frame_protector_destroy(protector);
  tsi_handshaker_result_destroy(result);
  signal(&tsi_to_caller_notification);
}

static tsi_result mock_client_start(alts_handshaker_client* client) {
  if (!should_handshaker_client_api_succeed) {
    return TSI_INTERNAL_ERROR;
  }
  alts_handshaker_client_check_fields_for_testing(
      client, on_client_start_success_cb, nullptr, false, nullptr);
  /* Populate handshaker response for client_start request. */
  grpc_byte_buffer** recv_buffer_ptr =
      alts_handshaker_client_get_recv_buffer_addr_for_testing(client);
  *recv_buffer_ptr = generate_handshaker_response(CLIENT_START);
  cb_event = client;
  signal(&caller_to_tsi_notification);
  return TSI_OK;
}

static void mock_shutdown(alts_handshaker_client* self) {}

static tsi_result mock_server_start(alts_handshaker_client* client,
                                    grpc_slice* bytes_received) {
  if (!should_handshaker_client_api_succeed) {
    return TSI_INTERNAL_ERROR;
  }
  alts_handshaker_client_check_fields_for_testing(
      client, on_server_start_success_cb, nullptr, false, nullptr);
  grpc_slice slice = grpc_empty_slice();
  GPR_ASSERT(grpc_slice_cmp(*bytes_received, slice) == 0);
  /* Populate handshaker response for server_start request. */
  grpc_byte_buffer** recv_buffer_ptr =
      alts_handshaker_client_get_recv_buffer_addr_for_testing(client);
  *recv_buffer_ptr = generate_handshaker_response(SERVER_START);
  cb_event = client;
  grpc_slice_unref(slice);
  signal(&caller_to_tsi_notification);
  return TSI_OK;
}

static tsi_result mock_next(alts_handshaker_client* client,
                            grpc_slice* bytes_received) {
  if (!should_handshaker_client_api_succeed) {
    return TSI_INTERNAL_ERROR;
  }
  alts_tsi_handshaker* handshaker =
      alts_handshaker_client_get_handshaker_for_testing(client);
  bool is_client = alts_tsi_handshaker_get_is_client_for_testing(handshaker);
  tsi_handshaker_on_next_done_cb cb =
      is_client ? on_client_next_success_cb : on_server_next_success_cb;
  alts_handshaker_client_set_cb_for_testing(client, cb);
  alts_handshaker_client_set_recv_bytes_for_testing(client, bytes_received);
  alts_handshaker_client_check_fields_for_testing(client, cb, nullptr, true,
                                                  bytes_received);
  GPR_ASSERT(bytes_received != nullptr);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(*bytes_received),
                    ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES,
                    GRPC_SLICE_LENGTH(*bytes_received)) == 0);
  /* Populate handshaker response for next request. */
  grpc_slice out_frame =
      grpc_slice_from_static_string(ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME);
  grpc_byte_buffer** recv_buffer_ptr =
      alts_handshaker_client_get_recv_buffer_addr_for_testing(client);
  *recv_buffer_ptr = is_client ? generate_handshaker_response(CLIENT_NEXT)
                               : generate_handshaker_response(SERVER_NEXT);
  alts_handshaker_client_set_recv_bytes_for_testing(client, &out_frame);
  cb_event = client;
  signal(&caller_to_tsi_notification);
  grpc_slice_unref(out_frame);
  return TSI_OK;
}

static void mock_destruct(alts_handshaker_client* client) {}

static alts_handshaker_client_vtable vtable = {mock_client_start,
                                               mock_server_start, mock_next,
                                               mock_shutdown, mock_destruct};

static tsi_handshaker* create_test_handshaker(bool is_client) {
  tsi_handshaker* handshaker = nullptr;
  grpc_alts_credentials_options* options =
      grpc_alts_credentials_client_options_create();
  alts_tsi_handshaker_create(options, "target_name",
                             ALTS_HANDSHAKER_SERVICE_URL_FOR_TESTING, is_client,
                             nullptr, &handshaker);
  alts_tsi_handshaker* alts_handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(handshaker);
  alts_tsi_handshaker_set_client_vtable_for_testing(alts_handshaker, &vtable);
  grpc_alts_credentials_options_destroy(options);
  return handshaker;
}

static void check_handshaker_next_invalid_input() {
  /* Initialization. */
  tsi_handshaker* handshaker = create_test_handshaker(true);
  /* Check nullptr handshaker. */
  GPR_ASSERT(tsi_handshaker_next(nullptr, nullptr, 0, nullptr, nullptr, nullptr,
                                 check_must_not_be_called,
                                 nullptr) == TSI_INVALID_ARGUMENT);
  /* Check nullptr callback. */
  GPR_ASSERT(tsi_handshaker_next(handshaker, nullptr, 0, nullptr, nullptr,
                                 nullptr, nullptr,
                                 nullptr) == TSI_INVALID_ARGUMENT);
  /* Cleanup. */
  tsi_handshaker_destroy(handshaker);
}

static void check_handshaker_shutdown_invalid_input() {
  /* Initialization. */
  tsi_handshaker* handshaker = create_test_handshaker(true /* is_client */);
  /* Check nullptr handshaker. */
  tsi_handshaker_shutdown(nullptr);
  /* Cleanup. */
  tsi_handshaker_destroy(handshaker);
}

static void check_handshaker_next_success() {
  /**
   * Create handshakers for which internal mock client is going to do
   * correctness check.
   */
  tsi_handshaker* client_handshaker =
      create_test_handshaker(true /* is_client */);
  tsi_handshaker* server_handshaker =
      create_test_handshaker(false /* is_client */);
  /* Client start. */
  GPR_ASSERT(tsi_handshaker_next(client_handshaker, nullptr, 0, nullptr,
                                 nullptr, nullptr, on_client_start_success_cb,
                                 nullptr) == TSI_ASYNC);
  wait(&tsi_to_caller_notification);
  /* Client next. */
  GPR_ASSERT(tsi_handshaker_next(
                 client_handshaker,
                 (const unsigned char*)ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES,
                 strlen(ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES), nullptr, nullptr,
                 nullptr, on_client_next_success_cb, nullptr) == TSI_ASYNC);
  wait(&tsi_to_caller_notification);
  /* Server start. */
  GPR_ASSERT(tsi_handshaker_next(server_handshaker, nullptr, 0, nullptr,
                                 nullptr, nullptr, on_server_start_success_cb,
                                 nullptr) == TSI_ASYNC);
  wait(&tsi_to_caller_notification);
  /* Server next. */
  GPR_ASSERT(tsi_handshaker_next(
                 server_handshaker,
                 (const unsigned char*)ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES,
                 strlen(ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES), nullptr, nullptr,
                 nullptr, on_server_next_success_cb, nullptr) == TSI_ASYNC);
  wait(&tsi_to_caller_notification);
  /* Cleanup. */
  tsi_handshaker_destroy(server_handshaker);
  tsi_handshaker_destroy(client_handshaker);
}

static void check_handshaker_next_with_shutdown() {
  tsi_handshaker* handshaker = create_test_handshaker(true /* is_client*/);
  /* next(success) -- shutdown(success) -- next (fail) */
  GPR_ASSERT(tsi_handshaker_next(handshaker, nullptr, 0, nullptr, nullptr,
                                 nullptr, on_client_start_success_cb,
                                 nullptr) == TSI_ASYNC);
  wait(&tsi_to_caller_notification);
  tsi_handshaker_shutdown(handshaker);
  GPR_ASSERT(tsi_handshaker_next(
                 handshaker,
                 (const unsigned char*)ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES,
                 strlen(ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES), nullptr, nullptr,
                 nullptr, on_client_next_success_cb,
                 nullptr) == TSI_HANDSHAKE_SHUTDOWN);
  /* Cleanup. */
  tsi_handshaker_destroy(handshaker);
}

static void check_handle_response_with_shutdown(void* unused) {
  wait(&caller_to_tsi_notification);
  alts_handshaker_client_handle_response(cb_event, true /* is_ok */);
}

static void check_handshaker_next_failure() {
  /**
   * Create handshakers for which internal mock client is always going to fail.
   */
  tsi_handshaker* client_handshaker =
      create_test_handshaker(true /* is_client */);
  tsi_handshaker* server_handshaker =
      create_test_handshaker(false /* is_client */);
  /* Client start. */
  GPR_ASSERT(tsi_handshaker_next(client_handshaker, nullptr, 0, nullptr,
                                 nullptr, nullptr, check_must_not_be_called,
                                 nullptr) == TSI_INTERNAL_ERROR);
  /* Server start. */
  GPR_ASSERT(tsi_handshaker_next(server_handshaker, nullptr, 0, nullptr,
                                 nullptr, nullptr, check_must_not_be_called,
                                 nullptr) == TSI_INTERNAL_ERROR);
  /* Server next. */
  GPR_ASSERT(tsi_handshaker_next(
                 server_handshaker,
                 (const unsigned char*)ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES,
                 strlen(ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES), nullptr, nullptr,
                 nullptr, check_must_not_be_called,
                 nullptr) == TSI_INTERNAL_ERROR);
  /* Client next. */
  GPR_ASSERT(tsi_handshaker_next(
                 client_handshaker,
                 (const unsigned char*)ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES,
                 strlen(ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES), nullptr, nullptr,
                 nullptr, check_must_not_be_called,
                 nullptr) == TSI_INTERNAL_ERROR);
  /* Cleanup. */
  tsi_handshaker_destroy(server_handshaker);
  tsi_handshaker_destroy(client_handshaker);
}

static void on_invalid_input_cb(tsi_result status, void* user_data,
                                const unsigned char* bytes_to_send,
                                size_t bytes_to_send_size,
                                tsi_handshaker_result* result) {
  GPR_ASSERT(status == TSI_INTERNAL_ERROR);
  GPR_ASSERT(user_data == nullptr);
  GPR_ASSERT(bytes_to_send == nullptr);
  GPR_ASSERT(bytes_to_send_size == 0);
  GPR_ASSERT(result == nullptr);
}

static void on_failed_grpc_call_cb(tsi_result status, void* user_data,
                                   const unsigned char* bytes_to_send,
                                   size_t bytes_to_send_size,
                                   tsi_handshaker_result* result) {
  GPR_ASSERT(status == TSI_INTERNAL_ERROR);
  GPR_ASSERT(user_data == nullptr);
  GPR_ASSERT(bytes_to_send == nullptr);
  GPR_ASSERT(bytes_to_send_size == 0);
  GPR_ASSERT(result == nullptr);
}

static void check_handle_response_invalid_input() {
  /* Initialization. */
  notification_init(&caller_to_tsi_notification);
  notification_init(&tsi_to_caller_notification);
  /**
   * Create a handshaker at the client side, for which internal mock client is
   * always going to fail.
   */
  tsi_handshaker* handshaker = create_test_handshaker(true /* is_client */);
  tsi_handshaker_next(handshaker, nullptr, 0, nullptr, nullptr, nullptr,
                      on_client_start_success_cb, nullptr);
  alts_tsi_handshaker* alts_handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(handshaker);
  grpc_slice slice = grpc_empty_slice();
  grpc_byte_buffer* recv_buffer = grpc_raw_byte_buffer_create(&slice, 1);
  alts_handshaker_client* client =
      alts_tsi_handshaker_get_client_for_testing(alts_handshaker);
  /* Check nullptr handshaker. */
  alts_handshaker_client_set_fields_for_testing(client, nullptr,
                                                on_invalid_input_cb, nullptr,
                                                recv_buffer, GRPC_STATUS_OK);
  alts_handshaker_client_handle_response(client, true);
  /* Check nullptr recv_bytes. */
  alts_handshaker_client_set_fields_for_testing(client, alts_handshaker,
                                                on_invalid_input_cb, nullptr,
                                                nullptr, GRPC_STATUS_OK);
  alts_handshaker_client_handle_response(client, true);
  /* Check failed grpc call made to handshaker service. */
  alts_handshaker_client_set_fields_for_testing(
      client, alts_handshaker, on_failed_grpc_call_cb, nullptr, recv_buffer,
      GRPC_STATUS_UNKNOWN);
  alts_handshaker_client_handle_response(client, true);
  alts_handshaker_client_set_fields_for_testing(client, alts_handshaker,
                                                on_failed_grpc_call_cb, nullptr,
                                                recv_buffer, GRPC_STATUS_OK);
  alts_handshaker_client_handle_response(client, false);
  /* Cleanup. */
  grpc_slice_unref(slice);
  tsi_handshaker_destroy(handshaker);
  notification_destroy(&caller_to_tsi_notification);
  notification_destroy(&tsi_to_caller_notification);
}

static void on_invalid_resp_cb(tsi_result status, void* user_data,
                               const unsigned char* bytes_to_send,
                               size_t bytes_to_send_size,
                               tsi_handshaker_result* result) {
  GPR_ASSERT(status == TSI_DATA_CORRUPTED);
  GPR_ASSERT(user_data == nullptr);
  GPR_ASSERT(bytes_to_send == nullptr);
  GPR_ASSERT(bytes_to_send_size == 0);
  GPR_ASSERT(result == nullptr);
}

static void check_handle_response_invalid_resp() {
  /* Initialization. */
  notification_init(&caller_to_tsi_notification);
  notification_init(&tsi_to_caller_notification);
  /**
   * Create a handshaker at the client side, for which internal mock client is
   * always going to fail.
   */
  tsi_handshaker* handshaker = create_test_handshaker(true /* is_client */);
  tsi_handshaker_next(handshaker, nullptr, 0, nullptr, nullptr, nullptr,
                      on_client_start_success_cb, nullptr);
  alts_tsi_handshaker* alts_handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(handshaker);
  alts_handshaker_client* client =
      alts_tsi_handshaker_get_client_for_testing(alts_handshaker);
  /* Tests. */
  grpc_byte_buffer* recv_buffer = generate_handshaker_response(INVALID);
  alts_handshaker_client_set_fields_for_testing(client, alts_handshaker,
                                                on_invalid_resp_cb, nullptr,
                                                recv_buffer, GRPC_STATUS_OK);
  alts_handshaker_client_handle_response(client, true);
  /* Cleanup. */
  tsi_handshaker_destroy(handshaker);
  notification_destroy(&caller_to_tsi_notification);
  notification_destroy(&tsi_to_caller_notification);
}

static void check_handle_response_success(void* unused) {
  /* Client start. */
  wait(&caller_to_tsi_notification);
  alts_handshaker_client_handle_response(cb_event, true /* is_ok */);
  /* Client next. */
  wait(&caller_to_tsi_notification);
  alts_handshaker_client_handle_response(cb_event, true /* is_ok */);
  /* Server start. */
  wait(&caller_to_tsi_notification);
  alts_handshaker_client_handle_response(cb_event, true /* is_ok */);
  /* Server next. */
  wait(&caller_to_tsi_notification);
  alts_handshaker_client_handle_response(cb_event, true /* is_ok */);
}

static void on_failed_resp_cb(tsi_result status, void* user_data,
                              const unsigned char* bytes_to_send,
                              size_t bytes_to_send_size,
                              tsi_handshaker_result* result) {
  GPR_ASSERT(status == TSI_INVALID_ARGUMENT);
  GPR_ASSERT(user_data == nullptr);
  GPR_ASSERT(bytes_to_send == nullptr);
  GPR_ASSERT(bytes_to_send_size == 0);
  GPR_ASSERT(result == nullptr);
}

static void check_handle_response_failure() {
  /* Initialization. */
  notification_init(&caller_to_tsi_notification);
  notification_init(&tsi_to_caller_notification);
  /**
   * Create a handshaker at the client side, for which internal mock client is
   * always going to fail.
   */
  tsi_handshaker* handshaker = create_test_handshaker(true /* is_client */);
  tsi_handshaker_next(handshaker, nullptr, 0, nullptr, nullptr, nullptr,
                      on_client_start_success_cb, nullptr);
  alts_tsi_handshaker* alts_handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(handshaker);
  alts_handshaker_client* client =
      alts_tsi_handshaker_get_client_for_testing(alts_handshaker);
  /* Tests. */
  grpc_byte_buffer* recv_buffer = generate_handshaker_response(FAILED);
  alts_handshaker_client_set_fields_for_testing(client, alts_handshaker,
                                                on_failed_resp_cb, nullptr,
                                                recv_buffer, GRPC_STATUS_OK);
  alts_handshaker_client_handle_response(client, true /* is_ok*/);
  /* Cleanup. */
  tsi_handshaker_destroy(handshaker);
  notification_destroy(&caller_to_tsi_notification);
  notification_destroy(&tsi_to_caller_notification);
}

static void on_shutdown_resp_cb(tsi_result status, void* user_data,
                                const unsigned char* bytes_to_send,
                                size_t bytes_to_send_size,
                                tsi_handshaker_result* result) {
  GPR_ASSERT(status == TSI_HANDSHAKE_SHUTDOWN);
  GPR_ASSERT(user_data == nullptr);
  GPR_ASSERT(bytes_to_send == nullptr);
  GPR_ASSERT(bytes_to_send_size == 0);
  GPR_ASSERT(result == nullptr);
}

static void check_handle_response_after_shutdown() {
  /* Initialization. */
  notification_init(&caller_to_tsi_notification);
  notification_init(&tsi_to_caller_notification);
  tsi_handshaker* handshaker = create_test_handshaker(true /* is_client */);
  tsi_handshaker_next(handshaker, nullptr, 0, nullptr, nullptr, nullptr,
                      on_client_start_success_cb, nullptr);
  alts_tsi_handshaker* alts_handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(handshaker);
  alts_handshaker_client* client =
      alts_tsi_handshaker_get_client_for_testing(alts_handshaker);
  grpc_byte_buffer** recv_buffer_ptr =
      alts_handshaker_client_get_recv_buffer_addr_for_testing(client);
  grpc_byte_buffer_destroy(*recv_buffer_ptr);

  /* Tests. */
  tsi_handshaker_shutdown(handshaker);
  grpc_byte_buffer* recv_buffer = generate_handshaker_response(CLIENT_START);
  alts_handshaker_client_set_fields_for_testing(client, alts_handshaker,
                                                on_shutdown_resp_cb, nullptr,
                                                recv_buffer, GRPC_STATUS_OK);
  alts_handshaker_client_handle_response(client, true);
  /* Cleanup. */
  tsi_handshaker_destroy(handshaker);
  notification_destroy(&caller_to_tsi_notification);
  notification_destroy(&tsi_to_caller_notification);
}

void check_handshaker_next_fails_after_shutdown() {
  /* Initialization. */
  notification_init(&caller_to_tsi_notification);
  notification_init(&tsi_to_caller_notification);
  cb_event = nullptr;
  /* Tests. */
  grpc_core::Thread thd("alts_tsi_handshaker_test",
                        &check_handle_response_with_shutdown, nullptr);
  thd.Start();
  check_handshaker_next_with_shutdown();
  thd.Join();
  /* Cleanup. */
  notification_destroy(&caller_to_tsi_notification);
  notification_destroy(&tsi_to_caller_notification);
}

void check_handshaker_success() {
  /* Initialization. */
  notification_init(&caller_to_tsi_notification);
  notification_init(&tsi_to_caller_notification);
  /* Tests. */
  grpc_core::Thread thd("alts_tsi_handshaker_test",
                        &check_handle_response_success, nullptr);
  thd.Start();
  check_handshaker_next_success();
  thd.Join();
  /* Cleanup. */
  notification_destroy(&caller_to_tsi_notification);
  notification_destroy(&tsi_to_caller_notification);
}

int main(int argc, char** argv) {
  /* Initialization. */
  grpc_init();
  grpc_alts_shared_resource_dedicated_init();
  /* Tests. */
  should_handshaker_client_api_succeed = true;
  check_handshaker_success();
  check_handshaker_next_invalid_input();
  check_handshaker_next_fails_after_shutdown();
  check_handle_response_after_shutdown();
  should_handshaker_client_api_succeed = false;
  check_handshaker_shutdown_invalid_input();
  check_handshaker_next_failure();
  check_handle_response_invalid_input();
  check_handle_response_invalid_resp();
  check_handle_response_failure();
  /* Cleanup. */
  grpc_alts_shared_resource_dedicated_shutdown();
  grpc_shutdown();
  return 0;
}
