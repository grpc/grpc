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

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/stat.h"
#include "src/core/lib/gprpp/strerror.h"

namespace grpc_core {

absl::Status GetFileModificationTime(const char* filename, time_t* timestamp) {
  CHECK_NE(filename, nullptr);
  CHECK_NE(timestamp, nullptr);
  struct _stat buf;
  if (_stat(filename, &buf) != 0) {
    std::string error_msg = StrError(errno);
    LOG(ERROR) << "_stat failed for filename " << filename << " with error "
               << error_msg;
    return absl::Status(absl::StatusCode::kInternal, error_msg);
  }
  // Last file/directory modification time.
  *timestamp = buf.st_mtime;
  return absl::OkStatus();
}

}  // namespace grpc_core

#endif  // GPR_WINDOWS_STAT
