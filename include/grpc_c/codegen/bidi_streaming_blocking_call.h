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

#ifndef GRPC_C_CODEGEN_BIDI_STREAMING_BLOCKING_CALL_H
#define GRPC_C_CODEGEN_BIDI_STREAMING_BLOCKING_CALL_H

#include <grpc_c/codegen/message.h>
#include <grpc_c/codegen/method.h>
#include <grpc_c/grpc_c.h>
#include <grpc_c/status.h>
#include <stdbool.h>

GRPC_client_reader_writer *GRPC_bidi_streaming_blocking_call(
    const GRPC_method rpc_method, GRPC_client_context *const context);

bool GRPC_bidi_streaming_blocking_read(GRPC_client_reader_writer *reader,
                                       void *response);

bool GRPC_bidi_streaming_blocking_write(
    GRPC_client_reader_writer *reader_writer, const GRPC_message request);

/* Marks the end of client stream. Useful for bidi-calls where     */
/* the server needs all data from the client to produce a response */
bool GRPC_bidi_streaming_blocking_writes_done(
    GRPC_client_reader_writer *reader_writer);

GRPC_status GRPC_client_reader_writer_terminate(
    GRPC_client_reader_writer *reader_writer);

#endif /* GRPC_C_CODEGEN_BIDI_STREAMING_BLOCKING_CALL_H */
