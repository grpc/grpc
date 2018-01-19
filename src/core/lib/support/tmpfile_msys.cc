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

FILE* gpr_tmpfile(const char* prefix, char** tmp_filename_out) {
  FILE* result = NULL;
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
