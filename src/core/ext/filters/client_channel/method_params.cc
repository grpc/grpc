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

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/method_params.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"

namespace grpc_core {
namespace internal {

namespace {

bool ParseWaitForReady(
    grpc_json* field, ClientChannelMethodParams::WaitForReady* wait_for_ready) {
  if (field->type != GRPC_JSON_TRUE && field->type != GRPC_JSON_FALSE) {
    return false;
  }
  *wait_for_ready = field->type == GRPC_JSON_TRUE
                        ? ClientChannelMethodParams::WAIT_FOR_READY_TRUE
                        : ClientChannelMethodParams::WAIT_FOR_READY_FALSE;
  return true;
}

// Parses a JSON field of the form generated for a google.proto.Duration
// proto message.
bool ParseDuration(grpc_json* field, grpc_millis* duration) {
  if (field->type != GRPC_JSON_STRING) return false;
  size_t len = strlen(field->value);
  if (field->value[len - 1] != 's') return false;
  UniquePtr<char> buf(gpr_strdup(field->value));
  *(buf.get() + len - 1) = '\0';  // Remove trailing 's'.
  char* decimal_point = strchr(buf.get(), '.');
  int nanos = 0;
  if (decimal_point != nullptr) {
    *decimal_point = '\0';
    nanos = gpr_parse_nonnegative_int(decimal_point + 1);
    if (nanos == -1) {
      return false;
    }
    int num_digits = (int)strlen(decimal_point + 1);
    if (num_digits > 9) {  // We don't accept greater precision than nanos.
      return false;
    }
    for (int i = 0; i < (9 - num_digits); ++i) {
      nanos *= 10;
    }
  }
  int seconds =
      decimal_point == buf.get() ? 0 : gpr_parse_nonnegative_int(buf.get());
  if (seconds == -1) return false;
  *duration = seconds * GPR_MS_PER_SEC + nanos / GPR_NS_PER_MS;
  return true;
}

}  // namespace

RefCountedPtr<ClientChannelMethodParams>
ClientChannelMethodParams::CreateFromJson(const grpc_json* json) {
  RefCountedPtr<ClientChannelMethodParams> method_params =
      MakeRefCounted<ClientChannelMethodParams>();
  for (grpc_json* field = json->child; field != nullptr; field = field->next) {
    if (field->key == nullptr) continue;
    if (strcmp(field->key, "waitForReady") == 0) {
      if (method_params->wait_for_ready_ != WAIT_FOR_READY_UNSET) {
        return nullptr;  // Duplicate.
      }
      if (!ParseWaitForReady(field, &method_params->wait_for_ready_)) {
        return nullptr;
      }
    } else if (strcmp(field->key, "timeout") == 0) {
      if (method_params->timeout_ > 0) return nullptr;  // Duplicate.
      if (!ParseDuration(field, &method_params->timeout_)) return nullptr;
    }
  }
  return method_params;
}

}  // namespace internal
}  // namespace grpc_core
