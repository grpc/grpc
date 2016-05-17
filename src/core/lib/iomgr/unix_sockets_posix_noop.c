/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/iomgr/unix_sockets_posix.h"

#ifndef GPR_HAVE_UNIX_SOCKET

#include <grpc/support/log.h>

void grpc_create_socketpair_if_unix(int sv[2]) {
  // TODO: Either implement this for the non-Unix socket case or make
  // sure that it is never called in any such case. Until then, leave an
  // assertion to notify if this gets called inadvertently
  GPR_ASSERT(0);
}

grpc_error *grpc_resolve_unix_domain_address(
    const char *name, grpc_resolved_addresses **addresses) {
  *addresses = NULL;
  return GRPC_ERROR_CREATE("Unix domain sockets are not supported on Windows");
}

int grpc_is_unix_socket(const struct sockaddr *addr) { return false; }

void grpc_unlink_if_unix_domain_socket(const struct sockaddr *addr) {}

char *grpc_sockaddr_to_uri_unix_if_possible(const struct sockaddr *addr) {
  return NULL;
}

#endif
