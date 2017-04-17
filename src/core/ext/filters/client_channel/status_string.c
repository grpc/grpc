/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/ext/filters/client_channel/status_string.h"

#include <string.h>

#include <grpc/support/useful.h>

typedef struct {
  const char* str;
  grpc_status_code status;
} status_string_entry;

static const status_string_entry g_status_string_entries[] = {
  {"OK", GRPC_STATUS_OK},
  {"CANCELLED", GRPC_STATUS_CANCELLED},
  {"UNKNOWN", GRPC_STATUS_UNKNOWN},
  {"INVALID_ARGUMENT", GRPC_STATUS_INVALID_ARGUMENT},
  {"DEADLINE_EXCEEDED", GRPC_STATUS_DEADLINE_EXCEEDED},
  {"NOT_FOUND", GRPC_STATUS_NOT_FOUND},
  {"ALREADY_EXISTS", GRPC_STATUS_ALREADY_EXISTS},
  {"PERMISSION_DENIED", GRPC_STATUS_PERMISSION_DENIED},
  {"UNAUTHENTICATED", GRPC_STATUS_UNAUTHENTICATED},
  {"RESOURCE_EXHAUSTED", GRPC_STATUS_RESOURCE_EXHAUSTED},
  {"FAILED_PRECONDITION", GRPC_STATUS_FAILED_PRECONDITION},
  {"ABORTED", GRPC_STATUS_ABORTED},
  {"OUT_OF_RANGE", GRPC_STATUS_OUT_OF_RANGE},
  {"UNIMPLEMENTED", GRPC_STATUS_UNIMPLEMENTED},
  {"INTERNAL", GRPC_STATUS_INTERNAL},
  {"UNAVAILABLE", GRPC_STATUS_UNAVAILABLE},
  {"DATA_LOSS", GRPC_STATUS_DATA_LOSS},
};

bool grpc_status_from_string(const char* status_str, grpc_status_code* status) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(g_status_string_entries); ++i) {
    if (strcmp(status_str, g_status_string_entries[i].str) == 0) {
      *status = g_status_string_entries[i].status;
      return true;
    }
  }
  return false;
}
