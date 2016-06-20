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

#include <grpc/support/port_platform.h>
#include "src/core/lib/iomgr/socket_utils_posix.h"

#include <errno.h>
#include <string.h>

#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

int main(int argc, char **argv) {
  int sock;
  grpc_test_init(argc, argv);

  sock = socket(PF_INET, SOCK_STREAM, 0);
  GPR_ASSERT(sock > 0);

  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_nonblocking",
                               grpc_set_socket_nonblocking(sock, 1)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_nonblocking",
                               grpc_set_socket_nonblocking(sock, 0)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_cloexec",
                               grpc_set_socket_cloexec(sock, 1)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_cloexec",
                               grpc_set_socket_cloexec(sock, 0)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_reuse_addr",
                               grpc_set_socket_reuse_addr(sock, 1)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_reuse_addr",
                               grpc_set_socket_reuse_addr(sock, 0)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_low_latency",
                               grpc_set_socket_low_latency(sock, 1)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_low_latency",
                               grpc_set_socket_low_latency(sock, 0)));

  close(sock);

  return 0;
}
