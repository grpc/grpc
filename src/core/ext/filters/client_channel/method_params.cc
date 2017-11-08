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

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/method_params.h"
#include "src/core/ext/filters/client_channel/status_util.h"
#include "src/core/lib/support/string.h"

// As per the retry design, we do not allow more than 5 retry attempts.
#define MAX_MAX_RETRY_ATTEMPTS 5

namespace grpc_core {
namespace internal {

namespace {

void retry_policy_params_free(retry_policy_params* retry_policy) {
  if (retry_policy != NULL) {
    gpr_free(retry_policy);
  }
}

}  // namespace

method_parameters* method_parameters_ref(method_parameters* method_params) {
  gpr_ref(&method_params->refs);
  return method_params;
}

void method_parameters_unref(method_parameters* method_params) {
  if (gpr_unref(&method_params->refs)) {
    retry_policy_params_free(method_params->retry_policy);
    gpr_free(method_params);
  }
}

namespace {

bool parse_wait_for_ready(grpc_json* field,
                                 wait_for_ready_value* wait_for_ready) {
  if (field->type != GRPC_JSON_TRUE && field->type != GRPC_JSON_FALSE) {
    return false;
  }
  *wait_for_ready = field->type == GRPC_JSON_TRUE ? WAIT_FOR_READY_TRUE
                                                  : WAIT_FOR_READY_FALSE;
  return true;
}

// Parses a JSON field of the form generated for a google.proto.Duration
// proto message.
bool parse_duration(grpc_json* field, grpc_millis* duration) {
  if (field->type != GRPC_JSON_STRING) return false;
  size_t len = strlen(field->value);
  if (field->value[len - 1] != 's') return false;
  char* buf = gpr_strdup(field->value);
  buf[len - 1] = '\0';  // Remove trailing 's'.
  char* decimal_point = strchr(buf, '.');
  int nanos = 0;
  if (decimal_point != NULL) {
    *decimal_point = '\0';
    nanos = gpr_parse_nonnegative_int(decimal_point + 1);
    if (nanos == -1) {
      gpr_free(buf);
      return false;
    }
    int num_digits = (int)strlen(decimal_point + 1);
    if (num_digits > 9) {  // We don't accept greater precision than nanos.
      gpr_free(buf);
      return false;
    }
    for (int i = 0; i < (9 - num_digits); ++i) {
      nanos *= 10;
    }
  }
  int seconds = decimal_point == buf ? 0 : gpr_parse_nonnegative_int(buf);
  gpr_free(buf);
  if (seconds == -1) return false;
  *duration = seconds * GPR_MS_PER_SEC + nanos / GPR_NS_PER_MS;
  return true;
}

bool parse_retry_policy(grpc_json* field, retry_policy_params* retry_policy) {
  if (field->type != GRPC_JSON_OBJECT) return false;
  for (grpc_json* sub_field = field->child; sub_field != NULL;
       sub_field = sub_field->next) {
    if (sub_field->key == NULL) return false;
    if (strcmp(sub_field->key, "maxAttempts") == 0) {
      if (retry_policy->max_attempts != 0) return false;  // Duplicate.
      if (sub_field->type != GRPC_JSON_NUMBER) return false;
      retry_policy->max_attempts = gpr_parse_nonnegative_int(sub_field->value);
      if (retry_policy->max_attempts <= 1) return false;
      if (retry_policy->max_attempts > MAX_MAX_RETRY_ATTEMPTS) {
        gpr_log(GPR_INFO,
                "service config: clamped retryPolicy.maxAttempts at %d",
                MAX_MAX_RETRY_ATTEMPTS);
        retry_policy->max_attempts = MAX_MAX_RETRY_ATTEMPTS;
      }
    } else if (strcmp(sub_field->key, "initialBackoff") == 0) {
      if (retry_policy->initial_backoff > 0) return false;  // Duplicate.
      if (!parse_duration(sub_field, &retry_policy->initial_backoff)) {
        return false;
      }
      if (retry_policy->initial_backoff == 0) return false;
    } else if (strcmp(sub_field->key, "maxBackoff") == 0) {
      if (retry_policy->max_backoff > 0) return false;  // Duplicate.
      if (!parse_duration(sub_field, &retry_policy->max_backoff)) return false;
      if (retry_policy->max_backoff == 0) return false;
    } else if (strcmp(sub_field->key, "backoffMultiplier") == 0) {
      if (retry_policy->backoff_multiplier != 0) return false;  // Duplicate.
      if (sub_field->type != GRPC_JSON_NUMBER) return false;
      if (sscanf(sub_field->value, "%f", &retry_policy->backoff_multiplier) !=
          1) {
        return false;
      }
      if (retry_policy->backoff_multiplier <= 0) return false;
    } else if (strcmp(sub_field->key, "retryableStatusCodes") == 0) {
      if (!retry_policy->retryable_status_codes.Empty()) {
        return false;  // Duplicate.
      }
      if (sub_field->type != GRPC_JSON_ARRAY) return false;
      for (grpc_json* element = sub_field->child; element != NULL;
           element = element->next) {
        if (element->type != GRPC_JSON_STRING) return false;
        grpc_status_code status;
        if (!grpc_status_from_string(element->value, &status)) return false;
        retry_policy->retryable_status_codes.Add(status);
      }
      if (retry_policy->retryable_status_codes.Empty()) return false;
    }
  }
  // Make sure required fields are set.
  if (retry_policy->max_attempts == 0 || retry_policy->initial_backoff == 0 ||
      retry_policy->max_backoff == 0 || retry_policy->backoff_multiplier == 0 ||
      retry_policy->retryable_status_codes.Empty()) {
    return false;
  }
  return true;
}

}  // namespace

void* method_parameters_create_from_json(const grpc_json* json,
                                         void* user_data) {
  const bool enable_retries = (bool)user_data;
  wait_for_ready_value wait_for_ready = WAIT_FOR_READY_UNSET;
  grpc_millis timeout = 0;
  retry_policy_params* retry_policy = NULL;
  method_parameters* value = NULL;
  for (grpc_json* field = json->child; field != NULL; field = field->next) {
    if (field->key == NULL) continue;
    if (strcmp(field->key, "waitForReady") == 0) {
      if (wait_for_ready != WAIT_FOR_READY_UNSET) goto error;  // Duplicate.
      if (!parse_wait_for_ready(field, &wait_for_ready)) goto error;
    } else if (strcmp(field->key, "timeout") == 0) {
      if (timeout > 0) return NULL;  // Duplicate.
      if (!parse_duration(field, &timeout)) goto error;
    } else if (strcmp(field->key, "retryPolicy") == 0 && enable_retries) {
      if (retry_policy != NULL) goto error;  // Duplicate.
      retry_policy = (retry_policy_params*)gpr_zalloc(sizeof(*retry_policy));
      if (!parse_retry_policy(field, retry_policy)) goto error;
    }
  }
  value = (method_parameters*)gpr_malloc(sizeof(method_parameters));
  gpr_ref_init(&value->refs, 1);
  value->timeout = timeout;
  value->wait_for_ready = wait_for_ready;
  value->retry_policy = retry_policy;
  return value;
error:
  retry_policy_params_free(retry_policy);
  return NULL;
}

}  // namespace internal
}  // namespace grpc_core
