/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/transport/error_utils.h"

#include <grpc/support/string_util.h>
#include "src/core/lib/iomgr/error_internal.h"
#include "src/core/lib/transport/status_conversion.h"

static grpc_error* recursively_find_error_with_field(grpc_error* error,
                                                     grpc_error_ints which) {
  // If the error itself has a status code, return it.
  if (grpc_error_get_int(error, which, nullptr)) {
    return error;
  }
  if (grpc_error_is_special(error)) return nullptr;
  // Otherwise, search through its children.
  uint8_t slot = error->first_err;
  while (slot != UINT8_MAX) {
    grpc_linked_error* lerr =
        reinterpret_cast<grpc_linked_error*>(error->arena + slot);
    grpc_error* result = recursively_find_error_with_field(lerr->err, which);
    if (result) return result;
    slot = lerr->next;
  }
  return nullptr;
}

void grpc_error_get_status(grpc_error* error, grpc_millis deadline,
                           grpc_status_code* code, grpc_slice* slice,
                           grpc_http2_error_code* http_error,
                           const char** error_string) {
  // Start with the parent error and recurse through the tree of children
  // until we find the first one that has a status code.
  grpc_error* found_error =
      recursively_find_error_with_field(error, GRPC_ERROR_INT_GRPC_STATUS);
  if (found_error == nullptr) {
    /// If no grpc-status exists, retry through the tree to find a http2 error
    /// code
    found_error =
        recursively_find_error_with_field(error, GRPC_ERROR_INT_HTTP2_ERROR);
  }

  // If we found an error with a status code above, use that; otherwise,
  // fall back to using the parent error.
  if (found_error == nullptr) found_error = error;

  grpc_status_code status = GRPC_STATUS_UNKNOWN;
  intptr_t integer;
  if (grpc_error_get_int(found_error, GRPC_ERROR_INT_GRPC_STATUS, &integer)) {
    status = static_cast<grpc_status_code>(integer);
  } else if (grpc_error_get_int(found_error, GRPC_ERROR_INT_HTTP2_ERROR,
                                &integer)) {
    status = grpc_http2_error_to_grpc_status(
        static_cast<grpc_http2_error_code>(integer), deadline);
  }
  if (code != nullptr) *code = status;

  if (error_string != nullptr && status != GRPC_STATUS_OK) {
    *error_string = gpr_strdup(grpc_error_string(error));
  }

  if (http_error != nullptr) {
    if (grpc_error_get_int(found_error, GRPC_ERROR_INT_HTTP2_ERROR, &integer)) {
      *http_error = static_cast<grpc_http2_error_code>(integer);
    } else if (grpc_error_get_int(found_error, GRPC_ERROR_INT_GRPC_STATUS,
                                  &integer)) {
      *http_error =
          grpc_status_to_http2_error(static_cast<grpc_status_code>(integer));
    } else {
      *http_error = found_error == GRPC_ERROR_NONE ? GRPC_HTTP2_NO_ERROR
                                                   : GRPC_HTTP2_INTERNAL_ERROR;
    }
  }

  // If the error has a status message, use it.  Otherwise, fall back to
  // the error description.
  if (slice != nullptr) {
    if (!grpc_error_get_str(found_error, GRPC_ERROR_STR_GRPC_MESSAGE, slice)) {
      if (!grpc_error_get_str(found_error, GRPC_ERROR_STR_DESCRIPTION, slice)) {
        *slice = grpc_slice_from_static_string("unknown error");
      }
    }
  }
}

bool grpc_error_has_clear_grpc_status(grpc_error* error) {
  if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, nullptr)) {
    return true;
  }
  uint8_t slot = error->first_err;
  while (slot != UINT8_MAX) {
    grpc_linked_error* lerr =
        reinterpret_cast<grpc_linked_error*>(error->arena + slot);
    if (grpc_error_has_clear_grpc_status(lerr->err)) {
      return true;
    }
    slot = lerr->next;
  }
  return false;
}
