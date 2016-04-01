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

#ifndef GRPC_CORE_LIB_SURFACE_CALL_TEST_ONLY_H
#define GRPC_CORE_LIB_SURFACE_CALL_TEST_ONLY_H

#include <grpc/grpc.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Return the compression algorithm from \a call.
 *
 * \warning This function should \b only be used in test code. */
grpc_compression_algorithm grpc_call_test_only_get_compression_algorithm(
    grpc_call *call);

/** Return the message flags from \a call.
 *
 * \warning This function should \b only be used in test code. */
uint32_t grpc_call_test_only_get_message_flags(grpc_call *call);

/** Returns a bitset for the encodings (compression algorithms) supported by \a
 * call's peer.
 *
 * To be indexed by grpc_compression_algorithm enum values. */
uint32_t grpc_call_test_only_get_encodings_accepted_by_peer(grpc_call *call);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SURFACE_CALL_TEST_ONLY_H */
