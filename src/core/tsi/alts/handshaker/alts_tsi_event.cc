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

#include "src/core/tsi/alts/handshaker/alts_tsi_event.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/slice/slice_internal.h"

tsi_result alts_tsi_event_create(alts_tsi_handshaker* handshaker,
                                 tsi_handshaker_on_next_done_cb cb,
                                 void* user_data,
                                 grpc_alts_credentials_options* options,
                                 grpc_slice target_name,
                                 alts_tsi_event** event) {
  if (event == nullptr || handshaker == nullptr || cb == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to alts_tsi_event_create()");
    return TSI_INVALID_ARGUMENT;
  }
  alts_tsi_event* e = static_cast<alts_tsi_event*>(gpr_zalloc(sizeof(*e)));
  e->handshaker = handshaker;
  e->cb = cb;
  e->user_data = user_data;
  e->options = grpc_alts_credentials_options_copy(options);
  e->target_name = grpc_slice_copy(target_name);
  grpc_metadata_array_init(&e->initial_metadata);
  grpc_metadata_array_init(&e->trailing_metadata);
  *event = e;
  return TSI_OK;
}

void alts_tsi_event_dispatch_to_handshaker(alts_tsi_event* event, bool is_ok) {
  if (event == nullptr) {
    gpr_log(
        GPR_ERROR,
        "ALTS TSI event is nullptr in alts_tsi_event_dispatch_to_handshaker()");
    return;
  }
  alts_tsi_handshaker_handle_response(event->handshaker, event->recv_buffer,
                                      event->status, &event->details, event->cb,
                                      event->user_data, is_ok);
}

void alts_tsi_event_destroy(alts_tsi_event* event) {
  if (event == nullptr) {
    return;
  }
  grpc_byte_buffer_destroy(event->send_buffer);
  grpc_byte_buffer_destroy(event->recv_buffer);
  grpc_metadata_array_destroy(&event->initial_metadata);
  grpc_metadata_array_destroy(&event->trailing_metadata);
  grpc_slice_unref_internal(event->details);
  grpc_slice_unref_internal(event->target_name);
  grpc_alts_credentials_options_destroy(event->options);
  gpr_free(event);
}
