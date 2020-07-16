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

#include "src/core/lib/iomgr/load_file.h"

#include <errno.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/block_annotate.h"

grpc_error* grpc_load_file(const char* filename, int add_null_terminator,
                           grpc_slice* output) {
  unsigned char* contents = nullptr;
  size_t contents_size = 0;
  grpc_slice result = grpc_empty_slice();
  FILE* file;
  size_t bytes_read = 0;
  grpc_error* error = GRPC_ERROR_NONE;
  int seek_result1 = 0;
  int seek_result2 = 0;
  int filenum = 0;

  //gpr_log(GPR_ERROR, "reading file: %s", filename );


  GRPC_SCHEDULING_START_BLOCKING_REGION;
  file = fopen(filename, "rb");
  
  if (file == nullptr) {
    error = GRPC_OS_ERROR(errno, "fopen");
    goto end;
  }
  seek_result1 = fseek(file, 0, SEEK_END);
  //gpr_log(GPR_ERROR, "seek to end result: %d", seek_result1 );
  /* Converting to size_t on the assumption that it will not fail */
  contents_size = static_cast<size_t>(ftell(file));
  //gpr_log(GPR_ERROR, "ftell result: %d", (int) contents_size );
  seek_result2 = fseek(file, 0, SEEK_SET);
  //gpr_log(GPR_ERROR, "seek to start result: %d", seek_result2 );
  contents = static_cast<unsigned char*>(
      gpr_malloc(contents_size + (add_null_terminator ? 1 : 0)));
  bytes_read = fread(contents, 1, contents_size, file);
  if (bytes_read < contents_size) {
    gpr_free(contents);
    error = GRPC_OS_ERROR(errno, "fread");
    GPR_ASSERT(ferror(file));
    goto end;
  }
  if (add_null_terminator) {
    contents[contents_size++] = 0;
  }
  result = grpc_slice_new(contents, contents_size, gpr_free);

end:
  filenum = fileno(file);
  gpr_log(GPR_ERROR, "reading %s : file %p : filenum %d : bytes_read %d : contents_size %d:  seek1 result %d, seek2 result: %d", filename, file, filenum, (int) bytes_read, (int) contents_size, seek_result1, seek_result2 );  
  *output = result;
  if (file != nullptr) fclose(file);
  if (error != GRPC_ERROR_NONE) {
    grpc_error* error_out =
        grpc_error_set_str(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                               "Failed to load file", &error, 1),
                           GRPC_ERROR_STR_FILENAME,
                           grpc_slice_from_copied_string(
                               filename));  // TODO(ncteisen), always static?
    GRPC_ERROR_UNREF(error);
    error = error_out;
  }
  GRPC_SCHEDULING_END_BLOCKING_REGION_NO_EXEC_CTX;
  return error;
}
