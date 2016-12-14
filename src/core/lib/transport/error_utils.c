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

static grpc_error *recursively_find_error_with_status(grpc_error *error,
                                                      intptr_t *status) {
  // If the error itself has a status code, return it.
  if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, status)) {
    return error;
  }
  if (grpc_error_is_special(error)) return NULL;
  // Otherwise, search through its children.
  intptr_t key = 0;
  while (true) {
    grpc_error *child_error = gpr_avl_get(error->errs, (void *)key++);
    if (child_error == NULL) break;
    grpc_error *result =
        recursively_find_error_with_status(child_error, status);
    if (result != NULL) return result;
  }
  return NULL;
}

void grpc_error_get_status(grpc_error *error, grpc_status_code *code,
                           const char **msg) {
  // Populate code.
  // Start with the parent error and recurse through the tree of children
  // until we find the first one that has a status code.
  intptr_t status = GRPC_STATUS_UNKNOWN;  // Default in case we don't find one.
  grpc_error *found_error = recursively_find_error_with_status(error, &status);
  *code = (grpc_status_code)status;
  // Now populate msg.
  // If we found an error with a status code above, use that; otherwise,
  // fall back to using the parent error.
  if (found_error == NULL) found_error = error;
  // If the error has a status message, use it.  Otherwise, fall back to
  // the error description.
  *msg = grpc_error_get_str(found_error, GRPC_ERROR_STR_GRPC_MESSAGE);
  if (*msg == NULL && status != GRPC_STATUS_OK) {
    *msg = grpc_error_get_str(found_error, GRPC_ERROR_STR_DESCRIPTION);
    if (*msg == NULL) *msg = "unknown error";  // Just in case.
  }
}

bool grpc_error_has_clear_grpc_status(grpc_error *error) {
  if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, NULL)) {
    return true;
  }
  intptr_t key = 0;
  while (true) {
    grpc_error *child_error = gpr_avl_get(error->errs, (void *)key++);
    if (child_error == NULL) break;
    if (grpc_error_has_clear_grpc_status(child_error)) {
      return true;
    }
  }
  return false;
}
