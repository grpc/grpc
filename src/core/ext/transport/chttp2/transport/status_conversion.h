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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STATUS_CONVERSION_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STATUS_CONVERSION_H

#include <grpc/grpc.h>
#include "src/core/ext/transport/chttp2/transport/http2_errors.h"

/* Conversion of grpc status codes to http2 error codes (for RST_STREAM) */
grpc_chttp2_error_code grpc_chttp2_grpc_status_to_http2_error(
    grpc_status_code status);
grpc_status_code grpc_chttp2_http2_error_to_grpc_status(
    grpc_chttp2_error_code error, gpr_timespec deadline);

/* Conversion of HTTP status codes (:status) to grpc status codes */
grpc_status_code grpc_chttp2_http2_status_to_grpc_status(int status);
int grpc_chttp2_grpc_status_to_http2_status(grpc_status_code status);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STATUS_CONVERSION_H */
