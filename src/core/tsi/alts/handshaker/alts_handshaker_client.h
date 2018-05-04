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

#ifndef GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_CLIENT_H
#define GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_CLIENT_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

#include "src/core/tsi/alts/handshaker/alts_tsi_event.h"

#define ALTS_SERVICE_METHOD "/grpc.gcp.HandshakerService/DoHandshake"
#define ALTS_APPLICATION_PROTOCOL "grpc"
#define ALTS_RECORD_PROTOCOL "ALTSRP_GCM_AES128_REKEY"

const size_t kAltsAes128GcmRekeyKeyLength = 44;

/**
 * A ALTS handshaker client interface. It is used to communicate with
 * ALTS handshaker service by scheduling a handshaker request that could be one
 * of client_start, server_start, and next handshaker requests. All APIs in the
 * header are thread-compatible.
 */
typedef struct alts_handshaker_client alts_handshaker_client;

/* A function that makes the grpc call to the handshaker service. */
typedef grpc_call_error (*alts_grpc_caller)(grpc_call* call, const grpc_op* ops,
                                            size_t nops, void* tag);

/* V-table for ALTS handshaker client operations. */
typedef struct alts_handshaker_client_vtable {
  tsi_result (*client_start)(alts_handshaker_client* client,
                             alts_tsi_event* event);
  tsi_result (*server_start)(alts_handshaker_client* client,
                             alts_tsi_event* event, grpc_slice* bytes_received);
  tsi_result (*next)(alts_handshaker_client* client, alts_tsi_event* event,
                     grpc_slice* bytes_received);
  void (*shutdown)(alts_handshaker_client* client);
  void (*destruct)(alts_handshaker_client* client);
} alts_handshaker_client_vtable;

struct alts_handshaker_client {
  const alts_handshaker_client_vtable* vtable;
};

/**
 * This method schedules a client_start handshaker request to ALTS handshaker
 * service.
 *
 * - client: ALTS handshaker client instance.
 * - event: ALTS TSI event instance.
 *
 * It returns TSI_OK on success and an error status code on failure.
 */
tsi_result alts_handshaker_client_start_client(alts_handshaker_client* client,
                                               alts_tsi_event* event);

/**
 * This method schedules a server_start handshaker request to ALTS handshaker
 * service.
 *
 * - client: ALTS handshaker client instance.
 * - event: ALTS TSI event instance.
 * - bytes_received: bytes in out_frames returned from the peer's handshaker
 *   response.
 *
 * It returns TSI_OK on success and an error status code on failure.
 */
tsi_result alts_handshaker_client_start_server(alts_handshaker_client* client,
                                               alts_tsi_event* event,
                                               grpc_slice* bytes_received);

/**
 * This method schedules a next handshaker request to ALTS handshaker service.
 *
 * - client: ALTS handshaker client instance.
 * - event: ALTS TSI event instance.
 * - bytes_received: bytes in out_frames returned from the peer's handshaker
 *   response.
 *
 * It returns TSI_OK on success and an error status code on failure.
 */
tsi_result alts_handshaker_client_next(alts_handshaker_client* client,
                                       alts_tsi_event* event,
                                       grpc_slice* bytes_received);

/**
 * This method cancels previously scheduled, but yet executed handshaker
 * requests to ALTS handshaker service. After this operation, the handshake
 * will be shutdown, and no more handshaker requests will get scheduled.
 *
 * - client: ALTS handshaker client instance.
 */
void alts_handshaker_client_shutdown(alts_handshaker_client* client);

/**
 * This method destroys a ALTS handshaker client.
 *
 * - client: a ALTS handshaker client instance.
 */
void alts_handshaker_client_destroy(alts_handshaker_client* client);

/**
 * This method creates a ALTS handshaker client.
 *
 * - channel: grpc channel to ALTS handshaker service.
 * - queue: grpc completion queue.
 * - handshaker_service_url: address of ALTS handshaker service in the format of
 *   "host:port".
 *
 * It returns the created ALTS handshaker client on success, and NULL on
 * failure.
 */
alts_handshaker_client* alts_grpc_handshaker_client_create(
    grpc_channel* channel, grpc_completion_queue* queue,
    const char* handshaker_service_url);

namespace grpc_core {
namespace internal {

/**
 * Unsafe, use for testing only. It allows the caller to change the way that
 * GRPC calls are made to the handshaker service.
 */
void alts_handshaker_client_set_grpc_caller_for_testing(
    alts_handshaker_client* client, alts_grpc_caller caller);

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_CLIENT_H */
