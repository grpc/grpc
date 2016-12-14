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

#include "src/core/lib/transport/status_conversion.h"

int grpc_status_to_http2_error(grpc_status_code status) {
  switch (status) {
    case GRPC_STATUS_OK:
      return GRPC_HTTP2_NO_ERROR;
    case GRPC_STATUS_CANCELLED:
      return GRPC_HTTP2_CANCEL;
    case GRPC_STATUS_DEADLINE_EXCEEDED:
      return GRPC_HTTP2_CANCEL;
    case GRPC_STATUS_RESOURCE_EXHAUSTED:
      return GRPC_HTTP2_ENHANCE_YOUR_CALM;
    case GRPC_STATUS_PERMISSION_DENIED:
      return GRPC_HTTP2_INADEQUATE_SECURITY;
    case GRPC_STATUS_UNAVAILABLE:
      return GRPC_HTTP2_REFUSED_STREAM;
    default:
      return GRPC_HTTP2_INTERNAL_ERROR;
  }
}

grpc_status_code grpc_http2_error_to_grpc_status(grpc_http2_error_code error,
                                                 gpr_timespec deadline) {
  switch (error) {
    case GRPC_HTTP2_NO_ERROR:
      /* should never be received */
      return GRPC_STATUS_INTERNAL;
    case GRPC_HTTP2_CANCEL:
      /* http2 cancel translates to STATUS_CANCELLED iff deadline hasn't been
       * exceeded */
      return gpr_time_cmp(gpr_now(deadline.clock_type), deadline) >= 0
                 ? GRPC_STATUS_DEADLINE_EXCEEDED
                 : GRPC_STATUS_CANCELLED;
    case GRPC_HTTP2_ENHANCE_YOUR_CALM:
      return GRPC_STATUS_RESOURCE_EXHAUSTED;
    case GRPC_HTTP2_INADEQUATE_SECURITY:
      return GRPC_STATUS_PERMISSION_DENIED;
    case GRPC_HTTP2_REFUSED_STREAM:
      return GRPC_STATUS_UNAVAILABLE;
    default:
      return GRPC_STATUS_INTERNAL;
  }
}

grpc_status_code grpc_http2_status_to_grpc_status(int status) {
  switch (status) {
    /* these HTTP2 status codes are called out explicitly in status.proto */
    case 200:
      return GRPC_STATUS_OK;
    case 400:
      return GRPC_STATUS_INVALID_ARGUMENT;
    case 401:
      return GRPC_STATUS_UNAUTHENTICATED;
    case 403:
      return GRPC_STATUS_PERMISSION_DENIED;
    case 404:
      return GRPC_STATUS_NOT_FOUND;
    case 409:
      return GRPC_STATUS_ABORTED;
    case 412:
      return GRPC_STATUS_FAILED_PRECONDITION;
    case 429:
      return GRPC_STATUS_RESOURCE_EXHAUSTED;
    case 499:
      return GRPC_STATUS_CANCELLED;
    case 500:
      return GRPC_STATUS_UNKNOWN;
    case 501:
      return GRPC_STATUS_UNIMPLEMENTED;
    case 503:
      return GRPC_STATUS_UNAVAILABLE;
    case 504:
      return GRPC_STATUS_DEADLINE_EXCEEDED;
    /* everything else is unknown */
    default:
      return GRPC_STATUS_UNKNOWN;
  }
}

int grpc_status_to_http2_status(grpc_status_code status) { return 200; }
