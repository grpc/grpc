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

#ifndef CENSUS_RESOURCE_ID_H
#define CENSUS_RESOURCE_ID_H

/* Resource ID's used for census measurements. */
#define RESOURCE_INVALID 0             /* Make default be invalid. */
#define RESOURCE_RPC_CLIENT_REQUESTS 1 /* Count of client requests sent. */
#define RESOURCE_RPC_SERVER_REQUESTS 2 /* Count of server requests sent. */
#define RESOURCE_RPC_CLIENT_ERRORS 3   /* Client error counts. */
#define RESOURCE_RPC_SERVER_ERRORS 4   /* Server error counts. */
#define RESOURCE_RPC_CLIENT_LATENCY 5  /* Client side request latency. */
#define RESOURCE_RPC_SERVER_LATENCY 6  /* Server side request latency. */
#define RESOURCE_RPC_CLIENT_CPU 7      /* Client CPU processing time. */
#define RESOURCE_RPC_SERVER_CPU 8      /* Server CPU processing time. */

#endif /* CENSUS_RESOURCE_ID_H */
