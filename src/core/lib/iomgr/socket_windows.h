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

#ifndef GRPC_CORE_LIB_IOMGR_SOCKET_WINDOWS_H
#define GRPC_CORE_LIB_IOMGR_SOCKET_WINDOWS_H

#include <grpc/support/port_platform.h>
#include <winsock2.h>

#include <grpc/support/atm.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_internal.h"

/* This holds the data for an outstanding read or write on a socket.
   The mutex to protect the concurrent access to that data is the one
   inside the winsocket wrapper. */
typedef struct grpc_winsocket_callback_info {
  /* This is supposed to be a WSAOVERLAPPED, but in order to get that
     definition, we need to include ws2tcpip.h, which needs to be included
     from the top, otherwise it'll clash with a previous inclusion of
     windows.h that in turns includes winsock.h. If anyone knows a way
     to do it properly, feel free to send a patch. */
  OVERLAPPED overlapped;
  /* The callback information for the pending operation. May be empty if the
     caller hasn't registered a callback yet. */
  grpc_closure *closure;
  /* A boolean to describe if the IO Completion Port got a notification for
     that operation. This will happen if the operation completed before the
     called had time to register a callback. We could avoid that behavior
     altogether by forcing the caller to always register its callback before
     proceeding queue an operation, but it is frequent for an IO Completion
     Port to trigger quickly. This way we avoid a context switch for calling
     the callback. We also simplify the read / write operations to avoid having
     to hold a mutex for a long amount of time. */
  int has_pending_iocp;
  /* The results of the overlapped operation. */
  DWORD bytes_transfered;
  int wsa_error;
} grpc_winsocket_callback_info;

/* This is a wrapper to a Windows socket. A socket can have one outstanding
   read, and one outstanding write. Doing an asynchronous accept means waiting
   for a read operation. Doing an asynchronous connect means waiting for a
   write operation. These are completely arbitrary ties between the operation
   and the kind of event, because we can have one overlapped per pending
   operation, whichever its nature is. So we could have more dedicated pending
   operation callbacks for connect and listen. But given the scope of listen
   and accept, we don't need to go to that extent and waste memory. Also, this
   is closer to what happens in posix world. */
typedef struct grpc_winsocket {
  SOCKET socket;
  bool destroy_called;

  grpc_winsocket_callback_info write_info;
  grpc_winsocket_callback_info read_info;

  gpr_mu state_mu;
  bool shutdown_called;

  /* You can't add the same socket twice to the same IO Completion Port.
     This prevents that. */
  int added_to_iocp;

  grpc_closure shutdown_closure;

  /* A label for iomgr to track outstanding objects */
  grpc_iomgr_object iomgr_object;
} grpc_winsocket;

/* Create a wrapped windows handle. This takes ownership of it, meaning that
   it will be responsible for closing it. */
grpc_winsocket *grpc_winsocket_create(SOCKET socket, const char *name);

/* Initiate an asynchronous shutdown of the socket. Will call off any pending
   operation to cancel them. */
void grpc_winsocket_shutdown(grpc_winsocket *socket);

/* Destroy a socket. Should only be called if there's no pending operation. */
void grpc_winsocket_destroy(grpc_winsocket *socket);

void grpc_socket_notify_on_write(grpc_exec_ctx *exec_ctx,
                                 grpc_winsocket *winsocket,
                                 grpc_closure *closure);

void grpc_socket_notify_on_read(grpc_exec_ctx *exec_ctx,
                                grpc_winsocket *winsocket,
                                grpc_closure *closure);

void grpc_socket_become_ready(grpc_exec_ctx *exec_ctx,
                              grpc_winsocket *winsocket,
                              grpc_winsocket_callback_info *ci);

#endif /* GRPC_CORE_LIB_IOMGR_SOCKET_WINDOWS_H */
