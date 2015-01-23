/*
 *
 * Copyright 2014, Google Inc.
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

#ifndef __GRPC_SUPPORT_STRING_H__
#define __GRPC_SUPPORT_STRING_H__

#include <stddef.h>

#include <grpc/support/port_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/* String utility functions */

/* Returns a copy of src that can be passed to gpr_free().
   If allocation fails or if src is NULL, returns NULL. */
char *gpr_strdup(const char *src);

/* flag to include plaintext after a hexdump */
#define GPR_HEXDUMP_PLAINTEXT 0x00000001

/* Converts array buf, of length len, into a hexadecimal dump. Result should
   be freed with gpr_free() */
char *gpr_hexdump(const char *buf, size_t len, gpr_uint32 flags);

/* Parses an array of bytes into an integer (base 10). Returns 1 on success,
   0 on failure. */
int gpr_parse_bytes_to_uint32(const char *data, size_t length,
                              gpr_uint32 *result);

/* minimum buffer size for calling ltoa */
#define GPR_LTOA_MIN_BUFSIZE (3 * sizeof(long))

/* Convert a long to a string in base 10; returns the length of the
   output string (or 0 on failure) */
int gpr_ltoa(long value, char *output);

/* Reverse a run of bytes */
void gpr_reverse_bytes(char *str, int len);

/* printf to a newly-allocated string.  The set of supported formats may vary
   between platforms.

   On success, returns the number of bytes printed (excluding the final '\0'),
   and *strp points to a string which must later be destroyed with gpr_free().

   On error, returns -1 and sets *strp to NULL. If the format string is bad,
   the result is undefined. */
int gpr_asprintf(char **strp, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* __GRPC_SUPPORT_STRING_H__ */
