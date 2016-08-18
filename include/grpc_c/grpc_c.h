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

#ifndef GRPC_C_GRPC_C_H
#define GRPC_C_GRPC_C_H

typedef struct grpc_channel GRPC_channel; /* using core data type */
typedef struct GRPC_context
    GRPC_context; /* base class for client and server context */
typedef struct GRPC_client_context GRPC_client_context;
typedef struct GRPC_server_context GRPC_server_context;
typedef struct grpc_completion_queue
    GRPC_completion_queue; /* using core data type */
typedef struct GRPC_incoming_notification_queue
    GRPC_incoming_notification_queue;
typedef struct GRPC_server GRPC_server;
typedef struct GRPC_registered_service GRPC_registered_service;

typedef struct GRPC_client_reader_writer GRPC_client_reader_writer;
typedef struct GRPC_client_reader GRPC_client_reader;
typedef struct GRPC_client_writer GRPC_client_writer;
typedef struct GRPC_client_async_reader_writer GRPC_client_async_reader_writer;
typedef struct GRPC_client_async_reader GRPC_client_async_reader;
typedef struct GRPC_client_async_writer GRPC_client_async_writer;
typedef struct GRPC_client_async_response_reader
    GRPC_client_async_response_reader;

typedef struct GRPC_server_async_response_writer
    GRPC_server_async_response_writer;

#endif /* GRPC_C_GRPC_C_H */
