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

#include <grpc/support/port_platform.h>

#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd_id.h>

#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/tsi/alts/frame_protector/alts_frame_protector.h"
#include "src/core/tsi/alts/handshaker/alts_handshaker_client.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_utils.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.h"
#include "src/core/tsi/alts_transport_security.h"

#define TSI_ALTS_INITIAL_BUFFER_SIZE 256

static alts_shared_resource* kSharedResource = alts_get_shared_resource();

/* Main struct for ALTS TSI handshaker. */
typedef struct alts_tsi_handshaker {
  tsi_handshaker base;
  alts_handshaker_client* client;
  grpc_slice recv_bytes;
  grpc_slice target_name;
  unsigned char* buffer;
  size_t buffer_size;
  bool is_client;
  bool has_sent_start_message;
  grpc_alts_credentials_options* options;
} alts_tsi_handshaker;

/* Main struct for ALTS TSI handshaker result. */
typedef struct alts_tsi_handshaker_result {
  tsi_handshaker_result base;
  char* peer_identity;
  char* key_data;
  unsigned char* unused_bytes;
  size_t unused_bytes_size;
  grpc_slice rpc_versions;
  bool is_client;
} alts_tsi_handshaker_result;

static tsi_result handshaker_result_extract_peer(
    const tsi_handshaker_result* self, tsi_peer* peer) {
  if (self == nullptr || peer == nullptr) {
    gpr_log(GPR_ERROR, "Invalid argument to handshaker_result_extract_peer()");
    return TSI_INVALID_ARGUMENT;
  }
  alts_tsi_handshaker_result* result =
      reinterpret_cast<alts_tsi_handshaker_result*>(
          const_cast<tsi_handshaker_result*>(self));
  GPR_ASSERT(kTsiAltsNumOfPeerProperties == 3);
  tsi_result ok = tsi_construct_peer(kTsiAltsNumOfPeerProperties, peer);
  int index = 0;
  if (ok != TSI_OK) {
    gpr_log(GPR_ERROR, "Failed to construct tsi peer");
    return ok;
  }
  GPR_ASSERT(&peer->properties[index] != nullptr);
  ok = tsi_construct_string_peer_property_from_cstring(
      TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
      &peer->properties[index]);
  if (ok != TSI_OK) {
    tsi_peer_destruct(peer);
    gpr_log(GPR_ERROR, "Failed to set tsi peer property");
    return ok;
  }
  index++;
  GPR_ASSERT(&peer->properties[index] != nullptr);
  ok = tsi_construct_string_peer_property_from_cstring(
      TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, result->peer_identity,
      &peer->properties[index]);
  if (ok != TSI_OK) {
    tsi_peer_destruct(peer);
    gpr_log(GPR_ERROR, "Failed to set tsi peer property");
  }
  index++;
  GPR_ASSERT(&peer->properties[index] != nullptr);
  ok = tsi_construct_string_peer_property(
      TSI_ALTS_RPC_VERSIONS,
      reinterpret_cast<char*>(GRPC_SLICE_START_PTR(result->rpc_versions)),
      GRPC_SLICE_LENGTH(result->rpc_versions), &peer->properties[2]);
  if (ok != TSI_OK) {
    tsi_peer_destruct(peer);
    gpr_log(GPR_ERROR, "Failed to set tsi peer property");
  }
  GPR_ASSERT(++index == kTsiAltsNumOfPeerProperties);
  return ok;
}

static tsi_result handshaker_result_create_zero_copy_grpc_protector(
    const tsi_handshaker_result* self, size_t* max_output_protected_frame_size,
    tsi_zero_copy_grpc_protector** protector) {
  if (self == nullptr || protector == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to create_zero_copy_grpc_protector()");
    return TSI_INVALID_ARGUMENT;
  }
  alts_tsi_handshaker_result* result =
      reinterpret_cast<alts_tsi_handshaker_result*>(
          const_cast<tsi_handshaker_result*>(self));
  tsi_result ok = alts_zero_copy_grpc_protector_create(
      reinterpret_cast<const uint8_t*>(result->key_data),
      kAltsAes128GcmRekeyKeyLength, /*is_rekey=*/true, result->is_client,
      /*is_integrity_only=*/false, max_output_protected_frame_size, protector);
  if (ok != TSI_OK) {
    gpr_log(GPR_ERROR, "Failed to create zero-copy grpc protector");
  }
  return ok;
}

static tsi_result handshaker_result_create_frame_protector(
    const tsi_handshaker_result* self, size_t* max_output_protected_frame_size,
    tsi_frame_protector** protector) {
  if (self == nullptr || protector == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to handshaker_result_create_frame_protector()");
    return TSI_INVALID_ARGUMENT;
  }
  alts_tsi_handshaker_result* result =
      reinterpret_cast<alts_tsi_handshaker_result*>(
          const_cast<tsi_handshaker_result*>(self));
  tsi_result ok = alts_create_frame_protector(
      reinterpret_cast<const uint8_t*>(result->key_data),
      kAltsAes128GcmRekeyKeyLength, result->is_client, /*is_rekey=*/true,
      max_output_protected_frame_size, protector);
  if (ok != TSI_OK) {
    gpr_log(GPR_ERROR, "Failed to create frame protector");
  }
  return ok;
}

static tsi_result handshaker_result_get_unused_bytes(
    const tsi_handshaker_result* self, const unsigned char** bytes,
    size_t* bytes_size) {
  if (self == nullptr || bytes == nullptr || bytes_size == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to handshaker_result_get_unused_bytes()");
    return TSI_INVALID_ARGUMENT;
  }
  alts_tsi_handshaker_result* result =
      reinterpret_cast<alts_tsi_handshaker_result*>(
          const_cast<tsi_handshaker_result*>(self));
  *bytes = result->unused_bytes;
  *bytes_size = result->unused_bytes_size;
  return TSI_OK;
}

static void handshaker_result_destroy(tsi_handshaker_result* self) {
  if (self == nullptr) {
    return;
  }
  alts_tsi_handshaker_result* result =
      reinterpret_cast<alts_tsi_handshaker_result*>(
          const_cast<tsi_handshaker_result*>(self));
  gpr_free(result->peer_identity);
  gpr_free(result->key_data);
  gpr_free(result->unused_bytes);
  grpc_slice_unref(result->rpc_versions);
  gpr_free(result);
}

static const tsi_handshaker_result_vtable result_vtable = {
    handshaker_result_extract_peer,
    handshaker_result_create_zero_copy_grpc_protector,
    handshaker_result_create_frame_protector,
    handshaker_result_get_unused_bytes, handshaker_result_destroy};

static tsi_result create_handshaker_result(grpc_gcp_handshaker_resp* resp,
                                           bool is_client,
                                           tsi_handshaker_result** self) {
  if (self == nullptr || resp == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to create_handshaker_result()");
    return TSI_INVALID_ARGUMENT;
  }
  grpc_slice* key = static_cast<grpc_slice*>(resp->result.key_data.arg);
  GPR_ASSERT(key != nullptr);
  grpc_slice* identity =
      static_cast<grpc_slice*>(resp->result.peer_identity.service_account.arg);
  if (identity == nullptr) {
    gpr_log(GPR_ERROR, "Invalid service account");
    return TSI_FAILED_PRECONDITION;
  }
  if (GRPC_SLICE_LENGTH(*key) < kAltsAes128GcmRekeyKeyLength) {
    gpr_log(GPR_ERROR, "Bad key length");
    return TSI_FAILED_PRECONDITION;
  }
  alts_tsi_handshaker_result* result =
      static_cast<alts_tsi_handshaker_result*>(gpr_zalloc(sizeof(*result)));
  result->key_data =
      static_cast<char*>(gpr_zalloc(kAltsAes128GcmRekeyKeyLength));
  memcpy(result->key_data, GRPC_SLICE_START_PTR(*key),
         kAltsAes128GcmRekeyKeyLength);
  result->peer_identity = grpc_slice_to_c_string(*identity);
  if (!resp->result.has_peer_rpc_versions) {
    gpr_log(GPR_ERROR, "Peer does not set RPC protocol versions.");
    return TSI_FAILED_PRECONDITION;
  }
  if (!grpc_gcp_rpc_protocol_versions_encode(&resp->result.peer_rpc_versions,
                                             &result->rpc_versions)) {
    gpr_log(GPR_ERROR, "Failed to serialize peer's RPC protocol versions.");
    return TSI_FAILED_PRECONDITION;
  }
  result->is_client = is_client;
  result->base.vtable = &result_vtable;
  *self = &result->base;
  return TSI_OK;
}

static tsi_result handshaker_next(
    tsi_handshaker* self, const unsigned char* received_bytes,
    size_t received_bytes_size, const unsigned char** bytes_to_send,
    size_t* bytes_to_send_size, tsi_handshaker_result** result,
    tsi_handshaker_on_next_done_cb cb, void* user_data) {
  if (self == nullptr || cb == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to handshaker_next()");
    return TSI_INVALID_ARGUMENT;
  }
  if (self->handshake_shutdown) {
    gpr_log(GPR_ERROR, "TSI handshake shutdown");
    return TSI_HANDSHAKE_SHUTDOWN;
  }
  alts_tsi_handshaker* handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(self);
  tsi_result ok = TSI_OK;
  alts_tsi_event* event = nullptr;
  ok = alts_tsi_event_create(handshaker, cb, user_data, handshaker->options,
                             handshaker->target_name, &event);
  if (ok != TSI_OK) {
    gpr_log(GPR_ERROR, "Failed to create ALTS TSI event");
    return ok;
  }
  grpc_slice slice = (received_bytes == nullptr || received_bytes_size == 0)
                         ? grpc_empty_slice()
                         : grpc_slice_from_copied_buffer(
                               reinterpret_cast<const char*>(received_bytes),
                               received_bytes_size);
  if (!handshaker->has_sent_start_message) {
    ok = handshaker->is_client
             ? alts_handshaker_client_start_client(handshaker->client, event)
             : alts_handshaker_client_start_server(handshaker->client, event,
                                                   &slice);
    handshaker->has_sent_start_message = true;
  } else {
    if (!GRPC_SLICE_IS_EMPTY(handshaker->recv_bytes)) {
      grpc_slice_unref(handshaker->recv_bytes);
    }
    handshaker->recv_bytes = grpc_slice_ref(slice);
    ok = alts_handshaker_client_next(handshaker->client, event, &slice);
  }
  grpc_slice_unref(slice);
  if (ok != TSI_OK) {
    gpr_log(GPR_ERROR, "Failed to schedule ALTS handshaker requests");
    return ok;
  }
  return TSI_ASYNC;
}

static void handshaker_shutdown(tsi_handshaker* self) {
  GPR_ASSERT(self != nullptr);
  if (self->handshake_shutdown) {
    return;
  }
  alts_tsi_handshaker* handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(self);
  alts_handshaker_client_shutdown(handshaker->client);
}

static void handshaker_destroy(tsi_handshaker* self) {
  if (self == nullptr) {
    return;
  }
  alts_tsi_handshaker* handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(self);
  alts_handshaker_client_destroy(handshaker->client);
  grpc_slice_unref(handshaker->recv_bytes);
  grpc_slice_unref(handshaker->target_name);
  grpc_alts_credentials_options_destroy(handshaker->options);
  gpr_free(handshaker->buffer);
  gpr_free(handshaker);
}

static const tsi_handshaker_vtable handshaker_vtable = {
    nullptr,         nullptr,
    nullptr,         nullptr,
    nullptr,         handshaker_destroy,
    handshaker_next, handshaker_shutdown};

static void thread_worker(void* arg) {
  while (true) {
    grpc_event event = grpc_completion_queue_next(
        kSharedResource->cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(event.type != GRPC_QUEUE_TIMEOUT);
    if (event.type == GRPC_QUEUE_SHUTDOWN) {
      /* signal alts_tsi_shutdown() to destroy completion queue. */
      grpc_tsi_alts_signal_for_cq_destroy();
      break;
    }
    /* event.type == GRPC_OP_COMPLETE. */
    alts_tsi_event* alts_event = static_cast<alts_tsi_event*>(event.tag);
    alts_tsi_event_dispatch_to_handshaker(alts_event, event.success);
    alts_tsi_event_destroy(alts_event);
  }
}

static void init_shared_resources(const char* handshaker_service_url) {
  GPR_ASSERT(handshaker_service_url != nullptr);
  gpr_mu_lock(&kSharedResource->mu);
  if (kSharedResource->channel == nullptr) {
    gpr_cv_init(&kSharedResource->cv);
    kSharedResource->channel =
        grpc_insecure_channel_create(handshaker_service_url, nullptr, nullptr);
    kSharedResource->cq = grpc_completion_queue_create_for_next(nullptr);
    kSharedResource->thread =
        grpc_core::Thread("alts_tsi_handshaker", &thread_worker, nullptr);
    kSharedResource->thread.Start();
  }
  gpr_mu_unlock(&kSharedResource->mu);
}

tsi_result alts_tsi_handshaker_create(
    const grpc_alts_credentials_options* options, const char* target_name,
    const char* handshaker_service_url, bool is_client, tsi_handshaker** self) {
  if (handshaker_service_url == nullptr || self == nullptr ||
      options == nullptr || (is_client && target_name == nullptr)) {
    gpr_log(GPR_ERROR, "Invalid arguments to alts_tsi_handshaker_create()");
    return TSI_INVALID_ARGUMENT;
  }
  init_shared_resources(handshaker_service_url);
  alts_handshaker_client* client = alts_grpc_handshaker_client_create(
      kSharedResource->channel, kSharedResource->cq, handshaker_service_url);
  if (client == nullptr) {
    gpr_log(GPR_ERROR, "Failed to create ALTS handshaker client");
    return TSI_FAILED_PRECONDITION;
  }
  alts_tsi_handshaker* handshaker =
      static_cast<alts_tsi_handshaker*>(gpr_zalloc(sizeof(*handshaker)));
  handshaker->client = client;
  handshaker->buffer_size = TSI_ALTS_INITIAL_BUFFER_SIZE;
  handshaker->buffer =
      static_cast<unsigned char*>(gpr_zalloc(handshaker->buffer_size));
  handshaker->is_client = is_client;
  handshaker->has_sent_start_message = false;
  handshaker->target_name = target_name == nullptr
                                ? grpc_empty_slice()
                                : grpc_slice_from_static_string(target_name);
  handshaker->options = grpc_alts_credentials_options_copy(options);
  handshaker->base.vtable = &handshaker_vtable;
  *self = &handshaker->base;
  return TSI_OK;
}

static bool is_handshake_finished_properly(grpc_gcp_handshaker_resp* resp) {
  GPR_ASSERT(resp != nullptr);
  if (resp->has_result) {
    return true;
  }
  return false;
}

static void set_unused_bytes(tsi_handshaker_result* self,
                             grpc_slice* recv_bytes, size_t bytes_consumed) {
  GPR_ASSERT(recv_bytes != nullptr && self != nullptr);
  if (GRPC_SLICE_LENGTH(*recv_bytes) == bytes_consumed) {
    return;
  }
  alts_tsi_handshaker_result* result =
      reinterpret_cast<alts_tsi_handshaker_result*>(self);
  result->unused_bytes_size = GRPC_SLICE_LENGTH(*recv_bytes) - bytes_consumed;
  result->unused_bytes =
      static_cast<unsigned char*>(gpr_zalloc(result->unused_bytes_size));
  memcpy(result->unused_bytes,
         GRPC_SLICE_START_PTR(*recv_bytes) + bytes_consumed,
         result->unused_bytes_size);
}

void alts_tsi_handshaker_handle_response(alts_tsi_handshaker* handshaker,
                                         grpc_byte_buffer* recv_buffer,
                                         grpc_status_code status,
                                         grpc_slice* details,
                                         tsi_handshaker_on_next_done_cb cb,
                                         void* user_data, bool is_ok) {
  /* Invalid input check. */
  if (cb == nullptr) {
    gpr_log(GPR_ERROR,
            "cb is nullptr in alts_tsi_handshaker_handle_response()");
    return;
  }
  if (handshaker == nullptr || recv_buffer == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to alts_tsi_handshaker_handle_response()");
    cb(TSI_INTERNAL_ERROR, user_data, nullptr, 0, nullptr);
    return;
  }
  if (handshaker->base.handshake_shutdown) {
    gpr_log(GPR_ERROR, "TSI handshake shutdown");
    cb(TSI_HANDSHAKE_SHUTDOWN, user_data, nullptr, 0, nullptr);
    return;
  }
  /* Failed grpc call check. */
  if (!is_ok || status != GRPC_STATUS_OK) {
    gpr_log(GPR_ERROR, "grpc call made to handshaker service failed");
    if (details != nullptr) {
      char* error_details = grpc_slice_to_c_string(*details);
      gpr_log(GPR_ERROR, "error details:%s", error_details);
      gpr_free(error_details);
    }
    cb(TSI_INTERNAL_ERROR, user_data, nullptr, 0, nullptr);
    return;
  }
  grpc_gcp_handshaker_resp* resp =
      alts_tsi_utils_deserialize_response(recv_buffer);
  /* Invalid handshaker response check. */
  if (resp == nullptr) {
    gpr_log(GPR_ERROR, "alts_tsi_utils_deserialize_response() failed");
    cb(TSI_DATA_CORRUPTED, user_data, nullptr, 0, nullptr);
    return;
  }
  grpc_slice* slice = static_cast<grpc_slice*>(resp->out_frames.arg);
  unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  if (slice != nullptr) {
    bytes_to_send_size = GRPC_SLICE_LENGTH(*slice);
    while (bytes_to_send_size > handshaker->buffer_size) {
      handshaker->buffer_size *= 2;
      handshaker->buffer = static_cast<unsigned char*>(
          gpr_realloc(handshaker->buffer, handshaker->buffer_size));
    }
    memcpy(handshaker->buffer, GRPC_SLICE_START_PTR(*slice),
           bytes_to_send_size);
    bytes_to_send = handshaker->buffer;
  }
  tsi_handshaker_result* result = nullptr;
  if (is_handshake_finished_properly(resp)) {
    create_handshaker_result(resp, handshaker->is_client, &result);
    set_unused_bytes(result, &handshaker->recv_bytes, resp->bytes_consumed);
  }
  grpc_status_code code = static_cast<grpc_status_code>(resp->status.code);
  grpc_gcp_handshaker_resp_destroy(resp);
  cb(alts_tsi_utils_convert_to_tsi_result(code), user_data, bytes_to_send,
     bytes_to_send_size, result);
}

namespace grpc_core {
namespace internal {

bool alts_tsi_handshaker_get_has_sent_start_message_for_testing(
    alts_tsi_handshaker* handshaker) {
  GPR_ASSERT(handshaker != nullptr);
  return handshaker->has_sent_start_message;
}

bool alts_tsi_handshaker_get_is_client_for_testing(
    alts_tsi_handshaker* handshaker) {
  GPR_ASSERT(handshaker != nullptr);
  return handshaker->is_client;
}

void alts_tsi_handshaker_set_recv_bytes_for_testing(
    alts_tsi_handshaker* handshaker, grpc_slice* slice) {
  GPR_ASSERT(handshaker != nullptr && slice != nullptr);
  handshaker->recv_bytes = grpc_slice_ref(*slice);
}

grpc_slice alts_tsi_handshaker_get_recv_bytes_for_testing(
    alts_tsi_handshaker* handshaker) {
  GPR_ASSERT(handshaker != nullptr);
  return handshaker->recv_bytes;
}

void alts_tsi_handshaker_set_client_for_testing(
    alts_tsi_handshaker* handshaker, alts_handshaker_client* client) {
  GPR_ASSERT(handshaker != nullptr && client != nullptr);
  alts_handshaker_client_destroy(handshaker->client);
  handshaker->client = client;
}

alts_handshaker_client* alts_tsi_handshaker_get_client_for_testing(
    alts_tsi_handshaker* handshaker) {
  return handshaker->client;
}

}  // namespace internal
}  // namespace grpc_core
