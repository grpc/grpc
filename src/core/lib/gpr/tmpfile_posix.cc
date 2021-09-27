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

#ifdef GPR_POSIX_TMPFILE

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"

FILE* gpr_tmpfile(const char* prefix, char** tmp_filename) {
  FILE* result = nullptr;
  char* filename_template;
  int fd;

  if (tmp_filename != nullptr) *tmp_filename = nullptr;

  gpr_asprintf(&filename_template, "/tmp/%s_XXXXXX", prefix);
  GPR_ASSERT(filename_template != nullptr);

  fd = mkstemp(filename_template);
  if (fd == -1) {
    gpr_log(GPR_ERROR, "mkstemp failed for filename_template %s with error %s.",
            filename_template, strerror(errno));
    goto end;
  }
  result = fdopen(fd, "w+");
  if (result == nullptr) {
    gpr_log(GPR_ERROR, "Could not open file %s from fd %d (error = %s).",
            filename_template, fd, strerror(errno));
    unlink(filename_template);
    close(fd);
    goto end;
  }

end:
  if (result != nullptr && tmp_filename != nullptr) {
    *tmp_filename = filename_template;
  } else {
    gpr_free(filename_template);
  }
  return result;
}

#endif /* GPR_POSIX_TMPFILE */
