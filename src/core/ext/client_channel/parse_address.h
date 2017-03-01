/*
 *
 * Copyright 2015, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_CLIENT_CHANNEL_PARSE_ADDRESS_H
#define GRPC_CORE_EXT_CLIENT_CHANNEL_PARSE_ADDRESS_H

#include <stddef.h>

#include "src/core/ext/client_channel/uri_parser.h"
#include "src/core/lib/iomgr/resolve_address.h"

/** Populate \a addr and \a len from \a uri, whose path is expected to contain a
 * unix socket path. Returns true upon success. */
int parse_unix(grpc_uri *uri, grpc_resolved_address *resolved_addr);

/** Populate /a addr and \a len from \a uri, whose path is expected to contain a
 * host:port pair. Returns true upon success. */
int parse_ipv4(grpc_uri *uri, grpc_resolved_address *resolved_addr);

/** Populate /a addr and \a len from \a uri, whose path is expected to contain a
 * host:port pair. Returns true upon success. */
int parse_ipv6(grpc_uri *uri, grpc_resolved_address *resolved_addr);

#endif /* GRPC_CORE_EXT_CLIENT_CHANNEL_PARSE_ADDRESS_H */
