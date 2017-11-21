/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_CHANNEL_HANDSHAKER_H
#define GRPC_CORE_LIB_CHANNEL_HANDSHAKER_H

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/tcp_server.h"

#ifdef __cplusplus
extern "C" {
#endif

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
  void (*shutdown)(grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker,
                   grpc_error* why);

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
                              grpc_handshaker* handshaker, grpc_error* why);
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
                                     grpc_handshake_manager* mgr,
                                     grpc_error* why);

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
    grpc_millis deadline, grpc_tcp_server_acceptor* acceptor,
    grpc_iomgr_cb_func on_handshake_done, void* user_data);

/// Add \a mgr to the server side list of all pending handshake managers, the
/// list starts with \a *head.
// Not thread-safe. Caller needs to synchronize.
void grpc_handshake_manager_pending_list_add(grpc_handshake_manager** head,
                                             grpc_handshake_manager* mgr);

/// Remove \a mgr from the server side list of all pending handshake managers.
// Not thread-safe. Caller needs to synchronize.
void grpc_handshake_manager_pending_list_remove(grpc_handshake_manager** head,
                                                grpc_handshake_manager* mgr);

/// Shutdown all pending handshake managers on the server side.
// Not thread-safe. Caller needs to synchronize.
void grpc_handshake_manager_pending_list_shutdown_all(
    grpc_exec_ctx* exec_ctx, grpc_handshake_manager* head, grpc_error* why);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_CHANNEL_HANDSHAKER_H */
