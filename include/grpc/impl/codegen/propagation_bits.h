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

#ifndef GRPC_IMPL_CODEGEN_PROPAGATION_BITS_H
#define GRPC_IMPL_CODEGEN_PROPAGATION_BITS_H

#include <grpc/impl/codegen/port_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Propagation bits: this can be bitwise or-ed to form propagation_mask for
 * grpc_call */
/** Propagate deadline */
#define GRPC_PROPAGATE_DEADLINE ((uint32_t)1)
/** Propagate census context */
#define GRPC_PROPAGATE_CENSUS_STATS_CONTEXT ((uint32_t)2)
#define GRPC_PROPAGATE_CENSUS_TRACING_CONTEXT ((uint32_t)4)
/** Propagate cancellation */
#define GRPC_PROPAGATE_CANCELLATION ((uint32_t)8)

/** Default propagation mask: clients of the core API are encouraged to encode
   deltas from this in their implementations... ie write:
   GRPC_PROPAGATE_DEFAULTS & ~GRPC_PROPAGATE_DEADLINE to disable deadline
   propagation. Doing so gives flexibility in the future to define new
   propagation types that are default inherited or not. */
#define GRPC_PROPAGATE_DEFAULTS                                                \
  ((uint32_t)((                                                                \
      0xffff | GRPC_PROPAGATE_DEADLINE | GRPC_PROPAGATE_CENSUS_STATS_CONTEXT | \
      GRPC_PROPAGATE_CENSUS_TRACING_CONTEXT | GRPC_PROPAGATE_CANCELLATION)))

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_PROPAGATION_BITS_H */
