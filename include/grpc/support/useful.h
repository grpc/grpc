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

#ifndef GRPC_SUPPORT_USEFUL_H
#define GRPC_SUPPORT_USEFUL_H

/* useful macros that don't belong anywhere else */

#define GPR_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GPR_MAX(a, b) ((a) > (b) ? (a) : (b))
#define GPR_CLAMP(a, min, max) ((a) < (min) ? (min) : (a) > (max) ? (max) : (a))
/* rotl, rotr assume x is unsigned */
#define GPR_ROTL(x, n) (((x) << (n)) | ((x) >> (sizeof(x) * 8 - (n))))
#define GPR_ROTR(x, n) (((x) >> (n)) | ((x) << (sizeof(x) * 8 - (n))))

#define GPR_ARRAY_SIZE(array) (sizeof(array) / sizeof(*(array)))

#define GPR_SWAP(type, a, b) \
  do {                   \
    type x = a;          \
    a = b;               \
    b = x;               \
  } while (0)

#endif  /* GRPC_SUPPORT_USEFUL_H */
