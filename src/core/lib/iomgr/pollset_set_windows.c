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

#include <stdint.h>
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include "src/core/lib/iomgr/pollset_set_windows.h"

grpc_pollset_set* grpc_pollset_set_create(void) {
  return (grpc_pollset_set*)((intptr_t)0xdeafbeef);
}

void grpc_pollset_set_destroy(grpc_exec_ctx* exec_ctx,
                              grpc_pollset_set* pollset_set) {}

void grpc_pollset_set_add_pollset(grpc_exec_ctx* exec_ctx,
                                  grpc_pollset_set* pollset_set,
                                  grpc_pollset* pollset) {}

void grpc_pollset_set_del_pollset(grpc_exec_ctx* exec_ctx,
                                  grpc_pollset_set* pollset_set,
                                  grpc_pollset* pollset) {}

void grpc_pollset_set_add_pollset_set(grpc_exec_ctx* exec_ctx,
                                      grpc_pollset_set* bag,
                                      grpc_pollset_set* item) {}

void grpc_pollset_set_del_pollset_set(grpc_exec_ctx* exec_ctx,
                                      grpc_pollset_set* bag,
                                      grpc_pollset_set* item) {}

#endif /* GRPC_WINSOCK_SOCKET */
