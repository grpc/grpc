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

#ifndef GRPC_CORE_EXT_CLIENT_CONFIG_PARSE_ADDRESS_H
#define GRPC_CORE_EXT_CLIENT_CONFIG_PARSE_ADDRESS_H

#include <stddef.h>

#include "src/core/ext/client_config/uri_parser.h"
#include "src/core/lib/iomgr/sockaddr.h"

#ifdef GPR_HAVE_UNIX_SOCKET
/** Populate \a addr and \a len from \a uri, whose path is expected to contain a
 * unix socket path. Returns true upon success. */
int parse_unix(grpc_uri *uri, struct sockaddr_storage *addr, size_t *len);
#endif

/** Populate /a addr and \a len from \a uri, whose path is expected to contain a
 * host:port pair. Returns true upon success. */
int parse_ipv4(grpc_uri *uri, struct sockaddr_storage *addr, size_t *len);

/** Populate /a addr and \a len from \a uri, whose path is expected to contain a
 * host:port pair. Returns true upon success. */
int parse_ipv6(grpc_uri *uri, struct sockaddr_storage *addr, size_t *len);

#endif /* GRPC_CORE_EXT_CLIENT_CONFIG_PARSE_ADDRESS_H */
