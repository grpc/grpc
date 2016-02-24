/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_FILE

#include "src/core/support/tmpfile.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/support/string.h"

FILE *gpr_tmpfile(const char *prefix, char **tmp_filename) {
  FILE *result = NULL;
  char *template;
  int fd;

  if (tmp_filename != NULL) *tmp_filename = NULL;

  gpr_asprintf(&template, "/tmp/%s_XXXXXX", prefix);
  GPR_ASSERT(template != NULL);

  fd = mkstemp(template);
  if (fd == -1) {
    gpr_log(GPR_ERROR, "mkstemp failed for template %s with error %s.",
            template, strerror(errno));
    goto end;
  }
  result = fdopen(fd, "w+");
  if (result == NULL) {
    gpr_log(GPR_ERROR, "Could not open file %s from fd %d (error = %s).",
            template, fd, strerror(errno));
    unlink(template);
    close(fd);
    goto end;
  }

end:
  if (result != NULL && tmp_filename != NULL) {
    *tmp_filename = template;
  } else {
    gpr_free(template);
  }
  return result;
}

#endif /* GPR_POSIX_FILE */
