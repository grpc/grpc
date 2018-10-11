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

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>

#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/transport_security_interface.h"

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/pollset_set.h"

#define ALTS_SERVICE_METHOD "/grpc.gcp.HandshakerService/DoHandshake"
#define ALTS_APPLICATION_PROTOCOL "grpc"
#define ALTS_RECORD_PROTOCOL "ALTSRP_GCM_AES128_REKEY"

const size_t kAltsAes128GcmRekeyKeyLength = 44;

typedef struct alts_tsi_handshaker alts_tsi_handshaker;
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
  tsi_result (*client_start)(alts_handshaker_client* client);
  tsi_result (*server_start)(alts_handshaker_client* client,
                             grpc_slice* bytes_received);
  tsi_result (*next)(alts_handshaker_client* client,
                     grpc_slice* bytes_received);
  void (*shutdown)(alts_handshaker_client* client);
  void (*destruct)(alts_handshaker_client* client);
} alts_handshaker_client_vtable;

/**
 * This method schedules a client_start handshaker request to ALTS handshaker
 * service.
 *
 * - client: ALTS handshaker client instance.
 *
 * It returns TSI_OK on success and an error status code on failure.
 */
tsi_result alts_handshaker_client_start_client(alts_handshaker_client* client);

/**
 * This method schedules a server_start handshaker request to ALTS handshaker
 * service.
 *
 * - client: ALTS handshaker client instance.
 * - bytes_received: bytes in out_frames returned from the peer's handshaker
 *   response.
 *
 * It returns TSI_OK on success and an error status code on failure.
 */
tsi_result alts_handshaker_client_start_server(alts_handshaker_client* client,
                                               grpc_slice* bytes_received);

/**
 * This method schedules a next handshaker request to ALTS handshaker service.
 *
 * - client: ALTS handshaker client instance.
 * - bytes_received: bytes in out_frames returned from the peer's handshaker
 *   response.
 *
 * It returns TSI_OK on success and an error status code on failure.
 */
tsi_result alts_handshaker_client_next(alts_handshaker_client* client,
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
 * This method destroys an ALTS handshaker client.
 *
 * - client: an ALTS handshaker client instance.
 */
void alts_handshaker_client_destroy(alts_handshaker_client* client);

/**
 * This method creates an ALTS handshaker client.
 *
 * - channel: grpc channel to ALTS handshaker service.
 * - handshaker_service_url: address of ALTS handshaker service in the format of
 *   "host:port".
 * - interested_parties: set of pollsets interested in this connection.
 * - cb: gRPC provided callbacks passed from TSI handshaker.
 * - is_client: a boolean value indicating if the created handshaker client is
 * used at the client side or not. It returns the created ALTS handshaker client
 * on success, and NULL on failure.
 */
alts_handshaker_client* alts_grpc_handshaker_client_create(
    grpc_channel* channel, const char* handshaker_service_url,
    grpc_pollset_set* interested_parties, grpc_iomgr_cb_func cb,
    bool is_client);

/**
 * This method returns a boolean value indicating whether or not an ALTS
 * handshaker client has been initialized.
 *
 * - client: an ALTS handshaker client instance.
 */
bool alts_handshaker_client_is_initialized(alts_handshaker_client* client);

/**
 * This method initializes an ALTS handshaker client.
 *
 * - client: an ALTS handshaker client instance.
 * - handshaker: an ALTS TSI handshaker instance. The onwership is not
 *   transferred.
 * - cb: callback function to be called when handling data received from ALTS
 *   handshaker service.
 * - user_data: argument to callback function.
 * - options: ALTS credentials options.
 * - target_name: name of endpoint used for secure naming check.
 */
void alts_handshaker_client_init(alts_handshaker_client* client,
                                 alts_tsi_handshaker* handshaker,
                                 tsi_handshaker_on_next_done_cb cb,
                                 void* user_data,
                                 grpc_alts_credentials_options* options,
                                 grpc_slice target_name);

/**
 * This method destroys send and recv buffers of an ALTS handshaker client.
 *
 * - client: an ALTS handshaker client instance.
 */
void alts_handshaker_client_buffer_destroy(alts_handshaker_client* client);

/**
 * This method handles handshaker response returned from ALTS handshaker
 * service.
 *
 * - client: an ALTS handshaker client instance.
 * - is_ok: a boolean value indicating if the handshaker response is ok to read.
 */
void alts_handshaker_client_handle_response(alts_handshaker_client* client,
                                            bool is_ok);

#endif /* GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_CLIENT_H */
