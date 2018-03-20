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

#ifndef GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_TSI_EVENT_H
#define GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_TSI_EVENT_H

#include <grpc/support/port_platform.h>

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>

#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/transport_security_interface.h"

/**
 * A ALTS TSI event interface. In asynchronous implementation of
 * tsi_handshaker_next(), the function will exit after scheduling a handshaker
 * request to ALTS handshaker service without waiting for response to return.
 * The event is used to link the scheduled handshaker request with the
 * corresponding response so that enough context information can be inferred
 * from it to handle the response. All APIs in the header are thread-compatible.
 */

/**
 * Main struct for ALTS TSI event. It retains ownership on send_buffer and
 * recv_buffer, but not on handshaker.
 */
typedef struct alts_tsi_event {
  alts_tsi_handshaker* handshaker;
  grpc_byte_buffer* send_buffer;
  grpc_byte_buffer* recv_buffer;
  grpc_status_code status;
  grpc_slice details;
  grpc_metadata_array initial_metadata;
  grpc_metadata_array trailing_metadata;
  tsi_handshaker_on_next_done_cb cb;
  void* user_data;
  grpc_alts_credentials_options* options;
  grpc_slice target_name;
} alts_tsi_event;

/**
 * This method creates a ALTS TSI event.
 *
 * - handshaker: ALTS TSI handshaker instance associated with the event to be
 *   created. The created event does not own the handshaker instance.
 * - cb: callback function to be called when handling data received from ALTS
 *   handshaker service.
 * - user_data: argument to callback function.
 * - options: ALTS credentials options.
 * - target_name: name of endpoint used for secure naming check.
 * - event: address of ALTS TSI event instance to be returned from the method.
 *
 * It returns TSI_OK on success and an error status code on failure.
 */
tsi_result alts_tsi_event_create(alts_tsi_handshaker* handshaker,
                                 tsi_handshaker_on_next_done_cb cb,
                                 void* user_data,
                                 grpc_alts_credentials_options* options,
                                 grpc_slice target_name,
                                 alts_tsi_event** event);

/**
 * This method dispatches a ALTS TSI event received from the handshaker service,
 * and a boolean flag indicating if the event is valid to read to ALTS TSI
 * handshaker to process. It is called by TSI thread.
 *
 * - event: ALTS TSI event instance.
 * - is_ok: a boolean value indicating if the event is valid to read.
 */
void alts_tsi_event_dispatch_to_handshaker(alts_tsi_event* event, bool is_ok);

/**
 * This method destroys the ALTS TSI event.
 */
void alts_tsi_event_destroy(alts_tsi_event* event);

#endif /* GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_TSI_EVENT_H */
