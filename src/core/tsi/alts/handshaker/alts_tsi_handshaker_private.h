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

#ifndef GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_TSI_HANDSHAKER_PRIVATE_H
#define GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_TSI_HANDSHAKER_PRIVATE_H

#include <grpc/support/port_platform.h>

#include "src/core/tsi/alts/handshaker/alts_handshaker_client.h"

namespace grpc_core {
namespace internal {

/**
 * Unsafe, use for testing only. It allows the caller to change the way the
 * ALTS TSI handshaker schedules handshaker requests.
 */
void alts_tsi_handshaker_set_client_for_testing(alts_tsi_handshaker* handshaker,
                                                alts_handshaker_client* client);

alts_handshaker_client* alts_tsi_handshaker_get_client_for_testing(
    alts_tsi_handshaker* handshaker);

/* For testing only. */
bool alts_tsi_handshaker_get_has_sent_start_message_for_testing(
    alts_tsi_handshaker* handshaker);

bool alts_tsi_handshaker_get_is_client_for_testing(
    alts_tsi_handshaker* handshaker);

void alts_tsi_handshaker_set_recv_bytes_for_testing(
    alts_tsi_handshaker* handshaker, grpc_slice* slice);

grpc_slice alts_tsi_handshaker_get_recv_bytes_for_testing(
    alts_tsi_handshaker* handshaker);

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_TSI_ALTS_HANDSHAKER_ALTS_TSI_HANDSHAKER_PRIVATE_H */
