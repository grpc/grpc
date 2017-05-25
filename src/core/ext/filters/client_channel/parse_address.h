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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_PARSE_ADDRESS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_PARSE_ADDRESS_H

#include <stddef.h>

#include "src/core/ext/filters/client_channel/uri_parser.h"
#include "src/core/lib/iomgr/resolve_address.h"

/** Populate \a resolved_addr from \a uri, whose path is expected to contain a
 * unix socket path. Returns true upon success. */
bool grpc_parse_unix(const grpc_uri *uri, grpc_resolved_address *resolved_addr);

/** Populate \a resolved_addr from \a uri, whose path is expected to contain an
 * IPv4 host:port pair. Returns true upon success. */
bool grpc_parse_ipv4(const grpc_uri *uri, grpc_resolved_address *resolved_addr);

/** Populate \a resolved_addr from \a uri, whose path is expected to contain an
 * IPv6 host:port pair. Returns true upon success. */
bool grpc_parse_ipv6(const grpc_uri *uri, grpc_resolved_address *resolved_addr);

/** Populate \a resolved_addr from \a uri. Returns true upon success. */
bool grpc_parse_uri(const grpc_uri *uri, grpc_resolved_address *resolved_addr);

/** Parse bare IPv4 or IPv6 "IP:port" strings. */
bool grpc_parse_ipv4_hostport(const char *hostport, grpc_resolved_address *addr,
                              bool log_errors);
bool grpc_parse_ipv6_hostport(const char *hostport, grpc_resolved_address *addr,
                              bool log_errors);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_PARSE_ADDRESS_H */
