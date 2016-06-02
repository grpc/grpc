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

/// \mainpage gRPC C++ API
///
/// The gRPC C++ API mainly consists of the following classes:
/// - grpc::Channel, which represents the connection to an endpoint. See [the
/// gRPC Concepts page](http://www.grpc.io/docs/guides/concepts.html) for more
/// details. Channels are created by the factory function grpc::CreateChannel.
/// - grpc::CompletionQueue, the producer-consumer queue used for all
/// asynchronous communication with the gRPC runtime.
/// - grpc::ClientContext and grpc::ServerContext, where optional configuration
/// for an RPC can be set, such as setting custom metadata to be conveyed to the
/// peer, compression settings, authentication, etc.
/// - grpc::Server, representing a gRPC server, created by grpc::ServerBuilder.
///
/// Refer to the
/// [examples](https://github.com/grpc/grpc/blob/master/examples/cpp)
/// for code putting these pieces into play.

#ifndef GRPCXX_GRPCXX_H
#define GRPCXX_GRPCXX_H

#include <grpc/grpc.h>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/create_channel.h>
#include <grpc++/create_channel_posix.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/server_posix.h>

#endif  // GRPCXX_GRPCXX_H
