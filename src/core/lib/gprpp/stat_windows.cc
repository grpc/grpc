//
// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS_STAT

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/stat.h"

namespace grpc_core {

absl::Status GetFileModificationTime(const char* filename, time_t* timestamp) {
  GPR_ASSERT(filename != nullptr);
  GPR_ASSERT(timestamp != nullptr);
  struct _stat buf;
  if (_stat(filename, &buf) != 0) {
    const char* error_msg = strerror(errno);
    gpr_log(GPR_ERROR, "_stat failed for filename %s with error %s.", filename,
            error_msg);
    return absl::Status(absl::StatusCode::kInternal, error_msg);
  }
  // Last file/directory modification time.
  *timestamp = buf.st_mtime;
  return absl::OkStatus();
}

}  // namespace grpc_core

#endif  // GPR_WINDOWS_STAT
