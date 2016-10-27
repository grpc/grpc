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

#ifndef GRPC_CORE_LIB_SLICE_PERCENT_ENCODING_H
#define GRPC_CORE_LIB_SLICE_PERCENT_ENCODING_H

/* Percent encoding and decoding of slices.
   Transforms arbitrary strings into safe-for-transmission strings by using
   variants of percent encoding (RFC 3986).
   Two major variants are supplied: one that strictly matches URL encoding,
     and another which applies percent encoding only to non-http2 header
     bytes (the 'compatible' variant) */

#include <stdbool.h>

#include <grpc/slice.h>

/* URL percent encoding spec bitfield (usabel as 'unreserved_bytes' in
   grpc_percent_encode_slice, grpc_strict_percent_decode_slice).
   Flags [A-Za-z0-9-_.~] as unreserved bytes for the percent encoding routines
   */
extern const uint8_t grpc_url_percent_encoding_unreserved_bytes[256 / 8];
/* URL percent encoding spec bitfield (usabel as 'unreserved_bytes' in
   grpc_percent_encode_slice, grpc_strict_percent_decode_slice).
   Flags ascii7 non-control characters excluding '%' as unreserved bytes for the
   percent encoding routines */
extern const uint8_t grpc_compatible_percent_encoding_unreserved_bytes[256 / 8];

/* Percent-encode a slice, returning the new slice (this cannot fail):
   unreserved_bytes is a bitfield indicating which bytes are considered
   unreserved and thus do not need percent encoding */
grpc_slice grpc_percent_encode_slice(grpc_slice slice,
                                     const uint8_t *unreserved_bytes);
/* Percent-decode a slice, strictly.
   If the input is legal (contains no unreserved bytes, and legal % encodings),
   returns true and sets *slice_out to the decoded slice.
   If the input is not legal, returns false and leaves *slice_out untouched.
   unreserved_bytes is a bitfield indicating which bytes are considered
   unreserved and thus do not need percent encoding */
bool grpc_strict_percent_decode_slice(grpc_slice slice_in,
                                      const uint8_t *unreserved_bytes,
                                      grpc_slice *slice_out);
/* Percent-decode a slice, permissively.
   If a % triplet can not be decoded, pass it through verbatim.
   This cannot fail. */
grpc_slice grpc_permissive_percent_decode_slice(grpc_slice slice_in);

#endif /* GRPC_CORE_LIB_SLICE_PERCENT_ENCODING_H */
