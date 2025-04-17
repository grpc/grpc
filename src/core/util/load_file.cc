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

#include "src/core/util/load_file.h"

#include <errno.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <stdio.h>
#include <string.h>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "src/core/util/strerror.h"

namespace grpc_core {

// Loads the content of a file into a slice. add_null_terminator will add a NULL
// terminator if true.
// This API is NOT thread-safe and requires proper synchronization when used by
// multiple threads, especially when they can happen to be reading from the same
// file.
absl::StatusOr<Slice> LoadFile(const std::string& filename,
                               bool add_null_terminator) {
  unsigned char* contents = nullptr;
  long contents_size = 0;
  FILE* file;
  size_t bytes_read = 0;
  auto sock_cleanup = absl::MakeCleanup([&file]() -> void {
    if (file != nullptr) {
      fclose(file);
    }
  });

  file = fopen(filename.c_str(), "rb");
  if (file == nullptr) {
    return absl::InternalError(
        absl::StrCat("Failed to load file: ", filename,
                     " due to error(fdopen): ", StrError(errno)));
  }
  if (fseek(file, 0, SEEK_END) < 0) {
    return absl::InternalError(
        absl::StrCat("Failed to load file: ", filename,
                     " due to error(fseek): ", StrError(errno)));
  }
  if ((contents_size = static_cast<size_t>(ftell(file))) < 0) {
    return absl::InternalError(
        absl::StrCat("Failed to load file: ", filename,
                     " due to error(ftell): ", StrError(errno)));
  }
  if (fseek(file, 0, SEEK_SET) < 0) {
    return absl::InternalError(
        absl::StrCat("Failed to load file: ", filename,
                     " due to error(fseek): ", StrError(errno)));
  }
  contents = static_cast<unsigned char*>(
      gpr_malloc(contents_size + (add_null_terminator ? 1 : 0)));
  bytes_read = fread(contents, 1, contents_size, file);
  static_assert(LONG_MAX <= SIZE_MAX,
                "size_t max should be more than long max");
  if (bytes_read < static_cast<size_t>(contents_size)) {
    gpr_free(contents);
    return absl::InternalError(
        absl::StrCat("Failed to load file: ", filename,
                     " due to error(fread): ", StrError(errno)));
  }
  if (add_null_terminator) {
    contents[contents_size++] = 0;
  }
  return Slice(
      grpc_slice_new(contents, static_cast<size_t>(contents_size), gpr_free));
}

}  // namespace grpc_core
