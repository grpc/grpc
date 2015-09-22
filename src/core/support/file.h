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

#ifndef GRPC_INTERNAL_CORE_SUPPORT_FILE_H
#define GRPC_INTERNAL_CORE_SUPPORT_FILE_H

#include <stdio.h>

#include <grpc/support/slice.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* File utility functions */

/* Loads the content of a file into a slice. add_null_terminator will add
   a NULL terminator if non-zero. The success parameter, if not NULL,
   will be set to 1 in case of success and 0 in case of failure. */
  gpr_slice gpr_load_file (const char *filename, int add_null_terminator, int *success);

/* Creates a temporary file from a prefix.
   If tmp_filename is not NULL, *tmp_filename is assigned the name of the
   created file and it is the responsibility of the caller to gpr_free it
   unless an error occurs in which case it will be set to NULL. */
  FILE *gpr_tmpfile (const char *prefix, char **tmp_filename);

#ifdef __cplusplus
}
#endif

#endif				/* GRPC_INTERNAL_CORE_SUPPORT_FILE_H */
