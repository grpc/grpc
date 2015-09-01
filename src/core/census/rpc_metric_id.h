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

#ifndef CENSUS_RPC_METRIC_ID_H
#define CENSUS_RPC_METRIC_ID_H

/* Metric ID's used for RPC measurements. */
/* Count of client requests sent. */
#define CENSUS_METRIC_RPC_CLIENT_REQUESTS ((gpr_uint32)0)
/* Count of server requests sent. */
#define CENSUS_METRIC_RPC_SERVER_REQUESTS ((gpr_uint32)1)
/* Client error counts. */
#define CENSUS_METRIC_RPC_CLIENT_ERRORS ((gpr_uint32)2)
/* Server error counts. */
#define CENSUS_METRIC_RPC_SERVER_ERRORS ((gpr_uint32)3)
/* Client side request latency. */
#define CENSUS_METRIC_RPC_CLIENT_LATENCY ((gpr_uint32)4)
/* Server side request latency. */
#define CENSUS_METRIC_RPC_SERVER_LATENCY ((gpr_uint32)5)

#endif /* CENSUS_RPC_METRIC_ID_H */
