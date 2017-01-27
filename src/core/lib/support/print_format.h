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

#ifndef GRPC_CORE_LIB_SUPPORT_PRINT_FORMAT_H
#define GRPC_CORE_LIB_SUPPORT_PRINT_FORMAT_H

#include <grpc/support/port_platform.h>

#ifdef __cplusplus
#include <cinttypes>
#else
#include <inttypes.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Defines formatting codes (as "PRIxPTR" and similar from inttypes.h) that work
 * correctly even with more exotic platforms (e.g. mingw's inttypes are broken
 * because PRIuPTR becomes %lld instead of the desired %I64u)
 */

#ifdef GPR_MINGW_PRINT_FORMAT_MACROS

#ifdef GPR_ARCH_64
#define GPR_PRIdPTR "I64d"
#define GPR_PRIiPTR "I64i"
#define GPR_PRIuPTR "I64u"
#define GPR_PRIxPTR "I64x"
#define GPR_PRIXPTR "I64X"
#else
#define GPR_PRIdPTR "d"
#define GPR_PRIiPTR "i"
#define GPR_PRIuPTR "u"
#define GPR_PRIxPTR "x"
#define GPR_PRIXPTR "X"
#endif /* GPR_ARCH_64 */

#else

#define GPR_PRIdPTR PRIdPTR
#define GPR_PRIiPTR PRIiPTR
#define GPR_PRIuPTR PRIuPTR
#define GPR_PRIxPTR PRIxPTR
#define GPR_PRIXPTR PRIXPTR

#endif /* GPR_MINGW_PRINT_FORMAT_MACROS */

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SUPPORT_PRINT_FORMAT_H */
