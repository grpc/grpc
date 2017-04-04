/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_CORE_TSI_FAKE_TRANSPORT_SECURITY_H
#define GRPC_CORE_TSI_FAKE_TRANSPORT_SECURITY_H

#include "src/core/tsi/transport_security_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Value for the TSI_CERTIFICATE_TYPE_PEER_PROPERTY property for FAKE certs. */
#define TSI_FAKE_CERTIFICATE_TYPE "FAKE"

/* Creates a fake handshaker that will create a fake frame protector.

   No cryptography is performed in these objects. They just simulate handshake
   messages going back and forth for the handshaker and do some framing on
   cleartext data for the protector.  */
tsi_handshaker *tsi_create_fake_handshaker(int is_client);

/* Creates a protector directly without going through the handshake phase. */
tsi_frame_protector *tsi_create_fake_protector(
    size_t *max_protected_frame_size);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_TSI_FAKE_TRANSPORT_SECURITY_H */
