/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPRPP_HOST_PORT_H
#define GRPC_CORE_LIB_GPRPP_HOST_PORT_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/string_view.h"

/** Given a host and port, creates a newly-allocated string of the form
   "host:port" or "[ho:st]:port", depending on whether the host contains colons
   like an IPv6 literal.  If the host is already bracketed, then additional
   brackets will not be added.

   Usage is similar to gpr_asprintf: returns the number of bytes written
   (excluding the final '\0'), and *out points to a string which must later be
   destroyed using gpr_free().

   In the unlikely event of an error, returns -1 and sets *out to NULL. */
int gpr_join_host_port(char** out, const char* host, int port);

/** Given a name in the form "host:port" or "[ho:st]:port", split into hostname
   and port number.

   There are two variants of this method:
   1) StringView ouptut: port and host are returned as views on name.
   2) char* output: port and host are copied into newly allocated strings,
                    which must later be destroyed using gpr_free().

   Prefer variant (1) over (2), because no allocation or copy is performed in
   variant (1).  Use (2) only when interacting with C API that mandate
   null-terminated strings.

   Return true on success, false on failure. Guarantees *host and *port are
   cleared on failure. */
bool gpr_split_host_port(grpc_core::StringView name,
                         grpc_core::StringView* host,
                         grpc_core::StringView* port);
bool gpr_split_host_port(grpc_core::StringView name, char** host, char** port);

#endif /* GRPC_CORE_LIB_GPRPP_HOST_PORT_H */
