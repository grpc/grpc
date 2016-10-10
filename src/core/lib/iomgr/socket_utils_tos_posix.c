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

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SOCKET

#include "src/core/lib/iomgr/socket_utils_posix.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

grpc_error* grpc_set_socket_tos(int fd, grpc_arg* arg) {
  int newval;
  socklen_t intlen = sizeof(newval);

  GPR_ASSERT(0 == strcmp(arg->key, GRPC_ARG_TOS));
  GPR_ASSERT(arg->type == GRPC_ARG_INTEGER);

  if (0 != setsockopt(fd, IPPROTO_IP, IP_TOS, &arg->value.integer,
                      sizeof(arg->value.integer))) {
    return GRPC_OS_ERROR(errno, "setsockopt(IP_TOS)");
  }
  if (0 != getsockopt(fd, IPPROTO_IP, IP_TOS, &newval, &intlen)) {
    return GRPC_OS_ERROR(errno, "getsockopt(IP_TOS)");
  }
  if (newval != arg->value.integer) {
    return GRPC_ERROR_CREATE("Failed to set IP_TOS");
  }

  return GRPC_ERROR_NONE;
}

#endif
