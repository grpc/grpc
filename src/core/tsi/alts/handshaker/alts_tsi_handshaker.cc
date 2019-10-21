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
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd_id.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/tsi/alts/frame_protector/alts_frame_protector.h"
#include "src/core/tsi/alts/handshaker/alts_handshaker_client.h"
#include "src/core/tsi/alts/handshaker/alts_shared_resource.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_utils.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.h"

/* Main struct for ALTS TSI handshaker. */
struct alts_tsi_handshaker {
  tsi_handshaker base;
  // mu synchronizes all fields below, including the inner fields of "client"
  gpr_mu mu;
  alts_handshaker_client* client;
  grpc_slice target_name;
  bool is_client;
  bool has_sent_start_message;
  bool has_created_handshaker_client;
  char* handshaker_service_url;
  grpc_pollset_set* interested_parties;
  grpc_alts_credentials_options* options;
  alts_handshaker_client_vtable* client_vtable_for_testing;
  bool use_dedicated_cq;
  grpc_channel* channel;
  // shutdown effectively follows base.handshake_shutdown,
  // but is synchronized by the mutex of this object.
  bool shutdown;
  bool receive_status_pending;
  bool tsi_destroy_called;
  grpc_millis handshake_rpc_deadline_ms;
};

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
      /*is_integrity_only=*/false, /*enable_extra_copy=*/false,
      max_output_protected_frame_size, protector);
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
  grpc_slice_unref_internal(result->rpc_versions);
  gpr_free(result);
}

static const tsi_handshaker_result_vtable result_vtable = {
    handshaker_result_extract_peer,
    handshaker_result_create_zero_copy_grpc_protector,
    handshaker_result_create_frame_protector,
    handshaker_result_get_unused_bytes, handshaker_result_destroy};

tsi_result alts_tsi_handshaker_result_create(grpc_gcp_HandshakerResp* resp,
                                             bool is_client,
                                             tsi_handshaker_result** self) {
  if (self == nullptr || resp == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to create_handshaker_result()");
    return TSI_INVALID_ARGUMENT;
  }
  const grpc_gcp_HandshakerResult* hresult =
      grpc_gcp_HandshakerResp_result(resp);
  const grpc_gcp_Identity* identity =
      grpc_gcp_HandshakerResult_peer_identity(hresult);
  if (identity == nullptr) {
    gpr_log(GPR_ERROR, "Invalid identity");
    return TSI_FAILED_PRECONDITION;
  }
  upb_strview service_account = grpc_gcp_Identity_service_account(identity);
  if (service_account.size == 0) {
    gpr_log(GPR_ERROR, "Invalid service account");
    return TSI_FAILED_PRECONDITION;
  }
  upb_strview key_data = grpc_gcp_HandshakerResult_key_data(hresult);
  if (key_data.size < kAltsAes128GcmRekeyKeyLength) {
    gpr_log(GPR_ERROR, "Bad key length");
    return TSI_FAILED_PRECONDITION;
  }
  const grpc_gcp_RpcProtocolVersions* peer_rpc_version =
      grpc_gcp_HandshakerResult_peer_rpc_versions(hresult);
  if (peer_rpc_version == nullptr) {
    gpr_log(GPR_ERROR, "Peer does not set RPC protocol versions.");
    return TSI_FAILED_PRECONDITION;
  }
  alts_tsi_handshaker_result* result =
      static_cast<alts_tsi_handshaker_result*>(gpr_zalloc(sizeof(*result)));
  result->key_data =
      static_cast<char*>(gpr_zalloc(kAltsAes128GcmRekeyKeyLength));
  memcpy(result->key_data, key_data.data, kAltsAes128GcmRekeyKeyLength);
  result->peer_identity =
      static_cast<char*>(gpr_zalloc(service_account.size + 1));
  memcpy(result->peer_identity, service_account.data, service_account.size);
  upb::Arena arena;
  bool serialized = grpc_gcp_rpc_protocol_versions_encode(
      peer_rpc_version, arena.ptr(), &result->rpc_versions);
  if (!serialized) {
    gpr_log(GPR_ERROR, "Failed to serialize peer's RPC protocol versions.");
    return TSI_FAILED_PRECONDITION;
  }
  result->is_client = is_client;
  result->base.vtable = &result_vtable;
  *self = &result->base;
  return TSI_OK;
}

/* gRPC provided callback used when gRPC thread model is applied. */
static void on_handshaker_service_resp_recv(void* arg, grpc_error* error) {
  alts_tsi_handshaker* handshaker = static_cast<alts_tsi_handshaker*>(arg);
  grpc_core::MutexLock lock(&handshaker->mu);
  if (handshaker->client == nullptr) {
    gpr_log(GPR_ERROR, "ALTS handshaker client is nullptr");
    return;
  }
  bool success = true;
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "ALTS handshaker on_handshaker_service_resp_recv error: %s",
            grpc_error_string(error));
    success = false;
  }
  alts_handshaker_client_handle_response_locked(handshaker->client, success);
}

void alts_tsi_handshaker_handle_response_dedicated(
    alts_tsi_handshaker* handshaker, bool success) {
  grpc_core::MutexLock lock(&handshaker->mu);
  alts_handshaker_client_handle_response_locked(handshaker->client, success);
}

/* gRPC provided callback used when dedicatd CQ and thread are used.
 * It serves to safely bring the control back to application. */
static void on_handshaker_service_resp_recv_dedicated(void* arg,
                                                      grpc_error* error) {
  alts_shared_resource_dedicated* resource =
      grpc_alts_get_shared_resource_dedicated();
  grpc_cq_end_op(resource->cq, arg, GRPC_ERROR_NONE,
                 [](void* done_arg, grpc_cq_completion* storage) {}, nullptr,
                 &resource->storage);
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
  alts_tsi_handshaker* handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(self);
  grpc_core::MutexLock lock(&handshaker->mu);
  if (self->handshake_shutdown) {
    gpr_log(GPR_ERROR, "TSI handshake shutdown");
    return TSI_HANDSHAKE_SHUTDOWN;
  }
  tsi_result ok = TSI_OK;
  if (!handshaker->has_created_handshaker_client) {
    if (handshaker->use_dedicated_cq) {
      grpc_alts_shared_resource_dedicated_start(
          handshaker->handshaker_service_url);
      handshaker->interested_parties =
          grpc_alts_get_shared_resource_dedicated()->interested_parties;
      GPR_ASSERT(handshaker->interested_parties != nullptr);
    }
    grpc_iomgr_cb_func grpc_cb = handshaker->use_dedicated_cq
                                     ? on_handshaker_service_resp_recv_dedicated
                                     : on_handshaker_service_resp_recv;
    handshaker->client = alts_grpc_handshaker_client_create_locked(
        handshaker, handshaker->options, handshaker->target_name, grpc_cb, cb,
        user_data, handshaker->client_vtable_for_testing,
        handshaker->is_client);
    if (handshaker->client == nullptr) {
      gpr_log(GPR_ERROR, "Failed to create ALTS handshaker client");
      return TSI_FAILED_PRECONDITION;
    }
    handshaker->has_created_handshaker_client = true;
  }
  if (handshaker->use_dedicated_cq &&
      handshaker->client_vtable_for_testing == nullptr) {
    GPR_ASSERT(grpc_cq_begin_op(grpc_alts_get_shared_resource_dedicated()->cq,
                                handshaker->client));
  }
  grpc_slice slice = (received_bytes == nullptr || received_bytes_size == 0)
                         ? grpc_empty_slice()
                         : grpc_slice_from_copied_buffer(
                               reinterpret_cast<const char*>(received_bytes),
                               received_bytes_size);
  if (!handshaker->has_sent_start_message) {
    ok = handshaker->is_client
             ? alts_handshaker_client_start_client_locked(handshaker->client)
             : alts_handshaker_client_start_server_locked(handshaker->client,
                                                          &slice);
    handshaker->has_sent_start_message = true;
    handshaker->receive_status_pending = true;
  } else {
    ok = alts_handshaker_client_next_locked(handshaker->client, &slice);
  }
  grpc_slice_unref_internal(slice);
  if (ok != TSI_OK) {
    handshaker->receive_status_pending = false;
    gpr_log(GPR_ERROR, "Failed to schedule ALTS handshaker requests");
    return ok;
  }
  return TSI_ASYNC;
}

/*
 * This API will be invoked by a non-gRPC application, and an ExecCtx needs
 * to be explicitly created in order to invoke ALTS handshaker client API's
 * that assumes the caller is inside gRPC core.
 */
static tsi_result handshaker_next_dedicated(
    tsi_handshaker* self, const unsigned char* received_bytes,
    size_t received_bytes_size, const unsigned char** bytes_to_send,
    size_t* bytes_to_send_size, tsi_handshaker_result** result,
    tsi_handshaker_on_next_done_cb cb, void* user_data) {
  grpc_core::ExecCtx exec_ctx;
  return handshaker_next(self, received_bytes, received_bytes_size,
                         bytes_to_send, bytes_to_send_size, result, cb,
                         user_data);
}

static void handshaker_shutdown(tsi_handshaker* self) {
  GPR_ASSERT(self != nullptr);
  alts_tsi_handshaker* handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(self);
  grpc_core::MutexLock lock(&handshaker->mu);
  if (handshaker->shutdown) {
    return;
  }
  alts_handshaker_client_shutdown_locked(handshaker->client);
  handshaker->shutdown = true;
}

static void handshaker_channel_destroy(void* arg, grpc_error* /* error */) {
  grpc_channel* c = static_cast<grpc_channel*>(arg);
  grpc_channel_destroy(c);
}

static void alts_tsi_handshaker_destroy_locked(alts_tsi_handshaker* self) {
  if (handshaker->channel != nullptr) {
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_CREATE(handshaker_channel_destroy, handshaker->channel,
                            grpc_schedule_on_exec_ctx),
        GRPC_ERROR_NONE);
  }
  alts_handshaker_client_destroy_locked(self->client);
  grpc_slice_unref_internal(self->target_name);
  grpc_alts_credentials_options_destroy(self->options);
  gpr_free(self->handshaker_service_url);
  gpr_mu_unlock(&self->mu);
  gpr_mu_destroy(&self->mu);
  gpr_free(self);
}

void alts_tsi_handshaker_on_status_received(void* arg, grpc_error* error) {
  alts_tsi_handshaker* handshaker = static_cast<alts_tsi_handshaker*>(arg);
  gpr_mu_lock(&handshaker->mu);
  alts_handshaker_client_on_status_received_locked(handshaker->client, error);
  GPR_ASSERT(handshaker->receive_status_pending);
  handshaker->receive_status_pending = false;
  if (handshaker->tsi_destroy_called) {
    alts_tsi_handshaker_destroy_locked(handshaker);
  } else {
    gpr_mu_unlock(&handshaker->mu);
  }
}

static void handshaker_orphan(tsi_handshaker* self) {
  if (self == nullptr) {
    return;
  }
  alts_tsi_handshaker* handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(self);
  gpr_mu_lock(&handshaker->mu);
  GPR_ASSERT(!handshaker->tsi_destroy_called);
  handshaker->tsi_destroy_called = true;
  if (!handshaker->receive_status_pending) {
    alts_tsi_handshaker_destroy_locked(handshaker);
  } else {
    // this is defensive in order to avoid leaving a stray/unpolled call
    alts_handshaker_client_cancel_call_locked(handshaker->client);
    gpr_mu_unlock(&handshaker->mu);
  }
}

static const tsi_handshaker_vtable handshaker_vtable = {
    nullptr, nullptr,           nullptr,         nullptr,
    nullptr, handshaker_orphan, handshaker_next, handshaker_shutdown};

static const tsi_handshaker_vtable handshaker_vtable_dedicated = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    handshaker_orphan,
    handshaker_next_dedicated,
    handshaker_shutdown};

bool alts_tsi_handshaker_has_shutdown_locked(alts_tsi_handshaker* handshaker) {
  GPR_ASSERT(handshaker != nullptr);
  return handshaker->shutdown;
}

tsi_result alts_tsi_handshaker_create(
    const grpc_alts_credentials_options* options, const char* target_name,
    const char* handshaker_service_url, bool is_client,
    grpc_pollset_set* interested_parties, grpc_millis handshake_rpc_deadline_ms,
    tsi_handshaker** self) {
  if (handshaker_service_url == nullptr || self == nullptr ||
      options == nullptr || (is_client && target_name == nullptr)) {
    gpr_log(GPR_ERROR, "Invalid arguments to alts_tsi_handshaker_create()");
    return TSI_INVALID_ARGUMENT;
  }
  alts_tsi_handshaker* handshaker =
      static_cast<alts_tsi_handshaker*>(gpr_zalloc(sizeof(*handshaker)));
  bool use_dedicated_cq = interested_parties == nullptr;
  gpr_mu_init(&handshaker->mu);
  handshaker->client = nullptr;
  handshaker->is_client = is_client;
  handshaker->has_sent_start_message = false;
  handshaker->target_name = target_name == nullptr
                                ? grpc_empty_slice()
                                : grpc_slice_from_static_string(target_name);
  handshaker->interested_parties = interested_parties;
  handshaker->has_created_handshaker_client = false;
  handshaker->handshaker_service_url = gpr_strdup(handshaker_service_url);
  handshaker->options = grpc_alts_credentials_options_copy(options);
  handshaker->base.vtable =
      use_dedicated_cq ? &handshaker_vtable_dedicated : &handshaker_vtable;
  handshaker->use_dedicated_cq = use_dedicated_cq;
  handshaker->handshake_rpc_deadline_ms = handshake_rpc_deadline_ms;
  handshaker->receive_status_pending = false;
  *self = &handshaker->base;
  return TSI_OK;
}

void alts_tsi_handshaker_result_set_unused_bytes(tsi_handshaker_result* self,
                                                 grpc_slice* recv_bytes,
                                                 size_t bytes_consumed) {
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

void alts_tsi_handshaker_re_enter_lock_then_continue_make_grpc_call(
    void* arg, grpc_error* unused_error) {
  alts_tsi_handshaker_re_enter_lock_then_continue_make_grpc_call_args* args =
      static_cast<
          alts_tsi_handshaker_re_enter_lock_then_continue_make_grpc_call_args*>(
          arg);
  alts_tsi_handshaker* handshaker = args->handshaker;
  bool is_start = args->is_start;
  gpr_free(args);
  if (is_start) {
    grpc_core::UniquePtr<char> handshaker_service_url;
    bool use_dedicated_cq;
    {
      grpc_core::MutexLock lock(&handshaker->mu);
      handshaker_service_url = grpc_core::UniquePtr<char>(
          gpr_strdup(handshaker->handshaker_service_url));
      use_dedicated_cq = handshaker->use_dedicated_cq;
    }
    // Explicitly create the channel without holding our own lock, just to
    // avoid any potential cycles with g_init_mu and our own lock.
    grpc_channel* channel;
    if (use_dedicated_cq) {
      channel = grpc_alts_get_shared_resource_dedicated()->channel;
    } else {
      channel = grpc_insecure_channel_create(handshaker_service_url.get(),
                                             nullptr, nullptr);
    }
    grpc_core::MutexLock lock(&handshaker->mu);
    GPR_ASSERT(handshaker->channel == nullptr);
    handshaker->channel = channel;
    grpc_slice slice =
        grpc_slice_from_copied_string(handshaker_service_url.get());
    grpc_core::ExecCtx::Get()->InvalidateNow();
    grpc_millis deadline = grpc_core::ExecCtx::Get()->Now() +
                           handshaker->handshake_rpc_deadline_ms;
    grpc_call* call = grpc_channel_create_pollset_set_call(
        handshaker->channel, nullptr, GRPC_PROPAGATE_DEFAULTS,
        handshaker->interested_parties,
        grpc_slice_from_static_string(ALTS_SERVICE_METHOD), &slice, deadline,
        nullptr);
    grpc_slice_unref_internal(slice);
    alts_handshaker_client_continue_make_grpc_call_locked(handshaker->client,
                                                          call);
    if (handshaker->shutdown) {
      grpc_call_cancel_internal(call);
    }
  } else {
    grpc_core::MutexLock lock(&handshaker->mu);
    alts_handshaker_client_continue_make_grpc_call_locked(handshaker->client,
                                                          nullptr);
  }
}

namespace grpc_core {
namespace internal {

bool alts_tsi_handshaker_get_has_sent_start_message_for_testing(
    alts_tsi_handshaker* handshaker) {
  GPR_ASSERT(handshaker != nullptr);
  return handshaker->has_sent_start_message;
}

void alts_tsi_handshaker_set_client_vtable_for_testing(
    alts_tsi_handshaker* handshaker, alts_handshaker_client_vtable* vtable) {
  GPR_ASSERT(handshaker != nullptr);
  handshaker->client_vtable_for_testing = vtable;
}

bool alts_tsi_handshaker_get_is_client_for_testing(
    alts_tsi_handshaker* handshaker) {
  GPR_ASSERT(handshaker != nullptr);
  return handshaker->is_client;
}

void alts_tsi_handshaker_set_receive_status_pending_for_testing(
    alts_tsi_handshaker* handshaker, bool receive_status_pending) {
  GPR_ASSERT(handshaker != nullptr);
  return handshaker->receive_status_pending = receive_status_pending;
}

alts_handshaker_client* alts_tsi_handshaker_get_client_for_testing(
    alts_tsi_handshaker* handshaker) {
  return handshaker->client;
}

void alts_tsi_handshaker_set_client_for_testing(
    alts_tsi_handshaker* handshaker, alts_handshaker_client* client) {
  handshaker->client = client;
}

}  // namespace internal
}  // namespace grpc_core
