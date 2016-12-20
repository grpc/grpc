/*
 *
 * Copyright 2015-2016, Google Inc.
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

/* RPC-internal Census API's. These are designed to be generic enough that
 * they can (ultimately) be used in many different RPC systems (with differing
 * implementations). */

#ifndef GRPC_CLIENT_CHANNEL_PUBLIC_H
#define GRPC_CLIENT_CHANNEL_PUBLIC_H

#include <grpc/grpc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the number of bytes written
 * (excluding the final '\0'), and *joined_host_port points to a string which must later
 * be destroyed using gpr_free().
 * Return values and ownership semantics are meant to mimick gpr_split_host_port, see
 * https://github.com/grpc/grpc/blob/master/include/grpc/support/host_port.h#L53 */
int grpc_generic_join_host_port(char **joined_host_port, const char *host, const char *port);

/* Split *joined_host_port into hostname and port number, into newly allocated strings, which must later be
 * destroyed using gpr_free().
 * Return 1 on success, 0 on failure. Guarantees *host and *port == NULL on
 * failure.
 * Return values and ownership semantics are meant to mimick gpr_join_host_port, see
 * https://github.com/grpc/grpc/blob/master/include/grpc/support/host_port.h#L60*/
int grpc_generic_split_host_port(const char *joined_host_port, char **host, char **port);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CENSUS_H */
