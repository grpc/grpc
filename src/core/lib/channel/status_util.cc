/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/channel/status_util.h"

#include "src/core/lib/gpr/useful.h"

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

bool grpc_status_code_from_string(const char* status_str,
                                  grpc_status_code* status) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(g_status_string_entries); ++i) {
    if (strcmp(status_str, g_status_string_entries[i].str) == 0) {
      *status = g_status_string_entries[i].status;
      return true;
    }
  }
  return false;
}

const char* grpc_status_code_to_string(grpc_status_code status) {
  switch (status) {
    case GRPC_STATUS_OK:
      return "OK";
    case GRPC_STATUS_CANCELLED:
      return "CANCELLED";
    case GRPC_STATUS_UNKNOWN:
      return "UNKNOWN";
    case GRPC_STATUS_INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case GRPC_STATUS_DEADLINE_EXCEEDED:
      return "DEADLINE_EXCEEDED";
    case GRPC_STATUS_NOT_FOUND:
      return "NOT_FOUND";
    case GRPC_STATUS_ALREADY_EXISTS:
      return "ALREADY_EXISTS";
    case GRPC_STATUS_PERMISSION_DENIED:
      return "PERMISSION_DENIED";
    case GRPC_STATUS_UNAUTHENTICATED:
      return "UNAUTHENTICATED";
    case GRPC_STATUS_RESOURCE_EXHAUSTED:
      return "RESOURCE_EXHAUSTED";
    case GRPC_STATUS_FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case GRPC_STATUS_ABORTED:
      return "ABORTED";
    case GRPC_STATUS_OUT_OF_RANGE:
      return "OUT_OF_RANGE";
    case GRPC_STATUS_UNIMPLEMENTED:
      return "UNIMPLEMENTED";
    case GRPC_STATUS_INTERNAL:
      return "INTERNAL";
    case GRPC_STATUS_UNAVAILABLE:
      return "UNAVAILABLE";
    case GRPC_STATUS_DATA_LOSS:
      return "DATA_LOSS";
    default:
      return "UNKNOWN";
  }
}
