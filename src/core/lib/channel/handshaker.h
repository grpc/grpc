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

#ifndef GRPC_CORE_LIB_CHANNEL_HANDSHAKER_H
#define GRPC_CORE_LIB_CHANNEL_HANDSHAKER_H

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/tcp_server.h"

/// Handshakers are used to perform initial handshakes on a connection
/// before the client sends the initial request.  Some examples of what
/// a handshaker can be used for includes support for HTTP CONNECT on
/// the client side and various types of security initialization.
///
/// In general, handshakers should be used via a handshake manager.

///
/// grpc_handshaker
///

typedef struct grpc_handshaker grpc_handshaker;

/// Arguments passed through handshakers and to the on_handshake_done callback.
///
/// For handshakers, all members are input/output parameters; for
/// example, a handshaker may read from or write to \a endpoint and
/// then later replace it with a wrapped endpoint.  Similarly, a
/// handshaker may modify \a args.
///
/// A handshaker takes ownership of the members while a handshake is in
/// progress.  Upon failure or shutdown of an in-progress handshaker,
/// the handshaker is responsible for destroying the members and setting
/// them to NULL before invoking the on_handshake_done callback.
///
/// For the on_handshake_done callback, all members are input arguments,
/// which the callback takes ownership of.
typedef struct {
  grpc_endpoint* endpoint;
  grpc_channel_args* args;
  grpc_slice_buffer* read_buffer;
  // A handshaker may set this to true before invoking on_handshake_done
  // to indicate that subsequent handshakers should be skipped.
  bool exit_early;
  // User data passed through the handshake manager.  Not used by
  // individual handshakers.
  void* user_data;
} grpc_handshaker_args;

typedef struct {
  /// Destroys the handshaker.
  void (*destroy)(grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker);

  /// Shuts down the handshaker (e.g., to clean up when the operation is
  /// aborted in the middle).
  void (*shutdown)(grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker);

  /// Performs handshaking, modifying \a args as needed (e.g., to
  /// replace \a endpoint with a wrapped endpoint).
  /// When finished, invokes \a on_handshake_done.
  /// \a acceptor will be NULL for client-side handshakers.
  void (*do_handshake)(grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker,
                       grpc_tcp_server_acceptor* acceptor,
                       grpc_closure* on_handshake_done,
                       grpc_handshaker_args* args);
} grpc_handshaker_vtable;

/// Base struct.  To subclass, make this the first member of the
/// implementation struct.
struct grpc_handshaker {
  const grpc_handshaker_vtable* vtable;
};

/// Called by concrete implementations to initialize the base struct.
void grpc_handshaker_init(const grpc_handshaker_vtable* vtable,
                          grpc_handshaker* handshaker);

void grpc_handshaker_destroy(grpc_exec_ctx* exec_ctx,
                             grpc_handshaker* handshaker);
void grpc_handshaker_shutdown(grpc_exec_ctx* exec_ctx,
                              grpc_handshaker* handshaker);
void grpc_handshaker_do_handshake(grpc_exec_ctx* exec_ctx,
                                  grpc_handshaker* handshaker,
                                  grpc_tcp_server_acceptor* acceptor,
                                  grpc_closure* on_handshake_done,
                                  grpc_handshaker_args* args);

///
/// grpc_handshake_manager
///

typedef struct grpc_handshake_manager grpc_handshake_manager;

/// Creates a new handshake manager.  Caller takes ownership.
grpc_handshake_manager* grpc_handshake_manager_create();

/// Adds a handshaker to the handshake manager.
/// Takes ownership of \a handshaker.
void grpc_handshake_manager_add(grpc_handshake_manager* mgr,
                                grpc_handshaker* handshaker);

/// Destroys the handshake manager.
void grpc_handshake_manager_destroy(grpc_exec_ctx* exec_ctx,
                                    grpc_handshake_manager* mgr);

/// Shuts down the handshake manager (e.g., to clean up when the operation is
/// aborted in the middle).
/// The caller must still call grpc_handshake_manager_destroy() after
/// calling this function.
void grpc_handshake_manager_shutdown(grpc_exec_ctx* exec_ctx,
                                     grpc_handshake_manager* mgr);

/// Invokes handshakers in the order they were added.
/// Takes ownership of \a endpoint, and then passes that ownership to
/// the \a on_handshake_done callback.
/// Does NOT take ownership of \a channel_args.  Instead, makes a copy before
/// invoking the first handshaker.
/// \a acceptor will be NULL for client-side handshakers.
///
/// When done, invokes \a on_handshake_done with a grpc_handshaker_args
/// object as its argument.  If the callback is invoked with error !=
/// GRPC_ERROR_NONE, then handshaking failed and the handshaker has done
/// the necessary clean-up.  Otherwise, the callback takes ownership of
/// the arguments.
void grpc_handshake_manager_do_handshake(
    grpc_exec_ctx* exec_ctx, grpc_handshake_manager* mgr,
    grpc_endpoint* endpoint, const grpc_channel_args* channel_args,
    gpr_timespec deadline, grpc_tcp_server_acceptor* acceptor,
    grpc_iomgr_cb_func on_handshake_done, void* user_data);

#endif /* GRPC_CORE_LIB_CHANNEL_HANDSHAKER_H */
