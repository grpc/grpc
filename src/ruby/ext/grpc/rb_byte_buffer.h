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

#ifndef GRPC_RB_BYTE_BUFFER_H_
#define GRPC_RB_BYTE_BUFFER_H_

#include <grpc/grpc.h>
#include <ruby.h>

/* rb_cByteBuffer is the ByteBuffer class whose instances proxy
   grpc_byte_buffer. */
extern VALUE rb_cByteBuffer;

/* Initializes the ByteBuffer class. */
void Init_grpc_byte_buffer();

/* grpc_rb_byte_buffer_create_with_mark creates a grpc_rb_byte_buffer with a
 * ruby mark object that will be kept alive while the byte_buffer is alive. */
VALUE grpc_rb_byte_buffer_create_with_mark(VALUE mark, grpc_byte_buffer* bb);

/* Gets the wrapped byte_buffer from its ruby object. */
grpc_byte_buffer* grpc_rb_get_wrapped_byte_buffer(VALUE v);

#endif /* GRPC_RB_BYTE_BUFFER_H_ */
