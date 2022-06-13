//
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

#include "src/core/lib/security/util/file_util.h"

#include <stdio.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

TmpFile::TmpFile(absl::string_view data) {
  name_ = CreateTmpFileAndWriteData(data);
  GPR_ASSERT(!name_.empty());
}

TmpFile::~TmpFile() { GPR_ASSERT(remove(name_.c_str()) == 0); }

void TmpFile::RewriteFile(absl::string_view data) {
  // Create a new file containing new data.
  std::string new_name = CreateTmpFileAndWriteData(data);
  GPR_ASSERT(!new_name.empty());
  // Remove the old file.
  GPR_ASSERT(remove(name_.c_str()) == 0);
  // Rename the new file to the original name.
  GPR_ASSERT(rename(new_name.c_str(), name_.c_str()) == 0);
}

std::string TmpFile::CreateTmpFileAndWriteData(absl::string_view data) {
  char* name = nullptr;
  FILE* file_descriptor = gpr_tmpfile("test", &name);
  GPR_ASSERT(fwrite(data.data(), 1, data.size(), file_descriptor) ==
             data.size());
  GPR_ASSERT(fclose(file_descriptor) == 0);
  GPR_ASSERT(file_descriptor != nullptr);
  GPR_ASSERT(name != nullptr);
  std::string name_to_return = name;
  gpr_free(name);
  return name_to_return;
}

std::string GetFileContents(const char* path) {
  grpc_slice slice = grpc_empty_slice();
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file", grpc_load_file(path, 0, &slice)));
  std::string data = std::string(StringViewFromSlice(slice));
  grpc_slice_unref(slice);
  return data;
}

}  // namespace grpc_core
