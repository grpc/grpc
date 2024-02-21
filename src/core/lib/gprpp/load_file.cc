// Copyright 2022 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/load_file.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

namespace grpc_core {

// Loads the content of a file into a slice. add_null_terminator will add a NULL
// terminator if true.
absl::StatusOr<Slice> LoadFile(const std::string& filename,
                               bool add_null_terminator) {
  unsigned char* contents = nullptr;
  size_t contents_size = 0;
  FILE* file;
  size_t bytes_read = 0;
  absl::Status error = absl::OkStatus();
  auto sock_cleanup = absl::MakeCleanup([&file]() -> void {
    if (file != nullptr) {
      fclose(file);
    }
  });

  file = fopen(filename.c_str(), "rb");
  if (file == nullptr) {
    error = absl::InternalError(
        absl::StrCat("Failed to load file: ", filename,
                     " due to error(fdopen): ", strerror(errno)));
    return error;
  }
  fseek(file, 0, SEEK_END);
  // Converting to size_t on the assumption that it will not fail.
  contents_size = static_cast<size_t>(ftell(file));
  fseek(file, 0, SEEK_SET);
  contents = static_cast<unsigned char*>(
      gpr_malloc(contents_size + (add_null_terminator ? 1 : 0)));
  bytes_read = fread(contents, 1, contents_size, file);
  if (bytes_read < contents_size) {
    gpr_free(contents);
    GPR_ASSERT(ferror(file));
    error = absl::InternalError(
        absl::StrCat("Failed to load file: ", filename,
                     " due to error(fread): ", strerror(errno)));
    return error;
  }
  if (add_null_terminator) {
    contents[contents_size++] = 0;
  }
  return Slice(grpc_slice_new(contents, contents_size, gpr_free));
}

}  // namespace grpc_core
