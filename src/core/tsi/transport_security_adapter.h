/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_TSI_TRANSPORT_SECURITY_ADAPTER_H
#define GRPC_CORE_TSI_TRANSPORT_SECURITY_ADAPTER_H

#include "src/core/tsi/transport_security_interface.h"

/* Create a tsi handshaker that takes an implementation of old interface and
   converts into an implementation of new interface. In the old interface,
   there are get_bytes_to_send_to_peer, process_bytes_from_peer, get_result,
   extract_peer, and create_frame_protector. In the new interface, only next
   method is needed. See transport_security_interface.h for details. Note that
   this tsi adapter handshaker is temporary. It will be removed once TSI has
   been fully migrated to the new interface.
   Ownership of input tsi_handshaker is transferred to this new adapter.  */
tsi_handshaker* tsi_create_adapter_handshaker(tsi_handshaker* wrapped);

/* Given a tsi adapter handshaker, return the original wrapped handshaker. The
   adapter still owns the wrapped handshaker which should not be destroyed by
   the caller. */
tsi_handshaker* tsi_adapter_handshaker_get_wrapped(tsi_handshaker* adapter);

#endif /* GRPC_CORE_TSI_TRANSPORT_SECURITY_ADAPTER_H */
