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

#ifndef GRPC_INTERNAL_CORE_CHANNEL_METADATA_BUFFER_H
#define GRPC_INTERNAL_CORE_CHANNEL_METADATA_BUFFER_H

#include "src/core/channel/channel_stack.h"

/* Utility code to buffer GRPC_SEND_METADATA calls and pass them down the stack
   all at once at some otherwise-determined time. Useful for implementing
   filters that want to queue metadata until a START event chooses some
   underlying filter stack to send an rpc on. */

/* Clients should declare a member of grpc_metadata_buffer. This may at some
   point become a typedef for a struct, but for now a pointer suffices */
typedef struct grpc_metadata_buffer_impl grpc_metadata_buffer_impl;
typedef grpc_metadata_buffer_impl *grpc_metadata_buffer;

/* Initializes the metadata buffer. Allocates no memory. */
void grpc_metadata_buffer_init(grpc_metadata_buffer *buffer);
/* Destroy the metadata buffer. */
void grpc_metadata_buffer_destroy(grpc_metadata_buffer *buffer,
                                  grpc_op_error error);
/* Append a call to the end of a metadata buffer: may allocate memory */
void grpc_metadata_buffer_queue(grpc_metadata_buffer *buffer, grpc_call_op *op);
/* Flush all queued operations from the metadata buffer to the element below
   self */
void grpc_metadata_buffer_flush(grpc_metadata_buffer *buffer,
                                grpc_call_element *self);
/* Count the number of queued elements in the buffer. */
size_t grpc_metadata_buffer_count(const grpc_metadata_buffer *buffer);
/* Extract elements as a grpc_metadata*, for presentation to applications.
   The returned buffer must be freed with
   grpc_metadata_buffer_cleanup_elements.
   Clears the metadata buffer (this is a one-shot operation) */
grpc_metadata *grpc_metadata_buffer_extract_elements(
    grpc_metadata_buffer *buffer);
void grpc_metadata_buffer_cleanup_elements(void *elements, grpc_op_error error);

#endif  /* GRPC_INTERNAL_CORE_CHANNEL_METADATA_BUFFER_H */
