//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "upb/mem/arena.hpp"

#include <grpc/credentials.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd_id.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/tsi/alts/frame_protector/alts_frame_protector.h"
#include "src/core/tsi/alts/handshaker/alts_handshaker_client.h"
#include "src/core/tsi/alts/handshaker/alts_shared_resource.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.h"

// Main struct for ALTS TSI handshaker.
struct alts_tsi_handshaker {
  tsi_handshaker base;
  grpc_slice target_name;
  bool is_client;
  bool has_sent_start_message = false;
  bool has_created_handshaker_client = false;
  char* handshaker_service_url;
  grpc_pollset_set* interested_parties;
  grpc_alts_credentials_options* options;
  alts_handshaker_client_vtable* client_vtable_for_testing = nullptr;
  grpc_channel* channel = nullptr;
  bool use_dedicated_cq;
  // mu synchronizes all fields below. Note these are the
  // only fields that can be concurrently accessed (due to
  // potential concurrency of tsi_handshaker_shutdown and
  // tsi_handshaker_next).
  grpc_core::Mutex mu;
  alts_handshaker_client* client = nullptr;
  // shutdown effectively follows base.handshake_shutdown,
  // but is synchronized by the mutex of this object.
  bool shutdown = false;
  // Maximum frame size used by frame protector.
  size_t max_frame_size;
};

// Main struct for ALTS TSI handshaker result.
typedef struct alts_tsi_handshaker_result {
  tsi_handshaker_result base;
  char* peer_identity;
  char* key_data;
  unsigned char* unused_bytes;
  size_t unused_bytes_size;
  grpc_slice rpc_versions;
  bool is_client;
  grpc_slice serialized_context;
  // Peer's maximum frame size.
  size_t max_frame_size;
} alts_tsi_handshaker_result;

static tsi_result handshaker_result_extract_peer(
    const tsi_handshaker_result* self, tsi_peer* peer) {
  if (self == nullptr || peer == nullptr) {
    LOG(ERROR) << "Invalid argument to handshaker_result_extract_peer()";
    return TSI_INVALID_ARGUMENT;
  }
  alts_tsi_handshaker_result* result =
      reinterpret_cast<alts_tsi_handshaker_result*>(
          const_cast<tsi_handshaker_result*>(self));
  CHECK_EQ(kTsiAltsNumOfPeerProperties, 5u);
  tsi_result ok = tsi_construct_peer(kTsiAltsNumOfPeerProperties, peer);
  int index = 0;
  if (ok != TSI_OK) {
    LOG(ERROR) << "Failed to construct tsi peer";
    return ok;
  }
  CHECK_NE(&peer->properties[index], nullptr);
  ok = tsi_construct_string_peer_property_from_cstring(
      TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
      &peer->properties[index]);
  if (ok != TSI_OK) {
    tsi_peer_destruct(peer);
    LOG(ERROR) << "Failed to set tsi peer property";
    return ok;
  }
  index++;
  CHECK_NE(&peer->properties[index], nullptr);
  ok = tsi_construct_string_peer_property_from_cstring(
      TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, result->peer_identity,
      &peer->properties[index]);
  if (ok != TSI_OK) {
    tsi_peer_destruct(peer);
    LOG(ERROR) << "Failed to set tsi peer property";
  }
  index++;
  CHECK_NE(&peer->properties[index], nullptr);
  ok = tsi_construct_string_peer_property(
      TSI_ALTS_RPC_VERSIONS,
      reinterpret_cast<char*>(GRPC_SLICE_START_PTR(result->rpc_versions)),
      GRPC_SLICE_LENGTH(result->rpc_versions), &peer->properties[index]);
  if (ok != TSI_OK) {
    tsi_peer_destruct(peer);
    LOG(ERROR) << "Failed to set tsi peer property";
  }
  index++;
  CHECK_NE(&peer->properties[index], nullptr);
  ok = tsi_construct_string_peer_property(
      TSI_ALTS_CONTEXT,
      reinterpret_cast<char*>(GRPC_SLICE_START_PTR(result->serialized_context)),
      GRPC_SLICE_LENGTH(result->serialized_context), &peer->properties[index]);
  if (ok != TSI_OK) {
    tsi_peer_destruct(peer);
    LOG(ERROR) << "Failed to set tsi peer property";
  }
  index++;
  CHECK_NE(&peer->properties[index], nullptr);
  ok = tsi_construct_string_peer_property_from_cstring(
      TSI_SECURITY_LEVEL_PEER_PROPERTY,
      tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
      &peer->properties[index]);
  if (ok != TSI_OK) {
    tsi_peer_destruct(peer);
    LOG(ERROR) << "Failed to set tsi peer property";
  }
  CHECK(++index == kTsiAltsNumOfPeerProperties);
  return ok;
}

static tsi_result handshaker_result_get_frame_protector_type(
    const tsi_handshaker_result* /*self*/,
    tsi_frame_protector_type* frame_protector_type) {
  *frame_protector_type = TSI_FRAME_PROTECTOR_NORMAL_OR_ZERO_COPY;
  return TSI_OK;
}

static tsi_result handshaker_result_create_zero_copy_grpc_protector(
    const tsi_handshaker_result* self, size_t* max_output_protected_frame_size,
    tsi_zero_copy_grpc_protector** protector) {
  if (self == nullptr || protector == nullptr) {
    LOG(ERROR) << "Invalid arguments to create_zero_copy_grpc_protector()";
    return TSI_INVALID_ARGUMENT;
  }
  alts_tsi_handshaker_result* result =
      reinterpret_cast<alts_tsi_handshaker_result*>(
          const_cast<tsi_handshaker_result*>(self));

  // In case the peer does not send max frame size (e.g. peer is gRPC Go or
  // peer uses an old binary), the negotiated frame size is set to
  // kTsiAltsMinFrameSize (ignoring max_output_protected_frame_size value if
  // present). Otherwise, it is based on peer and user specified max frame
  // size (if present).
  size_t max_frame_size = kTsiAltsMinFrameSize;
  if (result->max_frame_size) {
    size_t peer_max_frame_size = result->max_frame_size;
    max_frame_size = std::min<size_t>(peer_max_frame_size,
                                      max_output_protected_frame_size == nullptr
                                          ? kTsiAltsMaxFrameSize
                                          : *max_output_protected_frame_size);
    max_frame_size = std::max<size_t>(max_frame_size, kTsiAltsMinFrameSize);
  }
  max_output_protected_frame_size = &max_frame_size;
  gpr_log(GPR_DEBUG,
          "After Frame Size Negotiation, maximum frame size used by frame "
          "protector equals %zu",
          *max_output_protected_frame_size);
  tsi_result ok = alts_zero_copy_grpc_protector_create(
      grpc_core::GsecKeyFactory({reinterpret_cast<uint8_t*>(result->key_data),
                                 kAltsAes128GcmRekeyKeyLength},
                                /*is_rekey=*/true),
      result->is_client,
      /*is_integrity_only=*/false, /*enable_extra_copy=*/false,
      max_output_protected_frame_size, protector);
  if (ok != TSI_OK) {
    LOG(ERROR) << "Failed to create zero-copy grpc protector";
  }
  return ok;
}

static tsi_result handshaker_result_create_frame_protector(
    const tsi_handshaker_result* self, size_t* max_output_protected_frame_size,
    tsi_frame_protector** protector) {
  if (self == nullptr || protector == nullptr) {
    LOG(ERROR)
        << "Invalid arguments to handshaker_result_create_frame_protector()";
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
    LOG(ERROR) << "Failed to create frame protector";
  }
  return ok;
}

static tsi_result handshaker_result_get_unused_bytes(
    const tsi_handshaker_result* self, const unsigned char** bytes,
    size_t* bytes_size) {
  if (self == nullptr || bytes == nullptr || bytes_size == nullptr) {
    LOG(ERROR) << "Invalid arguments to handshaker_result_get_unused_bytes()";
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
  grpc_core::CSliceUnref(result->rpc_versions);
  grpc_core::CSliceUnref(result->serialized_context);
  gpr_free(result);
}

static const tsi_handshaker_result_vtable result_vtable = {
    handshaker_result_extract_peer,
    handshaker_result_get_frame_protector_type,
    handshaker_result_create_zero_copy_grpc_protector,
    handshaker_result_create_frame_protector,
    handshaker_result_get_unused_bytes,
    handshaker_result_destroy};

tsi_result alts_tsi_handshaker_result_create(grpc_gcp_HandshakerResp* resp,
                                             bool is_client,
                                             tsi_handshaker_result** result) {
  if (result == nullptr || resp == nullptr) {
    LOG(ERROR) << "Invalid arguments to create_handshaker_result()";
    return TSI_INVALID_ARGUMENT;
  }
  const grpc_gcp_HandshakerResult* hresult =
      grpc_gcp_HandshakerResp_result(resp);
  const grpc_gcp_Identity* identity =
      grpc_gcp_HandshakerResult_peer_identity(hresult);
  if (identity == nullptr) {
    LOG(ERROR) << "Invalid identity";
    return TSI_FAILED_PRECONDITION;
  }
  upb_StringView peer_service_account =
      grpc_gcp_Identity_service_account(identity);
  if (peer_service_account.size == 0) {
    LOG(ERROR) << "Invalid peer service account";
    return TSI_FAILED_PRECONDITION;
  }
  upb_StringView key_data = grpc_gcp_HandshakerResult_key_data(hresult);
  if (key_data.size < kAltsAes128GcmRekeyKeyLength) {
    LOG(ERROR) << "Bad key length";
    return TSI_FAILED_PRECONDITION;
  }
  const grpc_gcp_RpcProtocolVersions* peer_rpc_version =
      grpc_gcp_HandshakerResult_peer_rpc_versions(hresult);
  if (peer_rpc_version == nullptr) {
    LOG(ERROR) << "Peer does not set RPC protocol versions.";
    return TSI_FAILED_PRECONDITION;
  }
  upb_StringView application_protocol =
      grpc_gcp_HandshakerResult_application_protocol(hresult);
  if (application_protocol.size == 0) {
    LOG(ERROR) << "Invalid application protocol";
    return TSI_FAILED_PRECONDITION;
  }
  upb_StringView record_protocol =
      grpc_gcp_HandshakerResult_record_protocol(hresult);
  if (record_protocol.size == 0) {
    LOG(ERROR) << "Invalid record protocol";
    return TSI_FAILED_PRECONDITION;
  }
  const grpc_gcp_Identity* local_identity =
      grpc_gcp_HandshakerResult_local_identity(hresult);
  if (local_identity == nullptr) {
    LOG(ERROR) << "Invalid local identity";
    return TSI_FAILED_PRECONDITION;
  }
  upb_StringView local_service_account =
      grpc_gcp_Identity_service_account(local_identity);
  // We don't check if local service account is empty here
  // because local identity could be empty in certain situations.
  alts_tsi_handshaker_result* sresult =
      grpc_core::Zalloc<alts_tsi_handshaker_result>();
  sresult->key_data =
      static_cast<char*>(gpr_zalloc(kAltsAes128GcmRekeyKeyLength));
  memcpy(sresult->key_data, key_data.data, kAltsAes128GcmRekeyKeyLength);
  sresult->peer_identity =
      static_cast<char*>(gpr_zalloc(peer_service_account.size + 1));
  memcpy(sresult->peer_identity, peer_service_account.data,
         peer_service_account.size);
  sresult->max_frame_size = grpc_gcp_HandshakerResult_max_frame_size(hresult);
  upb::Arena rpc_versions_arena;
  bool serialized = grpc_gcp_rpc_protocol_versions_encode(
      peer_rpc_version, rpc_versions_arena.ptr(), &sresult->rpc_versions);
  if (!serialized) {
    LOG(ERROR) << "Failed to serialize peer's RPC protocol versions.";
    return TSI_FAILED_PRECONDITION;
  }
  upb::Arena context_arena;
  grpc_gcp_AltsContext* context = grpc_gcp_AltsContext_new(context_arena.ptr());
  grpc_gcp_AltsContext_set_application_protocol(context, application_protocol);
  grpc_gcp_AltsContext_set_record_protocol(context, record_protocol);
  // ALTS currently only supports the security level of 2,
  // which is "grpc_gcp_INTEGRITY_AND_PRIVACY".
  grpc_gcp_AltsContext_set_security_level(context, 2);
  grpc_gcp_AltsContext_set_peer_service_account(context, peer_service_account);
  grpc_gcp_AltsContext_set_local_service_account(context,
                                                 local_service_account);
  grpc_gcp_AltsContext_set_peer_rpc_versions(
      context, const_cast<grpc_gcp_RpcProtocolVersions*>(peer_rpc_version));
  grpc_gcp_Identity* peer_identity = const_cast<grpc_gcp_Identity*>(identity);
  if (peer_identity == nullptr) {
    LOG(ERROR) << "Null peer identity in ALTS context.";
    return TSI_FAILED_PRECONDITION;
  }
  if (grpc_gcp_Identity_attributes_size(identity) != 0) {
    size_t iter = kUpb_Map_Begin;
    grpc_gcp_Identity_AttributesEntry* peer_attributes_entry =
        grpc_gcp_Identity_attributes_nextmutable(peer_identity, &iter);
    while (peer_attributes_entry != nullptr) {
      upb_StringView key = grpc_gcp_Identity_AttributesEntry_key(
          const_cast<grpc_gcp_Identity_AttributesEntry*>(
              peer_attributes_entry));
      upb_StringView val = grpc_gcp_Identity_AttributesEntry_value(
          const_cast<grpc_gcp_Identity_AttributesEntry*>(
              peer_attributes_entry));
      grpc_gcp_AltsContext_peer_attributes_set(context, key, val,
                                               context_arena.ptr());
      peer_attributes_entry =
          grpc_gcp_Identity_attributes_nextmutable(peer_identity, &iter);
    }
  }
  size_t serialized_ctx_length;
  char* serialized_ctx = grpc_gcp_AltsContext_serialize(
      context, context_arena.ptr(), &serialized_ctx_length);
  if (serialized_ctx == nullptr) {
    LOG(ERROR) << "Failed to serialize peer's ALTS context.";
    return TSI_FAILED_PRECONDITION;
  }
  sresult->serialized_context =
      grpc_slice_from_copied_buffer(serialized_ctx, serialized_ctx_length);
  sresult->is_client = is_client;
  sresult->base.vtable = &result_vtable;
  *result = &sresult->base;
  return TSI_OK;
}

// gRPC provided callback used when gRPC thread model is applied.
static void on_handshaker_service_resp_recv(void* arg,
                                            grpc_error_handle error) {
  alts_handshaker_client* client = static_cast<alts_handshaker_client*>(arg);
  if (client == nullptr) {
    LOG(ERROR) << "ALTS handshaker client is nullptr";
    return;
  }
  bool success = true;
  if (!error.ok()) {
    gpr_log(GPR_INFO,
            "ALTS handshaker on_handshaker_service_resp_recv error: %s",
            grpc_core::StatusToString(error).c_str());
    success = false;
  }
  alts_handshaker_client_handle_response(client, success);
}

// gRPC provided callback used when dedicatd CQ and thread are used.
// It serves to safely bring the control back to application.
static void on_handshaker_service_resp_recv_dedicated(
    void* arg, grpc_error_handle /*error*/) {
  alts_shared_resource_dedicated* resource =
      grpc_alts_get_shared_resource_dedicated();
  grpc_cq_end_op(
      resource->cq, arg, absl::OkStatus(),
      [](void* /*done_arg*/, grpc_cq_completion* /*storage*/) {}, nullptr,
      &resource->storage);
}

// Returns TSI_OK if and only if no error is encountered.
static tsi_result alts_tsi_handshaker_continue_handshaker_next(
    alts_tsi_handshaker* handshaker, const unsigned char* received_bytes,
    size_t received_bytes_size, tsi_handshaker_on_next_done_cb cb,
    void* user_data, std::string* error) {
  if (!handshaker->has_created_handshaker_client) {
    if (handshaker->channel == nullptr) {
      grpc_alts_shared_resource_dedicated_start(
          handshaker->handshaker_service_url);
      handshaker->interested_parties =
          grpc_alts_get_shared_resource_dedicated()->interested_parties;
      CHECK_NE(handshaker->interested_parties, nullptr);
    }
    grpc_iomgr_cb_func grpc_cb = handshaker->channel == nullptr
                                     ? on_handshaker_service_resp_recv_dedicated
                                     : on_handshaker_service_resp_recv;
    grpc_channel* channel =
        handshaker->channel == nullptr
            ? grpc_alts_get_shared_resource_dedicated()->channel
            : handshaker->channel;
    alts_handshaker_client* client = alts_grpc_handshaker_client_create(
        handshaker, channel, handshaker->handshaker_service_url,
        handshaker->interested_parties, handshaker->options,
        handshaker->target_name, grpc_cb, cb, user_data,
        handshaker->client_vtable_for_testing, handshaker->is_client,
        handshaker->max_frame_size, error);
    if (client == nullptr) {
      LOG(ERROR) << "Failed to create ALTS handshaker client";
      if (error != nullptr) *error = "Failed to create ALTS handshaker client";
      return TSI_FAILED_PRECONDITION;
    }
    {
      grpc_core::MutexLock lock(&handshaker->mu);
      CHECK_EQ(handshaker->client, nullptr);
      handshaker->client = client;
      if (handshaker->shutdown) {
        LOG(INFO) << "TSI handshake shutdown";
        if (error != nullptr) *error = "TSI handshaker shutdown";
        return TSI_HANDSHAKE_SHUTDOWN;
      }
    }
    handshaker->has_created_handshaker_client = true;
  }
  if (handshaker->channel == nullptr &&
      handshaker->client_vtable_for_testing == nullptr) {
    CHECK(grpc_cq_begin_op(grpc_alts_get_shared_resource_dedicated()->cq,
                           handshaker->client));
  }
  grpc_slice slice = (received_bytes == nullptr || received_bytes_size == 0)
                         ? grpc_empty_slice()
                         : grpc_slice_from_copied_buffer(
                               reinterpret_cast<const char*>(received_bytes),
                               received_bytes_size);
  tsi_result ok = TSI_OK;
  if (!handshaker->has_sent_start_message) {
    handshaker->has_sent_start_message = true;
    ok = handshaker->is_client
             ? alts_handshaker_client_start_client(handshaker->client)
             : alts_handshaker_client_start_server(handshaker->client, &slice);
    // It's unsafe for the current thread to access any state in handshaker
    // at this point, since alts_handshaker_client_start_client/server
    // have potentially just started an op batch on the handshake call.
    // The completion callback for that batch is unsynchronized and so
    // can invoke the TSI next API callback from any thread, at which point
    // there is nothing taking ownership of this handshaker to prevent it
    // from being destroyed.
  } else {
    ok = alts_handshaker_client_next(handshaker->client, &slice);
  }
  grpc_core::CSliceUnref(slice);
  return ok;
}

struct alts_tsi_handshaker_continue_handshaker_next_args {
  alts_tsi_handshaker* handshaker;
  std::unique_ptr<unsigned char> received_bytes;
  size_t received_bytes_size;
  tsi_handshaker_on_next_done_cb cb;
  void* user_data;
  grpc_closure closure;
  std::string* error = nullptr;
};

static void alts_tsi_handshaker_create_channel(
    void* arg, grpc_error_handle /* unused_error */) {
  alts_tsi_handshaker_continue_handshaker_next_args* next_args =
      static_cast<alts_tsi_handshaker_continue_handshaker_next_args*>(arg);
  alts_tsi_handshaker* handshaker = next_args->handshaker;
  CHECK_EQ(handshaker->channel, nullptr);
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  // Disable retries so that we quickly get a signal when the
  // handshake server is not reachable.
  grpc_arg disable_retries_arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_ENABLE_RETRIES), 0);
  grpc_channel_args args = {1, &disable_retries_arg};
  handshaker->channel = grpc_channel_create(
      next_args->handshaker->handshaker_service_url, creds, &args);
  grpc_channel_credentials_release(creds);
  tsi_result continue_next_result =
      alts_tsi_handshaker_continue_handshaker_next(
          handshaker, next_args->received_bytes.get(),
          next_args->received_bytes_size, next_args->cb, next_args->user_data,
          next_args->error);
  if (continue_next_result != TSI_OK) {
    next_args->cb(continue_next_result, next_args->user_data, nullptr, 0,
                  nullptr);
  }
  delete next_args;
}

static tsi_result handshaker_next(
    tsi_handshaker* self, const unsigned char* received_bytes,
    size_t received_bytes_size, const unsigned char** /*bytes_to_send*/,
    size_t* /*bytes_to_send_size*/, tsi_handshaker_result** /*result*/,
    tsi_handshaker_on_next_done_cb cb, void* user_data, std::string* error) {
  if (self == nullptr || cb == nullptr) {
    LOG(ERROR) << "Invalid arguments to handshaker_next()";
    if (error != nullptr) *error = "invalid argument";
    return TSI_INVALID_ARGUMENT;
  }
  alts_tsi_handshaker* handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(self);
  {
    grpc_core::MutexLock lock(&handshaker->mu);
    if (handshaker->shutdown) {
      LOG(INFO) << "TSI handshake shutdown";
      if (error != nullptr) *error = "handshake shutdown";
      return TSI_HANDSHAKE_SHUTDOWN;
    }
  }
  if (handshaker->channel == nullptr && !handshaker->use_dedicated_cq) {
    alts_tsi_handshaker_continue_handshaker_next_args* args =
        new alts_tsi_handshaker_continue_handshaker_next_args();
    args->handshaker = handshaker;
    args->received_bytes = nullptr;
    args->received_bytes_size = received_bytes_size;
    args->error = error;
    if (received_bytes_size > 0) {
      args->received_bytes = std::unique_ptr<unsigned char>(
          static_cast<unsigned char*>(gpr_zalloc(received_bytes_size)));
      memcpy(args->received_bytes.get(), received_bytes, received_bytes_size);
    }
    args->cb = cb;
    args->user_data = user_data;
    GRPC_CLOSURE_INIT(&args->closure, alts_tsi_handshaker_create_channel, args,
                      grpc_schedule_on_exec_ctx);
    // We continue this handshaker_next call at the bottom of the ExecCtx just
    // so that we can invoke grpc_channel_create at the bottom of the call
    // stack. Doing so avoids potential lock cycles between g_init_mu and other
    // mutexes within core that might be held on the current call stack
    // (note that g_init_mu gets acquired during channel creation).
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &args->closure, absl::OkStatus());
  } else {
    tsi_result ok = alts_tsi_handshaker_continue_handshaker_next(
        handshaker, received_bytes, received_bytes_size, cb, user_data, error);
    if (ok != TSI_OK) {
      LOG(ERROR) << "Failed to schedule ALTS handshaker requests";
      return ok;
    }
  }
  return TSI_ASYNC;
}

//
// This API will be invoked by a non-gRPC application, and an ExecCtx needs
// to be explicitly created in order to invoke ALTS handshaker client API's
// that assumes the caller is inside gRPC core.
//
static tsi_result handshaker_next_dedicated(
    tsi_handshaker* self, const unsigned char* received_bytes,
    size_t received_bytes_size, const unsigned char** bytes_to_send,
    size_t* bytes_to_send_size, tsi_handshaker_result** result,
    tsi_handshaker_on_next_done_cb cb, void* user_data, std::string* error) {
  grpc_core::ExecCtx exec_ctx;
  return handshaker_next(self, received_bytes, received_bytes_size,
                         bytes_to_send, bytes_to_send_size, result, cb,
                         user_data, error);
}

static void handshaker_shutdown(tsi_handshaker* self) {
  CHECK_NE(self, nullptr);
  alts_tsi_handshaker* handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(self);
  grpc_core::MutexLock lock(&handshaker->mu);
  if (handshaker->shutdown) {
    return;
  }
  if (handshaker->client != nullptr) {
    alts_handshaker_client_shutdown(handshaker->client);
  }
  handshaker->shutdown = true;
}

static void handshaker_destroy(tsi_handshaker* self) {
  if (self == nullptr) {
    return;
  }
  alts_tsi_handshaker* handshaker =
      reinterpret_cast<alts_tsi_handshaker*>(self);
  alts_handshaker_client_destroy(handshaker->client);
  grpc_core::CSliceUnref(handshaker->target_name);
  grpc_alts_credentials_options_destroy(handshaker->options);
  if (handshaker->channel != nullptr) {
    grpc_channel_destroy_internal(handshaker->channel);
  }
  gpr_free(handshaker->handshaker_service_url);
  delete handshaker;
}

static const tsi_handshaker_vtable handshaker_vtable = {
    nullptr,         nullptr,
    nullptr,         nullptr,
    nullptr,         handshaker_destroy,
    handshaker_next, handshaker_shutdown};

static const tsi_handshaker_vtable handshaker_vtable_dedicated = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    handshaker_destroy,
    handshaker_next_dedicated,
    handshaker_shutdown};

bool alts_tsi_handshaker_has_shutdown(alts_tsi_handshaker* handshaker) {
  CHECK_NE(handshaker, nullptr);
  grpc_core::MutexLock lock(&handshaker->mu);
  return handshaker->shutdown;
}

tsi_result alts_tsi_handshaker_create(
    const grpc_alts_credentials_options* options, const char* target_name,
    const char* handshaker_service_url, bool is_client,
    grpc_pollset_set* interested_parties, tsi_handshaker** self,
    size_t user_specified_max_frame_size) {
  if (handshaker_service_url == nullptr || self == nullptr ||
      options == nullptr || (is_client && target_name == nullptr)) {
    LOG(ERROR) << "Invalid arguments to alts_tsi_handshaker_create()";
    return TSI_INVALID_ARGUMENT;
  }
  bool use_dedicated_cq = interested_parties == nullptr;
  alts_tsi_handshaker* handshaker = new alts_tsi_handshaker();
  memset(&handshaker->base, 0, sizeof(handshaker->base));
  handshaker->base.vtable =
      use_dedicated_cq ? &handshaker_vtable_dedicated : &handshaker_vtable;
  handshaker->target_name = target_name == nullptr
                                ? grpc_empty_slice()
                                : grpc_slice_from_static_string(target_name);
  handshaker->is_client = is_client;
  handshaker->handshaker_service_url = gpr_strdup(handshaker_service_url);
  handshaker->interested_parties = interested_parties;
  handshaker->options = grpc_alts_credentials_options_copy(options);
  handshaker->use_dedicated_cq = use_dedicated_cq;
  handshaker->max_frame_size = user_specified_max_frame_size != 0
                                   ? user_specified_max_frame_size
                                   : kTsiAltsMaxFrameSize;
  *self = &handshaker->base;
  return TSI_OK;
}

void alts_tsi_handshaker_result_set_unused_bytes(tsi_handshaker_result* result,
                                                 grpc_slice* recv_bytes,
                                                 size_t bytes_consumed) {
  CHECK(recv_bytes != nullptr);
  CHECK_NE(result, nullptr);
  if (GRPC_SLICE_LENGTH(*recv_bytes) == bytes_consumed) {
    return;
  }
  alts_tsi_handshaker_result* sresult =
      reinterpret_cast<alts_tsi_handshaker_result*>(result);
  sresult->unused_bytes_size = GRPC_SLICE_LENGTH(*recv_bytes) - bytes_consumed;
  sresult->unused_bytes =
      static_cast<unsigned char*>(gpr_zalloc(sresult->unused_bytes_size));
  memcpy(sresult->unused_bytes,
         GRPC_SLICE_START_PTR(*recv_bytes) + bytes_consumed,
         sresult->unused_bytes_size);
}

namespace grpc_core {
namespace internal {

bool alts_tsi_handshaker_get_has_sent_start_message_for_testing(
    alts_tsi_handshaker* handshaker) {
  CHECK_NE(handshaker, nullptr);
  return handshaker->has_sent_start_message;
}

void alts_tsi_handshaker_set_client_vtable_for_testing(
    alts_tsi_handshaker* handshaker, alts_handshaker_client_vtable* vtable) {
  CHECK_NE(handshaker, nullptr);
  handshaker->client_vtable_for_testing = vtable;
}

bool alts_tsi_handshaker_get_is_client_for_testing(
    alts_tsi_handshaker* handshaker) {
  CHECK_NE(handshaker, nullptr);
  return handshaker->is_client;
}

alts_handshaker_client* alts_tsi_handshaker_get_client_for_testing(
    alts_tsi_handshaker* handshaker) {
  return handshaker->client;
}

}  // namespace internal
}  // namespace grpc_core
