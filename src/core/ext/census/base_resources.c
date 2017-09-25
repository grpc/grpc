/*
 * Copyright 2016 gRPC authors.
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
  resource r = {(char *)"client_rpc_latency",             // name
                (char *)"Client RPC latency in seconds",  // description
                0,                                        // prefix
                1,                                        // n_numerators
                &numerator,                               // numerators
                0,                                        // n_denominators
                NULL};                                    // denominators
  define_resource(&r);
  r = (resource){(char *)"server_rpc_latency",             // name
                 (char *)"Server RPC latency in seconds",  // description
                 0,                                        // prefix
                 1,                                        // n_numerators
                 &numerator,                               // numerators
                 0,                                        // n_denominators
                 NULL};                                    // denominators
  define_resource(&r);
}
