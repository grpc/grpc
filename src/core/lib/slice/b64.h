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

#ifndef GRPC_CORE_LIB_SLICE_B64_H
#define GRPC_CORE_LIB_SLICE_B64_H

#include <grpc/slice.h>

/* Encodes data using base64. It is the caller's responsability to free
   the returned char * using gpr_free. Returns NULL on NULL input.
   TODO(makdharma) : change the flags to bool from int */
char *grpc_base64_encode(const void *data, size_t data_size, int url_safe,
                         int multiline);

/* estimate the upper bound on size of base64 encoded data. The actual size
 * is guaranteed to be less than or equal to the size returned here. */
size_t grpc_base64_estimate_encoded_size(size_t data_size, int url_safe,
                                         int multiline);

/* Encodes data using base64 and write it to memory pointed to by result. It is
 * the caller's responsiblity to allocate enough memory in |result| to fit the
 * encoded data. */
void grpc_base64_encode_core(char *result, const void *vdata, size_t data_size,
                             int url_safe, int multiline);

/* Decodes data according to the base64 specification. Returns an empty
   slice in case of failure. */
grpc_slice grpc_base64_decode(grpc_exec_ctx *exec_ctx, const char *b64,
                              int url_safe);

/* Same as above except that the length is provided by the caller. */
grpc_slice grpc_base64_decode_with_len(grpc_exec_ctx *exec_ctx, const char *b64,
                                       size_t b64_len, int url_safe);

#endif /* GRPC_CORE_LIB_SLICE_B64_H */
