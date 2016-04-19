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

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_STRING

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>

int gpr_asprintf(char **strp, const char *format, ...) {
  va_list args;
  int ret;
  char buf[64];
  size_t strp_buflen;

  /* Use a constant-sized buffer to determine the length. */
  va_start(args, format);
  ret = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  if (ret < 0) {
    *strp = NULL;
    return -1;
  }

  /* Allocate a new buffer, with space for the NUL terminator. */
  strp_buflen = (size_t)ret + 1;
  if ((*strp = gpr_malloc(strp_buflen)) == NULL) {
    /* This shouldn't happen, because gpr_malloc() calls abort(). */
    return -1;
  }

  /* Return early if we have all the bytes. */
  if (strp_buflen <= sizeof(buf)) {
    memcpy(*strp, buf, strp_buflen);
    return ret;
  }

  /* Try again using the larger buffer. */
  va_start(args, format);
  ret = vsnprintf(*strp, strp_buflen, format, args);
  va_end(args);
  if ((size_t)ret == strp_buflen - 1) {
    return ret;
  }

  /* This should never happen. */
  gpr_free(*strp);
  *strp = NULL;
  return -1;
}

#endif /* GPR_POSIX_STRING */
