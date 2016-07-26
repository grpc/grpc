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

/* Census-internal resource definition and manipluation functions. */
#ifndef GRPC_CORE_EXT_CENSUS_RESOURCE_H
#define GRPC_CORE_EXT_CENSUS_RESOURCE_H

#include <grpc/grpc.h>
#include "src/core/ext/census/gen/census.pb.h"

/* Internal representation of a resource. */
typedef struct {
  char *name;
  char *description;
  int32_t prefix;
  int n_numerators;
  google_census_Resource_BasicUnit *numerators;
  int n_denominators;
  google_census_Resource_BasicUnit *denominators;
} resource;

/* Initialize and shutdown the resources subsystem. */
void initialize_resources(void);
void shutdown_resources(void);

/* Add a new resource, given a proposed resource structure. Returns the
   resource ID, or -ve on failure.
   TODO(aveitch): this function exists to support addition of the base
   resources. It should be removed when we have the ability to add resources
   from configuration files. */
int32_t define_resource(const resource *base);

#endif /* GRPC_CORE_EXT_CENSUS_RESOURCE_H */
