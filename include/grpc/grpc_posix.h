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

#ifndef GRPC_GRPC_POSIX_H
#define GRPC_GRPC_POSIX_H

#include <grpc/grpc.h>
#include <grpc/impl/grpc_types.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! \mainpage GRPC Core POSIX
 *
 * The GRPC Core POSIX library provides some POSIX-specific low-level
 * functionality on top of GRPC Core.
 */

/** Create a secure channel to 'target' using file descriptor 'fd' and passed-in
    credentials. The 'target' argument will be used to indicate the name for
    this channel. Note that this API currently only supports insecure channel
    credentials. Using other types of credentials will result in a failure. */
GRPCAPI grpc_channel* grpc_channel_create_from_fd(
    const char* target, int fd, grpc_channel_credentials* creds,
    const grpc_channel_args* args);

/** Add the connected secure communication channel based on file descriptor 'fd'
   to the 'server' and server credentials 'creds'. The 'fd' must be an open file
   descriptor corresponding to a connected socket. Events from the file
   descriptor may come on any of the server completion queues (i.e completion
   queues registered via the grpc_server_register_completion_queue API).
   Note that this API currently only supports inseure server credentials
   Using other types of credentials will result in a failure.
   TODO(hork): add channel_args to this API to allow endpoints and transports
   created in this function to participate in the resource quota feature. */
GRPCAPI void grpc_server_add_channel_from_fd(grpc_server* server, int fd,
                                             grpc_server_credentials* creds);

/** EXPERIMENTAL API - Subject to change
    Set a file descriptor that gRPC will signal via write() whenever an event
    is enqueued to a GRPC_CQ_NEXT completion queue. A single uint64_t value
    of 1 is written for each event, making this compatible with both
    eventfd(2) on Linux and ordinary pipes on other POSIX systems.

    This allows applications to integrate gRPC completion queues into their
    own event loops (epoll, poll, select) without needing a dedicated thread
    blocked on grpc_completion_queue_next().

    Pass fd=-1 to disable notification (the default). The fd must be
    non-blocking. The completion queue does NOT take ownership of the fd;
    the caller is responsible for closing it after the completion queue is
    destroyed.

    Must be called before the completion queue is polled. Not thread-safe
    with respect to concurrent grpc_completion_queue_next() calls on the
    same completion queue.

    On non-POSIX platforms, calling this function has no effect. */
GRPCAPI void grpc_completion_queue_set_notify_fd(grpc_completion_queue* cq,
                                                 int fd);

#ifdef __cplusplus
}

namespace grpc_core::experimental {

/**
 * EXPERIMENTAL API - Subject to change
 *
 * This function creates a gRPC channel using a raw file descriptor
 * that represents an open socket. This API supports both secure and insecure
 * channel credentials.
 *
 * \param fd The file descriptor representing the connection.
 * \param creds The channel credentials used to secure the connection.
 * \param args Optional channel arguments to configure the channel behavior.
 */
grpc_channel* CreateChannelFromFd(int fd, grpc_channel_credentials* creds,
                                  const grpc_channel_args* args);

}  // namespace grpc_core::experimental

#endif  // __cplusplus

#endif /* GRPC_GRPC_POSIX_H */
