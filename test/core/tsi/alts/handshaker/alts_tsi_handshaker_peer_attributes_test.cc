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
#include <iostream>

#include <grpc/grpc.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/thd.h"
#include "src/core/tsi/alts/handshaker/alts_handshaker_client.h"
#include "src/core/tsi/alts/handshaker/alts_shared_resource.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker_private.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "src/proto/grpc/gcp/altscontext.upb.h"
#include "test/core/tsi/alts/handshaker/alts_handshaker_service_api_test_lib.h"
#include "test/core/util/test_config.h"

#define ALTS_TSI_HANDSHAKER_TEST_RECV_BYTES "Hello World"
#define ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME "Hello Google"
#define ALTS_TSI_HANDSHAKER_TEST_CONSUMED_BYTES "Hello "
#define ALTS_TSI_HANDSHAKER_TEST_REMAIN_BYTES "Google"
#define ALTS_TSI_HANDSHAKER_TEST_PEER_IDENTITY "chapi@service.google.com"
#define ALTS_TSI_HANDSHAKER_TEST_SECURITY_LEVEL "TSI_PRIVACY_AND_INTEGRITY"
#define ALTS_TSI_HANDSHAKER_TEST_KEY_DATA \
  "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKL"
#define ALTS_TSI_HANDSHAKER_TEST_BUFFER_SIZE 100
#define ALTS_TSI_HANDSHAKER_TEST_SLEEP_TIME_IN_SECONDS 2
#define ALTS_TSI_HANDSHAKER_TEST_MAX_RPC_VERSION_MAJOR 3
#define ALTS_TSI_HANDSHAKER_TEST_MAX_RPC_VERSION_MINOR 2
#define ALTS_TSI_HANDSHAKER_TEST_MIN_RPC_VERSION_MAJOR 2
#define ALTS_TSI_HANDSHAKER_TEST_MIN_RPC_VERSION_MINOR 1
#define ALTS_TSI_HANDSHAKER_TEST_LOCAL_IDENTITY "chapilocal@service.google.com"
#define ALTS_TSI_HANDSHAKER_TEST_APPLICATION_PROTOCOL \
  "test application protocol"
#define ALTS_TSI_HANDSHAKER_TEST_RECORD_PROTOCOL "test record protocol"
#define ALTS_TSI_HANDSHAKER_TEST_MAX_FRAME_SIZE 256 * 1024

#define ALTS_TSI_HANDSHAKER_TEST_ATTRIBUTE_KEY "peer"
#define ALTS_TSI_HANDSHAKER_TEST_ATTRIBUTE_VALUE "attribute"

using grpc_core::internal::alts_handshaker_client_check_fields_for_testing;
using grpc_core::internal::alts_handshaker_client_get_handshaker_for_testing;
using grpc_core::internal::
    alts_handshaker_client_get_recv_buffer_addr_for_testing;
using grpc_core::internal::
    alts_handshaker_client_on_status_received_for_testing;
using grpc_core::internal::alts_handshaker_client_ref_for_testing;
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

static tsi_result peer_attribute_transfer_test() {
  upb::Arena arena;
  grpc_gcp_HandshakerResult* result;
  grpc_gcp_Identity* peer_identity_;
  grpc_gcp_HandshakerResp* resp = grpc_gcp_HandshakerResp_new(arena.ptr());
  grpc_gcp_HandshakerStatus* status = grpc_gcp_HandshakerResp_mutable_status(resp, arena.ptr());
  grpc_gcp_HandshakerStatus_set_code(status, 0);
  grpc_gcp_Identity* local_identity_;

  grpc_gcp_HandshakerResp_set_out_frames(resp, upb_strview_makez(ALTS_TSI_HANDSHAKER_TEST_OUT_FRAME));
  grpc_gcp_HandshakerResp_set_bytes_consumed(resp, strlen(ALTS_TSI_HANDSHAKER_TEST_CONSUMED_BYTES));
  result = grpc_gcp_HandshakerResp_mutable_result(resp, arena.ptr());
  peer_identity_ =grpc_gcp_HandshakerResult_mutable_peer_identity(result, arena.ptr());
  grpc_gcp_Identity_set_service_account(peer_identity_,upb_strview_makez(ALTS_TSI_HANDSHAKER_TEST_PEER_IDENTITY));
  grpc_gcp_HandshakerResult_set_key_data(result, upb_strview_makez(ALTS_TSI_HANDSHAKER_TEST_KEY_DATA));
  GPR_ASSERT(grpc_gcp_handshaker_resp_set_peer_rpc_versions(
      resp, arena.ptr(), ALTS_TSI_HANDSHAKER_TEST_MAX_RPC_VERSION_MAJOR,
      ALTS_TSI_HANDSHAKER_TEST_MAX_RPC_VERSION_MINOR,
      ALTS_TSI_HANDSHAKER_TEST_MIN_RPC_VERSION_MAJOR,
      ALTS_TSI_HANDSHAKER_TEST_MIN_RPC_VERSION_MINOR));
  local_identity_ = grpc_gcp_HandshakerResult_mutable_local_identity(result, arena.ptr());
  grpc_gcp_Identity_set_service_account(local_identity_,upb_strview_makez(ALTS_TSI_HANDSHAKER_TEST_LOCAL_IDENTITY));
  grpc_gcp_HandshakerResult_set_application_protocol(result,upb_strview_makez(ALTS_TSI_HANDSHAKER_TEST_APPLICATION_PROTOCOL));
  grpc_gcp_HandshakerResult_set_record_protocol(result, upb_strview_makez(ALTS_TSI_HANDSHAKER_TEST_RECORD_PROTOCOL));
  grpc_gcp_HandshakerResult_set_max_frame_size(result, ALTS_TSI_HANDSHAKER_TEST_MAX_FRAME_SIZE);

   

  grpc_gcp_Identity_AttributesEntry* peer_attributes_ = grpc_gcp_Identity_add_attributes(peer_identity_, arena.ptr());
  grpc_gcp_Identity_AttributesEntry_set_key(peer_attributes_, upb_strview_makez(ALTS_TSI_HANDSHAKER_TEST_ATTRIBUTE_KEY));
  grpc_gcp_Identity_AttributesEntry_set_value(peer_attributes_, upb_strview_makez(ALTS_TSI_HANDSHAKER_TEST_ATTRIBUTE_VALUE));
// Want to create filled example with attribute but how do you generate a full response?
  // where is HandshakerResp defined? want to fill that with test information

  
  // tsi_result TEST_RESULT = alts_tsi_handshaker_result_create(grpc_gcp_HandshakerResp* resp, bool is_client, tsi_handshaker_result** self);


  const grpc_gcp_HandshakerResult * hresult = const_cast<grpc_gcp_HandshakerResult*>(result);
  const grpc_gcp_Identity* identity =
      grpc_gcp_HandshakerResult_peer_identity(hresult);
  if (identity == nullptr) {
    gpr_log(GPR_ERROR, "Invalid identity");
    return TSI_FAILED_PRECONDITION;
  }
  upb_strview peer_service_account =
      grpc_gcp_Identity_service_account(identity);
  if (peer_service_account.size == 0) {
    gpr_log(GPR_ERROR, "Invalid peer service account");
    return TSI_FAILED_PRECONDITION;
  }
  upb_strview application_protocol =
      grpc_gcp_HandshakerResult_application_protocol(hresult);
  if (application_protocol.size == 0) {
    gpr_log(GPR_ERROR, "Invalid application protocol");
    return TSI_FAILED_PRECONDITION;
  }
  upb_strview record_protocol =
      grpc_gcp_HandshakerResult_record_protocol(hresult);
  if (record_protocol.size == 0) {
    gpr_log(GPR_ERROR, "Invalid record protocol");
    return TSI_FAILED_PRECONDITION;
  }

  const grpc_gcp_RpcProtocolVersions* peer_rpc_version =
      grpc_gcp_HandshakerResult_peer_rpc_versions(hresult);
  if (peer_rpc_version == nullptr) {
    gpr_log(GPR_ERROR, "Peer does not set RPC protocol versions.");
    return TSI_FAILED_PRECONDITION;
  }

  const grpc_gcp_Identity* local_identity =
      grpc_gcp_HandshakerResult_local_identity(hresult);
  if (local_identity == nullptr) {
    gpr_log(GPR_ERROR, "Invalid local identity");
    return TSI_FAILED_PRECONDITION;
  }


  upb_strview local_service_account = grpc_gcp_Identity_service_account(local_identity);
  upb::Arena rpc_versions_arena;

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


  // grpc_gcp_HandshakerResult* ncresult = grpc_gcp_HandshakerResp_mutable_result(resp, context_arena.ptr());
  grpc_gcp_Identity* peer_identity = grpc_gcp_HandshakerResult_mutable_peer_identity(result, context_arena.ptr());
  if(peer_identity == nullptr ) { //|| *peer_attributes_counter == nullptr) {
    gpr_log(GPR_ERROR, "Null Peer Identity.");
    return TSI_FAILED_PRECONDITION;
  }

  size_t dog = 2048; // removing this line removes errors messages e0612
  size_t* lenz = &dog;
  grpc_gcp_Identity_AttributesEntry** peer_attributes = grpc_gcp_Identity_mutable_attributes(peer_identity, lenz); // need size_t *len)
  // where do these labeled functions come from --> upb genreated files
  // grpc_gcp_Identity_AttributesEntry** peer_attributes_counter = peer_attributes;
  if(peer_attributes == nullptr ) { //|| *peer_attributes_counter == nullptr) {
    gpr_log(GPR_ERROR, "Null Peer Attributes.");
    return TSI_FAILED_PRECONDITION; //This is triggering
  }

}
int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  /* Initialization. */
  grpc_init();
  grpc_alts_shared_resource_dedicated_init();
  /* Tests. */
  peer_attribute_transfer_test();
  /* Cleanup. */
  grpc_alts_shared_resource_dedicated_shutdown();
  grpc_shutdown();
  return 0;
}