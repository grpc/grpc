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

#ifndef GRPC_SUPPORT_HOST_PORT_H
#define GRPC_SUPPORT_HOST_PORT_H

#include <grpc/support/port_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Given a host and port, creates a newly-allocated string of the form
   "host:port" or "[ho:st]:port", depending on whether the host contains colons
   like an IPv6 literal.  If the host is already bracketed, then additional
   brackets will not be added.

   Usage is similar to gpr_asprintf: returns the number of bytes written
   (excluding the final '\0'), and *out points to a string which must later be
   destroyed using gpr_free().

   In the unlikely event of an error, returns -1 and sets *out to NULL. */
GPRAPI int gpr_join_host_port(char **out, const char *host, int port);

/** Given a name in the form "host:port" or "[ho:st]:port", split into hostname
   and port number, into newly allocated strings, which must later be
   destroyed using gpr_free().
   Return 1 on success, 0 on failure. Guarantees *host and *port == NULL on
   failure. */
GPRAPI int gpr_split_host_port(const char *name, char **host, char **port);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_SUPPORT_HOST_PORT_H */
