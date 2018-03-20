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
#include "src/core/tsi/alts/handshaker/alts_tsi_event.h"
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

using grpc_core::internal::
    alts_tsi_handshaker_get_has_sent_start_message_for_testing;
using grpc_core::internal::alts_tsi_handshaker_get_is_client_for_testing;
using grpc_core::internal::alts_tsi_handshaker_get_recv_bytes_for_testing;
using grpc_core::internal::alts_tsi_handshaker_set_client_for_testing;
using grpc_core::internal::alts_tsi_handshaker_set_recv_bytes_for_testing;

/* ALTS mock notification. */
typedef struct notification {
  gpr_cv cv;
  gpr_mu mu;
  bool notified;
} notification;

/* ALTS mock handshaker client. */
typedef struct alts_mock_handshaker_client {
  alts_handshaker_client base;
  bool used_for_success_test;
} alts_mock_handshaker_client;

/* Type of ALTS handshaker response. */
typedef enum {
  INVALID,
  FAILED,
  CLIENT_START,
  SERVER_START,
  CLIENT_NEXT,
  SERVER_NEXT,
} alts_handshaker_response_type;

static alts_tsi_event* client_start_event;
static alts_tsi_event* client_next_event;
static alts_tsi_event* server_start_event;
static alts_tsi_event* server_next_event;
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

static tsi_result mock_client_start(alts_handshaker_client* self,
                                    alts_tsi_event* event) {
  alts_mock_handshaker_client* client =
      reinterpret_cast<alts_mock_handshaker_client*>(self);
  if (!client->used_for_success_test) {
    alts_tsi_event_destroy(event);
    return TSI_INTERNAL_ERROR;
  }
  GPR_ASSERT(event->cb == on_client_start_success_cb);
  GPR_ASSERT(event->user_data == nullptr);
  GPR_ASSERT(!alts_tsi_handshaker_get_has_sent_start_message_for_testing(
      event->handshaker));
  /* Populate handshaker response for client_start request. */
  event->recv_buffer = generate_handshaker_response(CLIENT_START);
  client_start_event = event;
  signal(&caller_to_tsi_notification);
  return TSI_OK;
}

static tsi_result mock_server_start(alts_handshaker_client* self,
                                    alts_tsi_event* event,
                                    grpc_slice* bytes_received) {
  alts_mock_handshaker_client* client =
      reinterpret_cast<alts_mock_handshaker_client*>(self);
  if (!client->used_for_success_test) {
    alts_tsi_event_destroy(event);
    return TSI_INTERNAL_ERROR;
  }
  GPR_ASSERT(event->cb == on_server_start_success_cb);
  GPR_ASSERT(event->user_data == nullptr);
  grpc_slice slice = grpc_empty_slice();
  GPR_ASSERT(grpc_slice_cmp(*bytes_received, slice) == 0);
  GPR_ASSERT(!alts_tsi_handshaker_get_has_sent_start_message_for_testing(
      event->handshaker));
  /* Populate handshaker response for server_start request. */
  event->recv_buffer = generate_handshaker_response(SERVER_START);
  server_start_event = event;
  grpc_slice_unref(slice);
  signal(&caller_to_tsi_notification);
  return TSI_OK;
}

static tsi_result mock_next(alts_handshaker_client* self, alts_tsi_event* event,
                            grpc_slice* bytes_received) {
  alts_mock_handshaker_client* client =
      reinterpret_cast<alts_mock_handshaker_client*>(self);
  if (!client->used_for_success_test) {
    alts_tsi_event_destroy(event);
    return TSI_INTERNAL_ERROR;
  }
  bool is_client =
      alts_tsi_handshaker_get_is_client_for_testing(event->handshaker);
  if (is_client) {
    GPR_ASSERT(event->cb == on_client_next_success_cb);
  } else {
    GPR_ASSERT(event->cb == on_server_next_success_cb);
  }
  GPR_ASSERT(event->user_data == nullptr);
  GPR_ASSERT(bytes_received != nullptr);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(*bytes_received),
                    ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES,
                    GRPC_SLICE_LENGTH(*bytes_received)) == 0);
  GPR_ASSERT(grpc_slice_cmp(alts_tsi_handshaker_get_recv_bytes_for_testing(
                                event->handshaker),
                            *bytes_received) == 0);
  GPR_ASSERT(alts_tsi_handshaker_get_has_sent_start_message_for_testing(
      event->handshaker));
  /* Populate handshaker response for next request. */
  grpc_slice out_frame =
      grpc_slice_from_static_string(ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME);
  if (is_client) {
    event->recv_buffer = generate_handshaker_response(CLIENT_NEXT);
  } else {
    event->recv_buffer = generate_handshaker_response(SERVER_NEXT);
  }
  alts_tsi_handshaker_set_recv_bytes_for_testing(event->handshaker, &out_frame);
  if (is_client) {
    client_next_event = event;
  } else {
    server_next_event = event;
  }
  signal(&caller_to_tsi_notification);
  grpc_slice_unref(out_frame);
  return TSI_OK;
}

static void mock_destruct(alts_handshaker_client* client) {}

static const alts_handshaker_client_vtable vtable = {
    mock_client_start, mock_server_start, mock_next, mock_destruct};

static alts_handshaker_client* alts_mock_handshaker_client_create(
    bool used_for_success_test) {
  alts_mock_handshaker_client* client =
      static_cast<alts_mock_handshaker_client*>(gpr_zalloc(sizeof(*client)));
  client->base.vtable = &vtable;
  client->used_for_success_test = used_for_success_test;
  return &client->base;
}

static tsi_handshaker* create_test_handshaker(bool used_for_success_test,
                                              bool is_client) {
  tsi_handshaker* handshaker = nullptr;
  alts_handshaker_client* client =
      alts_mock_handshaker_client_create(used_for_success_test);
  grpc_alts_credentials_options* options =
      grpc_alts_credentials_client_options_create();
  alts_tsi_handshaker_create(options, "target_name", "lame", is_client,
                             &handshaker);
  alts_tsi_handshaker* alts_handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(handshaker);
  alts_tsi_handshaker_set_client_for_testing(alts_handshaker, client);
  grpc_alts_credentials_options_destroy(options);
  return handshaker;
}

static void check_handshaker_next_invalid_input() {
  /* Initialization. */
  tsi_handshaker* handshaker = create_test_handshaker(true, true);
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

static void check_handshaker_next_success() {
  /**
   * Create handshakers for which internal mock client is going to do
   * correctness check.
   */
  tsi_handshaker* client_handshaker = create_test_handshaker(
      true /* used_for_success_test */, true /* is_client */);
  tsi_handshaker* server_handshaker = create_test_handshaker(
      true /* used_for_success_test */, false /* is_client */);
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

static void check_handshaker_next_failure() {
  /**
   * Create handshakers for which internal mock client is always going to fail.
   */
  tsi_handshaker* client_handshaker = create_test_handshaker(
      false /* used_for_success_test */, true /* is_client */);
  tsi_handshaker* server_handshaker = create_test_handshaker(
      false /* used_for_success_test */, false /* is_client */);
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
  /**
   * Create a handshaker at the client side, for which internal mock client is
   * always going to fail.
   */
  tsi_handshaker* handshaker = create_test_handshaker(
      false /* used_for_success_test */, true /* is_client */);
  alts_tsi_handshaker* alts_handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(handshaker);
  grpc_byte_buffer recv_buffer;
  /* Check nullptr handshaker. */
  alts_tsi_handshaker_handle_response(nullptr, &recv_buffer, GRPC_STATUS_OK,
                                      nullptr, on_invalid_input_cb, nullptr,
                                      true);
  /* Check nullptr recv_bytes. */
  alts_tsi_handshaker_handle_response(alts_handshaker, nullptr, GRPC_STATUS_OK,
                                      nullptr, on_invalid_input_cb, nullptr,
                                      true);
  /* Check failed grpc call made to handshaker service. */
  alts_tsi_handshaker_handle_response(alts_handshaker, &recv_buffer,
                                      GRPC_STATUS_UNKNOWN, nullptr,
                                      on_failed_grpc_call_cb, nullptr, true);

  alts_tsi_handshaker_handle_response(alts_handshaker, &recv_buffer,
                                      GRPC_STATUS_OK, nullptr,
                                      on_failed_grpc_call_cb, nullptr, false);

  /* Cleanup. */
  tsi_handshaker_destroy(handshaker);
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
  /**
   * Create a handshaker at the client side, for which internal mock client is
   * always going to fail.
   */
  tsi_handshaker* handshaker = create_test_handshaker(
      false /* used_for_success_test */, true /* is_client */);
  alts_tsi_handshaker* alts_handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(handshaker);
  /* Tests. */
  grpc_byte_buffer* recv_buffer = generate_handshaker_response(INVALID);
  alts_tsi_handshaker_handle_response(alts_handshaker, recv_buffer,
                                      GRPC_STATUS_OK, nullptr,
                                      on_invalid_resp_cb, nullptr, true);
  /* Cleanup. */
  grpc_byte_buffer_destroy(recv_buffer);
  tsi_handshaker_destroy(handshaker);
}

static void check_handle_response_success(void* unused) {
  /* Client start. */
  wait(&caller_to_tsi_notification);
  alts_tsi_event_dispatch_to_handshaker(client_start_event, true /* is_ok */);
  alts_tsi_event_destroy(client_start_event);
  /* Client next. */
  wait(&caller_to_tsi_notification);
  alts_tsi_event_dispatch_to_handshaker(client_next_event, true /* is_ok */);
  alts_tsi_event_destroy(client_next_event);
  /* Server start. */
  wait(&caller_to_tsi_notification);
  alts_tsi_event_dispatch_to_handshaker(server_start_event, true /* is_ok */);
  alts_tsi_event_destroy(server_start_event);
  /* Server next. */
  wait(&caller_to_tsi_notification);
  alts_tsi_event_dispatch_to_handshaker(server_next_event, true /* is_ok */);
  alts_tsi_event_destroy(server_next_event);
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
  /**
   * Create a handshaker at the client side, for which internal mock client is
   * always going to fail.
   */
  tsi_handshaker* handshaker = create_test_handshaker(
      false /* used_for_success_test */, true /* is_client */);
  alts_tsi_handshaker* alts_handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(handshaker);
  /* Tests. */
  grpc_byte_buffer* recv_buffer = generate_handshaker_response(FAILED);
  alts_tsi_handshaker_handle_response(alts_handshaker, recv_buffer,
                                      GRPC_STATUS_OK, nullptr,
                                      on_failed_resp_cb, nullptr, true);
  grpc_byte_buffer_destroy(recv_buffer);
  /* Cleanup. */
  tsi_handshaker_destroy(handshaker);
}

void check_handshaker_success() {
  /* Initialization. */
  notification_init(&caller_to_tsi_notification);
  notification_init(&tsi_to_caller_notification);
  client_start_event = nullptr;
  client_next_event = nullptr;
  server_start_event = nullptr;
  server_next_event = nullptr;
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
  /* Tests. */
  check_handshaker_success();
  check_handshaker_next_invalid_input();
  check_handshaker_next_failure();
  check_handle_response_invalid_input();
  check_handle_response_invalid_resp();
  check_handle_response_failure();
  /* Cleanup. */
  grpc_shutdown();
  return 0;
}
