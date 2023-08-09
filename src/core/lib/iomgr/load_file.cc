//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/load_file.h"

#include <errno.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/block_annotate.h"

grpc_error_handle grpc_load_file(const char* filename, int add_null_terminator,
                                 grpc_slice* output) {
  unsigned char* contents = nullptr;
  size_t contents_size = 0;
  grpc_slice result = grpc_empty_slice();
  FILE* file;
  size_t bytes_read = 0;
  grpc_error_handle error;

  GRPC_SCHEDULING_START_BLOCKING_REGION;
  file = fopen(filename, "rb");
  if (file == nullptr) {
    error = GRPC_OS_ERROR(errno, "fopen");
    goto end;
  }
  fseek(file, 0, SEEK_END);
  // Converting to size_t on the assumption that it will not fail
  contents_size = static_cast<size_t>(ftell(file));
  fseek(file, 0, SEEK_SET);
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
  *output = result;
  if (file != nullptr) fclose(file);
  if (!error.ok()) {
    grpc_error_handle error_out = grpc_error_set_str(
        GRPC_ERROR_CREATE_REFERENCING("Failed to load file", &error, 1),
        grpc_core::StatusStrProperty::kFilename, filename);
    error = error_out;
  }
  GRPC_SCHEDULING_END_BLOCKING_REGION_NO_EXEC_CTX;
  return error;
}
