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

#ifndef GRPC_CORE_LIB_SURFACE_API_TRACE_H
#define GRPC_CORE_LIB_SURFACE_API_TRACE_H

#include <grpc/support/log.h>
#include "src/core/lib/debug/trace.h"

extern grpc_tracer_flag grpc_api_trace;

/* Provide unwrapping macros because we're in C89 and variadic macros weren't
   introduced until C99... */
#define GRPC_API_TRACE_UNWRAP0()
#define GRPC_API_TRACE_UNWRAP1(a) , a
#define GRPC_API_TRACE_UNWRAP2(a, b) , a, b
#define GRPC_API_TRACE_UNWRAP3(a, b, c) , a, b, c
#define GRPC_API_TRACE_UNWRAP4(a, b, c, d) , a, b, c, d
#define GRPC_API_TRACE_UNWRAP5(a, b, c, d, e) , a, b, c, d, e
#define GRPC_API_TRACE_UNWRAP6(a, b, c, d, e, f) , a, b, c, d, e, f
#define GRPC_API_TRACE_UNWRAP7(a, b, c, d, e, f, g) , a, b, c, d, e, f, g
#define GRPC_API_TRACE_UNWRAP8(a, b, c, d, e, f, g, h) , a, b, c, d, e, f, g, h
#define GRPC_API_TRACE_UNWRAP9(a, b, c, d, e, f, g, h, i) \
  , a, b, c, d, e, f, g, h, i
#define GRPC_API_TRACE_UNWRAP10(a, b, c, d, e, f, g, h, i, j) \
  , a, b, c, d, e, f, g, h, i, j

/* Due to the limitations of C89's preprocessor, the arity of the var-arg list
   'nargs' must be specified. */
#define GRPC_API_TRACE(fmt, nargs, args)                      \
  if (GRPC_TRACER_ON(grpc_api_trace)) {                       \
    gpr_log(GPR_INFO, fmt GRPC_API_TRACE_UNWRAP##nargs args); \
  }

#endif /* GRPC_CORE_LIB_SURFACE_API_TRACE_H */
