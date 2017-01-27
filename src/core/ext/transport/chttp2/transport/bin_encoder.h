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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_BIN_ENCODER_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_BIN_ENCODER_H

#include <grpc/slice.h>

/* base64 encode a slice. Returns a new slice, does not take ownership of the
   input */
grpc_slice grpc_chttp2_base64_encode(grpc_slice input);

/* Compress a slice with the static huffman encoder detailed in the hpack
   standard. Returns a new slice, does not take ownership of the input */
grpc_slice grpc_chttp2_huffman_compress(grpc_slice input);

/* equivalent to:
   grpc_slice x = grpc_chttp2_base64_encode(input);
   grpc_slice y = grpc_chttp2_huffman_compress(x);
   grpc_slice_unref_internal(exec_ctx, x);
   return y; */
grpc_slice grpc_chttp2_base64_encode_and_huffman_compress(grpc_slice input);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_BIN_ENCODER_H */
