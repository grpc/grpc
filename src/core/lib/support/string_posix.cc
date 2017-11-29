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

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_STRING

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

int gpr_asprintf(char** strp, const char* format, ...) {
  va_list args;
  int ret;
  char buf[64];
  size_t strp_buflen;

  /* Use a constant-sized buffer to determine the length. */
  va_start(args, format);
  ret = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  if (ret < 0) {
    *strp = nullptr;
    return -1;
  }

  /* Allocate a new buffer, with space for the NUL terminator. */
  strp_buflen = (size_t)ret + 1;
  if ((*strp = (char*)gpr_malloc(strp_buflen)) == nullptr) {
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
  *strp = nullptr;
  return -1;
}

#endif /* GPR_POSIX_STRING */
