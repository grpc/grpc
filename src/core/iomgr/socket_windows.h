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

#ifndef GRPC_INTERNAL_CORE_IOMGR_SOCKET_WINDOWS_H
#define GRPC_INTERNAL_CORE_IOMGR_SOCKET_WINDOWS_H

#include <windows.h>

#include <grpc/support/sync.h>
#include <grpc/support/atm.h>

typedef struct grpc_winsocket_callback_info {
  /* This is supposed to be a WSAOVERLAPPED, but in order to get that
   * definition, we need to include ws2tcpip.h, which needs to be included
   * from the top, otherwise it'll clash with a previous inclusion of
   * windows.h that in turns includes winsock.h. If anyone knows a way
   * to do it properly, feel free to send a patch.
   */
  OVERLAPPED overlapped;
  void(*cb)(void *opaque, int success);
  void *opaque;
  int has_pending_iocp;
  DWORD bytes_transfered;
  int wsa_error;
} grpc_winsocket_callback_info;

typedef struct grpc_winsocket {
  SOCKET socket;

  int added_to_iocp;

  grpc_winsocket_callback_info write_info;
  grpc_winsocket_callback_info read_info;

  gpr_mu state_mu;
} grpc_winsocket;

/* Create a wrapped windows handle.
This takes ownership of closing it. */
grpc_winsocket *grpc_winsocket_create(SOCKET socket);

void grpc_winsocket_shutdown(grpc_winsocket *socket);
void grpc_winsocket_orphan(grpc_winsocket *socket);

#endif  /* GRPC_INTERNAL_CORE_IOMGR_SOCKET_WINDOWS_H */
