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
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <tchar.h>
#include <time.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/stat.h"
#include "src/core/lib/gpr/string_windows.h"

const char* gpr_last_modified_timestamp(const char* filename) {
  // Initializing with epoch time.
  time_t ts = 0;
  struct _stat buf;
  if (_stat(filename, &buf) != 0) {
    gpr_log(GPR_ERROR, "_stat failed for filename %s with error %s.", filename,
            strerror(errno));
    goto end;
  }
  // Last file/directory modification time.
  ts = buf.st_mtime;
end:
  return ts;
}

#endif  // GPR_WINDOWS_STAT
