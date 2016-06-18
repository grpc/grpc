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

#include "src/core/lib/iomgr/load_file.h"

#include <errno.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/support/block_annotate.h"
#include "src/core/lib/support/string.h"

grpc_error *grpc_load_file(const char *filename, int add_null_terminator,
                           gpr_slice *output) {
  unsigned char *contents = NULL;
  size_t contents_size = 0;
  gpr_slice result = gpr_empty_slice();
  FILE *file;
  size_t bytes_read = 0;
  grpc_error *error = GRPC_ERROR_NONE;

  GRPC_SCHEDULING_START_BLOCKING_REGION;
  file = fopen(filename, "rb");
  if (file == NULL) {
    error = GRPC_OS_ERROR(errno, "fopen");
    goto end;
  }
  fseek(file, 0, SEEK_END);
  /* Converting to size_t on the assumption that it will not fail */
  contents_size = (size_t)ftell(file);
  fseek(file, 0, SEEK_SET);
  contents = gpr_malloc(contents_size + (add_null_terminator ? 1 : 0));
  bytes_read = fread(contents, 1, contents_size, file);
  if (bytes_read < contents_size) {
    error = GRPC_OS_ERROR(errno, "fread");
    GPR_ASSERT(ferror(file));
    goto end;
  }
  if (add_null_terminator) {
    contents[contents_size++] = 0;
  }
  result = gpr_slice_new(contents, contents_size, gpr_free);

end:
  *output = result;
  if (file != NULL) fclose(file);
  if (error != GRPC_ERROR_NONE) {
    grpc_error *error_out = grpc_error_set_str(
        GRPC_ERROR_CREATE_REFERENCING("Failed to load file", &error, 1),
        GRPC_ERROR_STR_FILENAME, filename);
    GRPC_ERROR_UNREF(error);
    error = error_out;
  }
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  return error;
}
