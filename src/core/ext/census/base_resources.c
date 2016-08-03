/*
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

#include "src/core/ext/census/base_resources.h"

#include <stdio.h>
#include <string.h>

#include <grpc/census.h>
#include <grpc/support/log.h>

#include "src/core/ext/census/resource.h"

// Add base RPC resource definitions for use by RPC runtime.
//
// TODO(aveitch): All of these are currently hardwired definitions encoded in
// the code in this file. These should be converted to use an external
// configuration mechanism, in which these resources are defined in a text
// file, which is compiled to .pb format and read by still-to-be-written
// configuration functions.

// Define all base resources. This should be called by census initialization.
void define_base_resources() {
  google_census_Resource_BasicUnit numerator =
      google_census_Resource_BasicUnit_SECS;
  resource r = {"client_rpc_latency",             // name
                "Client RPC latency in seconds",  // description
                0,                                // prefix
                1,                                // n_numerators
                &numerator,                       // numerators
                0,                                // n_denominators
                NULL};                            // denominators
  define_resource(&r);
  r = (resource){"server_rpc_latency",             // name
                 "Server RPC latency in seconds",  // description
                 0,                                // prefix
                 1,                                // n_numerators
                 &numerator,                       // numerators
                 0,                                // n_denominators
                 NULL};                            // denominators
  define_resource(&r);
}
