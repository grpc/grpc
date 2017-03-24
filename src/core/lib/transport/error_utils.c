/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/transport/error_utils.h"

#include "src/core/lib/iomgr/error_internal.h"
#include "src/core/lib/transport/status_conversion.h"

static grpc_error *recursively_find_error_with_field(grpc_error *error,
                                                     grpc_error_ints which) {
  // If the error itself has a status code, return it.
  if (grpc_error_get_int(error, which, NULL)) {
    return error;
  }
  if (grpc_error_is_special(error)) return NULL;
  // Otherwise, search through its children.
  uint8_t slot = error->first_err;
  while (slot != UINT8_MAX) {
    grpc_linked_error *lerr = (grpc_linked_error *)(error->arena + slot);
    grpc_error *result = recursively_find_error_with_field(lerr->err, which);
    if (result) return result;
    slot = lerr->next;
  }
  return NULL;
}

void grpc_error_get_status(grpc_error *error, gpr_timespec deadline,
                           grpc_status_code *code, grpc_slice *slice,
                           grpc_http2_error_code *http_error) {
  // Start with the parent error and recurse through the tree of children
  // until we find the first one that has a status code.
  grpc_error *found_error =
      recursively_find_error_with_field(error, GRPC_ERROR_INT_GRPC_STATUS);
  if (found_error == NULL) {
    /// If no grpc-status exists, retry through the tree to find a http2 error
    /// code
    found_error =
        recursively_find_error_with_field(error, GRPC_ERROR_INT_HTTP2_ERROR);
  }

  // If we found an error with a status code above, use that; otherwise,
  // fall back to using the parent error.
  if (found_error == NULL) found_error = error;

  grpc_status_code status = GRPC_STATUS_UNKNOWN;
  intptr_t integer;
  if (grpc_error_get_int(found_error, GRPC_ERROR_INT_GRPC_STATUS, &integer)) {
    status = (grpc_status_code)integer;
  } else if (grpc_error_get_int(found_error, GRPC_ERROR_INT_HTTP2_ERROR,
                                &integer)) {
    status = grpc_http2_error_to_grpc_status((grpc_http2_error_code)integer,
                                             deadline);
  }
  if (code != NULL) *code = status;

  if (http_error != NULL) {
    if (grpc_error_get_int(found_error, GRPC_ERROR_INT_HTTP2_ERROR, &integer)) {
      *http_error = (grpc_http2_error_code)integer;
    } else if (grpc_error_get_int(found_error, GRPC_ERROR_INT_GRPC_STATUS,
                                  &integer)) {
      *http_error = grpc_status_to_http2_error((grpc_status_code)integer);
    } else {
      *http_error = found_error == GRPC_ERROR_NONE ? GRPC_HTTP2_NO_ERROR
                                                   : GRPC_HTTP2_INTERNAL_ERROR;
    }
  }

  // If the error has a status message, use it.  Otherwise, fall back to
  // the error description.
  if (slice != NULL) {
    if (!grpc_error_get_str(found_error, GRPC_ERROR_STR_GRPC_MESSAGE, slice)) {
      if (!grpc_error_get_str(found_error, GRPC_ERROR_STR_DESCRIPTION, slice)) {
        *slice = grpc_slice_from_static_string("unknown error");
      }
    }
  }

  if (found_error == NULL) found_error = error;
}

bool grpc_error_has_clear_grpc_status(grpc_error *error) {
  if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, NULL)) {
    return true;
  }
  uint8_t slot = error->first_err;
  while (slot != UINT8_MAX) {
    grpc_linked_error *lerr = (grpc_linked_error *)(error->arena + slot);
    if (grpc_error_has_clear_grpc_status(lerr->err)) {
      return true;
    }
    slot = lerr->next;
  }
  return false;
}
