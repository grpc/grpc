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

#include <grpc/grpc.h>

#include "src/core/tsi/alts/handshaker/alts_handshaker_client.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_event.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "test/core/tsi/alts/handshaker/alts_handshaker_service_api_test_lib.h"

#define ALTS_HANDSHAKER_CLIENT_TEST_OUT_FRAME "Hello Google"
#define ALTS_HANDSHAKER_CLIENT_TEST_HANDSHAKER_SERVICE_URL "lame"
#define ALTS_HANDSHAKER_CLIENT_TEST_TARGET_NAME "bigtable.google.api.com"
#define ALTS_HANDSHAKER_CLIENT_TEST_TARGET_SERVICE_ACCOUNT1 "A@google.com"
#define ALTS_HANDSHAKER_CLIENT_TEST_TARGET_SERVICE_ACCOUNT2 "B@google.com"

const size_t kHandshakerClientOpNum = 4;
const size_t kMaxRpcVersionMajor = 3;
const size_t kMaxRpcVersionMinor = 2;
const size_t kMinRpcVersionMajor = 2;
const size_t kMinRpcVersionMinor = 1;

using grpc_core::internal::alts_handshaker_client_set_grpc_caller_for_testing;

typedef struct alts_handshaker_client_test_config {
  grpc_channel* channel;
  grpc_completion_queue* cq;
  alts_handshaker_client* client;
  grpc_slice out_frame;
} alts_handshaker_client_test_config;

static alts_tsi_event* alts_tsi_event_create_for_testing(bool is_client) {
  alts_tsi_event* e = static_cast<alts_tsi_event*>(gpr_zalloc(sizeof(*e)));
  grpc_metadata_array_init(&e->initial_metadata);
  grpc_metadata_array_init(&e->trailing_metadata);
  e->options = is_client ? grpc_alts_credentials_client_options_create()
                         : grpc_alts_credentials_server_options_create();
  if (is_client) {
    grpc_alts_credentials_client_options_add_target_service_account(
        e->options, ALTS_HANDSHAKER_CLIENT_TEST_TARGET_SERVICE_ACCOUNT1);
    grpc_alts_credentials_client_options_add_target_service_account(
        e->options, ALTS_HANDSHAKER_CLIENT_TEST_TARGET_SERVICE_ACCOUNT2);
  }
  grpc_gcp_rpc_protocol_versions* versions = &e->options->rpc_versions;
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_max(
      versions, kMaxRpcVersionMajor, kMaxRpcVersionMinor));
  GPR_ASSERT(grpc_gcp_rpc_protocol_versions_set_min(
      versions, kMinRpcVersionMajor, kMinRpcVersionMinor));
  e->target_name =
      grpc_slice_from_static_string(ALTS_HANDSHAKER_CLIENT_TEST_TARGET_NAME);
  return e;
}

static void validate_rpc_protocol_versions(
    grpc_gcp_rpc_protocol_versions* versions) {
  GPR_ASSERT(versions != nullptr);
  GPR_ASSERT(versions->max_rpc_version.major == kMaxRpcVersionMajor);
  GPR_ASSERT(versions->max_rpc_version.minor == kMaxRpcVersionMinor);
  GPR_ASSERT(versions->min_rpc_version.major == kMinRpcVersionMajor);
  GPR_ASSERT(versions->min_rpc_version.minor == kMinRpcVersionMinor);
}

static void validate_target_identities(
    const repeated_field* target_identity_head) {
  grpc_gcp_identity* target_identity1 = static_cast<grpc_gcp_identity*>(
      const_cast<void*>(target_identity_head->next->data));
  grpc_gcp_identity* target_identity2 = static_cast<grpc_gcp_identity*>(
      const_cast<void*>(target_identity_head->data));
  grpc_slice* service_account1 =
      static_cast<grpc_slice*>(target_identity1->service_account.arg);
  grpc_slice* service_account2 =
      static_cast<grpc_slice*>(target_identity2->service_account.arg);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(*service_account1),
                    ALTS_HANDSHAKER_CLIENT_TEST_TARGET_SERVICE_ACCOUNT1,
                    GRPC_SLICE_LENGTH(*service_account1)) == 0);
  GPR_ASSERT(strlen(ALTS_HANDSHAKER_CLIENT_TEST_TARGET_SERVICE_ACCOUNT1) ==
             GRPC_SLICE_LENGTH(*service_account1));
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(*service_account2),
                    ALTS_HANDSHAKER_CLIENT_TEST_TARGET_SERVICE_ACCOUNT2,
                    GRPC_SLICE_LENGTH(*service_account2)) == 0);
  GPR_ASSERT(strlen(ALTS_HANDSHAKER_CLIENT_TEST_TARGET_SERVICE_ACCOUNT2) ==
             GRPC_SLICE_LENGTH(*service_account2));
}

/**
 * Validate if grpc operation data is correctly populated with the fields of
 * ALTS TSI event.
 */
static bool validate_op(alts_tsi_event* event, const grpc_op* op, size_t nops,
                        bool is_start) {
  GPR_ASSERT(event != nullptr && op != nullptr && nops != 0);
  bool ok = true;
  grpc_op* start_op = const_cast<grpc_op*>(op);
  if (is_start) {
    ok &= (op->op == GRPC_OP_SEND_INITIAL_METADATA);
    ok &= (op->data.send_initial_metadata.count == 0);
    op++;
    GPR_ASSERT((size_t)(op - start_op) <= kHandshakerClientOpNum);

    ok &= (op->op == GRPC_OP_RECV_INITIAL_METADATA);
    ok &= (op->data.recv_initial_metadata.recv_initial_metadata ==
           &event->initial_metadata);
    op++;
    GPR_ASSERT((size_t)(op - start_op) <= kHandshakerClientOpNum);
  }
  ok &= (op->op == GRPC_OP_SEND_MESSAGE);
  ok &= (op->data.send_message.send_message == event->send_buffer);
  op++;
  GPR_ASSERT((size_t)(op - start_op) <= kHandshakerClientOpNum);

  ok &= (op->op == GRPC_OP_RECV_MESSAGE);
  ok &= (op->data.recv_message.recv_message == &event->recv_buffer);
  op++;
  GPR_ASSERT((size_t)(op - start_op) <= kHandshakerClientOpNum);

  return ok;
}

static grpc_gcp_handshaker_req* deserialize_handshaker_req(
    grpc_gcp_handshaker_req_type type, grpc_byte_buffer* buffer) {
  GPR_ASSERT(buffer != nullptr);
  grpc_gcp_handshaker_req* req = grpc_gcp_handshaker_decoded_req_create(type);
  grpc_byte_buffer_reader bbr;
  GPR_ASSERT(grpc_byte_buffer_reader_init(&bbr, buffer));
  grpc_slice slice = grpc_byte_buffer_reader_readall(&bbr);
  GPR_ASSERT(grpc_gcp_handshaker_req_decode(slice, req));
  grpc_slice_unref(slice);
  grpc_byte_buffer_reader_destroy(&bbr);
  return req;
}

/**
 * A mock grpc_caller used to check if client_start, server_start, and next
 * operations correctly handle invalid arguments. It should not be called.
 */
static grpc_call_error check_must_not_be_called(grpc_call* call,
                                                const grpc_op* ops, size_t nops,
                                                void* tag) {
  GPR_ASSERT(0);
}

/**
 * A mock grpc_caller used to check correct execution of client_start operation.
 * It checks if the client_start handshaker request is populated with correct
 * handshake_security_protocol, application_protocol, and record_protocol, and
 * op is correctly populated.
 */
static grpc_call_error check_client_start_success(grpc_call* call,
                                                  const grpc_op* op,
                                                  size_t nops, void* tag) {
  alts_tsi_event* event = static_cast<alts_tsi_event*>(tag);
  grpc_gcp_handshaker_req* req =
      deserialize_handshaker_req(CLIENT_START_REQ, event->send_buffer);
  GPR_ASSERT(req->client_start.handshake_security_protocol ==
             grpc_gcp_HandshakeProtocol_ALTS);
  const void* data = (static_cast<repeated_field*>(
                          req->client_start.application_protocols.arg))
                         ->data;
  GPR_ASSERT(data != nullptr);
  grpc_slice* application_protocol = (grpc_slice*)data;
  data = (static_cast<repeated_field*>(req->client_start.record_protocols.arg))
             ->data;
  grpc_slice* record_protocol = (grpc_slice*)data;
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(*application_protocol),
                    ALTS_APPLICATION_PROTOCOL,
                    GRPC_SLICE_LENGTH(*application_protocol)) == 0);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(*record_protocol),
                    ALTS_RECORD_PROTOCOL,
                    GRPC_SLICE_LENGTH(*record_protocol)) == 0);
  validate_rpc_protocol_versions(&req->client_start.rpc_versions);
  validate_target_identities(
      static_cast<repeated_field*>(req->client_start.target_identities.arg));
  grpc_slice* target_name =
      static_cast<grpc_slice*>(req->client_start.target_name.arg);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(*target_name),
                    ALTS_HANDSHAKER_CLIENT_TEST_TARGET_NAME,
                    GRPC_SLICE_LENGTH(*target_name)) == 0);
  GPR_ASSERT(GRPC_SLICE_LENGTH(*target_name) ==
             strlen(ALTS_HANDSHAKER_CLIENT_TEST_TARGET_NAME));
  GPR_ASSERT(validate_op(event, op, nops, true /* is_start */));
  grpc_gcp_handshaker_req_destroy(req);
  return GRPC_CALL_OK;
}

/**
 * A mock grpc_caller used to check correct execution of server_start operation.
 * It checks if the server_start handshaker request is populated with correct
 * handshake_security_protocol, application_protocol, and record_protocol, and
 * op is correctly populated.
 */
static grpc_call_error check_server_start_success(grpc_call* call,
                                                  const grpc_op* op,
                                                  size_t nops, void* tag) {
  alts_tsi_event* event = static_cast<alts_tsi_event*>(tag);
  grpc_gcp_handshaker_req* req =
      deserialize_handshaker_req(SERVER_START_REQ, event->send_buffer);
  const void* data = (static_cast<repeated_field*>(
                          req->server_start.application_protocols.arg))
                         ->data;
  GPR_ASSERT(data != nullptr);
  grpc_slice* application_protocol = (grpc_slice*)data;
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(*application_protocol),
                    ALTS_APPLICATION_PROTOCOL,
                    GRPC_SLICE_LENGTH(*application_protocol)) == 0);
  GPR_ASSERT(req->server_start.handshake_parameters_count == 1);
  GPR_ASSERT(req->server_start.handshake_parameters[0].key ==
             grpc_gcp_HandshakeProtocol_ALTS);
  data = (static_cast<repeated_field*>(req->server_start.handshake_parameters[0]
                                           .value.record_protocols.arg))
             ->data;
  GPR_ASSERT(data != nullptr);
  grpc_slice* record_protocol = (grpc_slice*)data;
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(*record_protocol),
                    ALTS_RECORD_PROTOCOL,
                    GRPC_SLICE_LENGTH(*record_protocol)) == 0);
  validate_rpc_protocol_versions(&req->server_start.rpc_versions);
  GPR_ASSERT(validate_op(event, op, nops, true /* is_start */));
  grpc_gcp_handshaker_req_destroy(req);
  return GRPC_CALL_OK;
}

/**
 * A mock grpc_caller used to check correct execution of next operation. It
 * checks if the next handshaker request is populated with correct information,
 * and op is correctly populated.
 */
static grpc_call_error check_next_success(grpc_call* call, const grpc_op* op,
                                          size_t nops, void* tag) {
  alts_tsi_event* event = static_cast<alts_tsi_event*>(tag);
  grpc_gcp_handshaker_req* req =
      deserialize_handshaker_req(NEXT_REQ, event->send_buffer);
  grpc_slice* in_bytes = static_cast<grpc_slice*>(req->next.in_bytes.arg);
  GPR_ASSERT(in_bytes != nullptr);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(*in_bytes),
                    ALTS_HANDSHAKER_CLIENT_TEST_OUT_FRAME,
                    GRPC_SLICE_LENGTH(*in_bytes)) == 0);
  GPR_ASSERT(validate_op(event, op, nops, false /* is_start */));
  grpc_gcp_handshaker_req_destroy(req);
  return GRPC_CALL_OK;
}
/**
 * A mock grpc_caller used to check if client_start, server_start, and next
 * operations correctly handle the situation when the grpc call made to the
 * handshaker service fails.
 */
static grpc_call_error check_grpc_call_failure(grpc_call* call,
                                               const grpc_op* op, size_t nops,
                                               void* tag) {
  return GRPC_CALL_ERROR;
}

static alts_handshaker_client_test_config* create_config() {
  alts_handshaker_client_test_config* config =
      static_cast<alts_handshaker_client_test_config*>(
          gpr_zalloc(sizeof(*config)));
  config->channel = grpc_insecure_channel_create(
      ALTS_HANDSHAKER_CLIENT_TEST_HANDSHAKER_SERVICE_URL, nullptr, nullptr);
  config->cq = grpc_completion_queue_create_for_next(nullptr);
  config->client = alts_grpc_handshaker_client_create(
      config->channel, config->cq,
      ALTS_HANDSHAKER_CLIENT_TEST_HANDSHAKER_SERVICE_URL);
  GPR_ASSERT(config->client != nullptr);
  config->out_frame =
      grpc_slice_from_static_string(ALTS_HANDSHAKER_CLIENT_TEST_OUT_FRAME);
  return config;
}

static void destroy_config(alts_handshaker_client_test_config* config) {
  if (config == nullptr) {
    return;
  }
  grpc_completion_queue_destroy(config->cq);
  grpc_channel_destroy(config->channel);
  alts_handshaker_client_destroy(config->client);
  grpc_slice_unref(config->out_frame);
  gpr_free(config);
}

static void schedule_request_invalid_arg_test() {
  /* Initialization. */
  alts_handshaker_client_test_config* config = create_config();
  alts_tsi_event* event = nullptr;

  /* Tests. */
  alts_handshaker_client_set_grpc_caller_for_testing(config->client,
                                                     check_must_not_be_called);
  event = alts_tsi_event_create_for_testing(true /* is_client */);
  /* Check client_start. */
  GPR_ASSERT(alts_handshaker_client_start_client(nullptr, event) ==
             TSI_INVALID_ARGUMENT);
  GPR_ASSERT(alts_handshaker_client_start_client(config->client, nullptr) ==
             TSI_INVALID_ARGUMENT);

  /* Check server_start. */
  GPR_ASSERT(alts_handshaker_client_start_server(
                 config->client, event, nullptr) == TSI_INVALID_ARGUMENT);
  GPR_ASSERT(alts_handshaker_client_start_server(config->client, nullptr,
                                                 &config->out_frame) ==
             TSI_INVALID_ARGUMENT);
  GPR_ASSERT(alts_handshaker_client_start_server(
                 nullptr, event, &config->out_frame) == TSI_INVALID_ARGUMENT);

  /* Check next. */
  GPR_ASSERT(alts_handshaker_client_next(config->client, event, nullptr) ==
             TSI_INVALID_ARGUMENT);
  GPR_ASSERT(alts_handshaker_client_next(config->client, nullptr,
                                         &config->out_frame) ==
             TSI_INVALID_ARGUMENT);
  GPR_ASSERT(alts_handshaker_client_next(nullptr, event, &config->out_frame) ==
             TSI_INVALID_ARGUMENT);

  /* Cleanup. */
  alts_tsi_event_destroy(event);
  destroy_config(config);
}

static void schedule_request_success_test() {
  /* Initialization. */
  alts_handshaker_client_test_config* config = create_config();
  alts_tsi_event* event = nullptr;

  /* Check client_start success. */
  alts_handshaker_client_set_grpc_caller_for_testing(
      config->client, check_client_start_success);
  event = alts_tsi_event_create_for_testing(true /* is_client. */);
  GPR_ASSERT(alts_handshaker_client_start_client(config->client, event) ==
             TSI_OK);
  alts_tsi_event_destroy(event);

  /* Check server_start success. */
  alts_handshaker_client_set_grpc_caller_for_testing(
      config->client, check_server_start_success);
  event = alts_tsi_event_create_for_testing(false /* is_client. */);
  GPR_ASSERT(alts_handshaker_client_start_server(config->client, event,
                                                 &config->out_frame) == TSI_OK);
  alts_tsi_event_destroy(event);

  /* Check next success. */
  alts_handshaker_client_set_grpc_caller_for_testing(config->client,
                                                     check_next_success);
  event = alts_tsi_event_create_for_testing(true /* is_client. */);
  GPR_ASSERT(alts_handshaker_client_next(config->client, event,
                                         &config->out_frame) == TSI_OK);
  alts_tsi_event_destroy(event);

  /* Cleanup. */
  destroy_config(config);
}

static void schedule_request_grpc_call_failure_test() {
  /* Initialization. */
  alts_handshaker_client_test_config* config = create_config();
  alts_tsi_event* event = nullptr;

  /* Check client_start failure. */
  alts_handshaker_client_set_grpc_caller_for_testing(config->client,
                                                     check_grpc_call_failure);
  event = alts_tsi_event_create_for_testing(true /* is_client. */);
  GPR_ASSERT(alts_handshaker_client_start_client(config->client, event) ==
             TSI_INTERNAL_ERROR);
  alts_tsi_event_destroy(event);

  /* Check server_start failure. */
  event = alts_tsi_event_create_for_testing(false /* is_client. */);
  GPR_ASSERT(alts_handshaker_client_start_server(config->client, event,
                                                 &config->out_frame) ==
             TSI_INTERNAL_ERROR);
  alts_tsi_event_destroy(event);

  /* Check next failure. */
  event = alts_tsi_event_create_for_testing(true /* is_cleint. */);
  GPR_ASSERT(
      alts_handshaker_client_next(config->client, event, &config->out_frame) ==
      TSI_INTERNAL_ERROR);
  alts_tsi_event_destroy(event);

  /* Cleanup. */
  destroy_config(config);
}

int main(int argc, char** argv) {
  /* Initialization. */
  grpc_init();

  /* Tests. */
  schedule_request_invalid_arg_test();
  schedule_request_success_test();
  schedule_request_grpc_call_failure_test();

  /* Cleanup. */
  grpc_shutdown();
  return 0;
}
