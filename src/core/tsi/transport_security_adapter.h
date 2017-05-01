/*
 *
 * Copyright 2017, Google Inc.
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

#ifndef GRPC_CORE_TSI_TRANSPORT_SECURITY_ADAPTER_H
#define GRPC_CORE_TSI_TRANSPORT_SECURITY_ADAPTER_H

#include "src/core/tsi/transport_security_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create a tsi handshaker that takes an implementation of old interface and
   converts into an implementation of new interface. In the old interface,
   there are get_bytes_to_send_to_peer, process_bytes_from_peer, get_result,
   extract_peer, and create_frame_protector. In the new interface, only next
   method is needed. See transport_security_interface.h for details. Note that
   this tsi adapter handshaker is temporary. It will be removed once TSI has
   been fully migrated to the new interface.
   Ownership of input tsi_handshaker is transferred to this new adapter.  */
tsi_handshaker *tsi_create_adapter_handshaker(tsi_handshaker *wrapped);

/* Given a tsi adapter handshaker, return the original wrapped handshaker. The
   adapter still owns the wrapped handshaker which should not be destroyed by
   the caller. */
tsi_handshaker *tsi_adapter_handshaker_get_wrapped(tsi_handshaker *adapter);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_TSI_TRANSPORT_SECURITY_ADAPTER_H */
