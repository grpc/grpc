/*
 *
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
