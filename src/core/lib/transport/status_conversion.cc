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

#include "src/core/lib/transport/status_conversion.h"

#include "src/core/lib/iomgr/exec_ctx.h"

grpc_http2_error_code grpc_status_to_http2_error(grpc_status_code status) {
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

grpc_status_code grpc_http2_error_to_grpc_status(
    grpc_http2_error_code error, grpc_core::Timestamp deadline) {
  switch (error) {
    case GRPC_HTTP2_NO_ERROR:
      /* should never be received */
      return GRPC_STATUS_INTERNAL;
    case GRPC_HTTP2_CANCEL:
      /* http2 cancel translates to STATUS_CANCELLED iff deadline hasn't been
       * exceeded */
      return grpc_core::ExecCtx::Get()->Now() > deadline
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
      return GRPC_STATUS_INTERNAL;
    case 401:
      return GRPC_STATUS_UNAUTHENTICATED;
    case 403:
      return GRPC_STATUS_PERMISSION_DENIED;
    case 404:
      return GRPC_STATUS_UNIMPLEMENTED;
    case 429:
      return GRPC_STATUS_UNAVAILABLE;
    case 502:
      return GRPC_STATUS_UNAVAILABLE;
    case 503:
      return GRPC_STATUS_UNAVAILABLE;
    case 504:
      return GRPC_STATUS_UNAVAILABLE;
    /* everything else is unknown */
    default:
      return GRPC_STATUS_UNKNOWN;
  }
}

int grpc_status_to_http2_status(grpc_status_code /*status*/) { return 200; }
