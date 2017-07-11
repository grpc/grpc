/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* Windows code for gpr snprintf support. */

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS_STRING

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/lib/support/string.h"

int gpr_asprintf(char **strp, const char *format, ...) {
  va_list args;
  int ret;
  size_t strp_buflen;

  /* Determine the length. */
  va_start(args, format);
  ret = _vscprintf(format, args);
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

  /* Print to the buffer. */
  va_start(args, format);
  ret = vsnprintf_s(*strp, strp_buflen, _TRUNCATE, format, args);
  va_end(args);
  if ((size_t)ret == strp_buflen - 1) {
    return ret;
  }

  /* This should never happen. */
  gpr_free(*strp);
  *strp = NULL;
  return -1;
}

#endif /* GPR_WINDOWS_STRING */
