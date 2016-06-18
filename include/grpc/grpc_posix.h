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

#ifndef GRPC_GRPC_POSIX_H
#define GRPC_GRPC_POSIX_H

#include <grpc/impl/codegen/grpc_types.h>
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

/** Create a client channel to 'target' using file descriptor 'fd'. The 'target'
    argument will be used to indicate the name for this channel. See the comment
    for grpc_insecure_channel_create for description of 'args' argument. */
GRPCAPI grpc_channel *grpc_insecure_channel_create_from_fd(
    const char *target, int fd, const grpc_channel_args *args);

/** Add the connected communication channel based on file descriptor 'fd' to the
    'server'. The 'fd' must be an open file descriptor corresponding to a
    connected socket. The 'cq' is a completion queue that will be getting events
    from that descriptor. */
GRPCAPI void grpc_server_add_insecure_channel_from_fd(grpc_server *server,
                                                      grpc_completion_queue *cq,
                                                      int fd);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_GRPC_POSIX_H */
