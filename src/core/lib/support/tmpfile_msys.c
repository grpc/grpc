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

#ifdef GPR_MSYS_TMPFILE

#include <io.h>
#include <stdio.h>
#include <string.h>
#include <tchar.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/support/string_windows.h"
#include "src/core/lib/support/tmpfile.h"

FILE *gpr_tmpfile(const char *prefix, char **tmp_filename_out) {
  FILE *result = NULL;
  char tmp_filename[MAX_PATH];
  UINT success;

  if (tmp_filename_out != NULL) *tmp_filename_out = NULL;

  /* Generate a unique filename with our template + temporary path. */
  success = GetTempFileNameA(".", prefix, 0, tmp_filename);
  fprintf(stderr, "success = %d\n", success);

  if (success) {
    /* Open a file there. */
    result = fopen(tmp_filename, "wb+");
    fprintf(stderr, "result = %p\n", result);
  }
  if (result != NULL && tmp_filename_out) {
    *tmp_filename_out = gpr_strdup(tmp_filename);
  }

  return result;
}

#endif /* GPR_MSYS_TMPFILE */
