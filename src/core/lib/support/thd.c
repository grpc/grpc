/*
 *
 * Copyright 2015, Google Inc.
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

/* Posix implementation for gpr threads. */

#include <memory.h>

#include <grpc/support/thd.h>

enum { GPR_THD_JOINABLE = 1 };

gpr_thd_options gpr_thd_options_default(void) {
  gpr_thd_options options;
  memset(&options, 0, sizeof(options));
  return options;
}

void gpr_thd_options_set_detached(gpr_thd_options* options) {
  options->flags &= ~GPR_THD_JOINABLE;
}

void gpr_thd_options_set_joinable(gpr_thd_options* options) {
  options->flags |= GPR_THD_JOINABLE;
}

int gpr_thd_options_is_detached(const gpr_thd_options* options) {
  if (!options) return 1;
  return (options->flags & GPR_THD_JOINABLE) == 0;
}

int gpr_thd_options_is_joinable(const gpr_thd_options* options) {
  if (!options) return 0;
  return (options->flags & GPR_THD_JOINABLE) == GPR_THD_JOINABLE;
}
