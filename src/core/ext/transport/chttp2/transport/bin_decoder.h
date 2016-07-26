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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_BIN_DECODER_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_BIN_DECODER_H

#include <grpc/support/slice.h>
#include <stdbool.h>

struct grpc_base64_decode_context {
  /* input/output: */
  uint8_t *input_cur;
  uint8_t *input_end;
  uint8_t *output_cur;
  uint8_t *output_end;
  /* Indicate if the decoder should handle the tail of input data*/
  bool contains_tail;
};

/* base64 decode a grpc_base64_decode_context util either input_end is reached
   or output_end is reached. When input_end is reached, (input_end - input_cur)
   is less than 4. When output_end is reached, (output_end - output_cur) is less
   than 3. Returns false if decoding is failed. */
bool grpc_base64_decode_partial(struct grpc_base64_decode_context *ctx);

/* base64 decode a slice with pad chars. Returns a new slice, does not take
   ownership of the input. Returns an empty slice if decoding is failed. */
gpr_slice grpc_chttp2_base64_decode(gpr_slice input);

/* base64 decode a slice without pad chars, data length is needed. Returns a new
   slice, does not take ownership of the input. Returns an empty slice if
   decoding is failed. */
gpr_slice grpc_chttp2_base64_decode_with_length(gpr_slice input,
                                                size_t output_length);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_BIN_DECODER_H */
