/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/completion_queue_factory.h"

#include <grpc/support/log.h>

/*
 * == Default completion queue factory implementation ==
 */

static grpc_completion_queue* default_create(
    const grpc_completion_queue_factory* factory,
    const grpc_completion_queue_attributes* attr) {
  return grpc_completion_queue_create_internal(attr->cq_completion_type,
                                               attr->cq_polling_type);
}

static grpc_completion_queue_factory_vtable default_vtable = {default_create};

static const grpc_completion_queue_factory g_default_cq_factory = {
    "Default Factory", nullptr, &default_vtable};

/*
 * == Completion queue factory APIs
 */

const grpc_completion_queue_factory* grpc_completion_queue_factory_lookup(
    const grpc_completion_queue_attributes* attributes) {
  GPR_ASSERT(attributes->version >= 1 &&
             attributes->version <= GRPC_CQ_CURRENT_VERSION);

  /* The default factory can handle version 1 of the attributes structure. We
     may have to change this as more fields are added to the structure */
  return &g_default_cq_factory;
}

/*
 * == Completion queue creation APIs ==
 */

grpc_completion_queue* grpc_completion_queue_create_for_next(void* reserved) {
  GPR_ASSERT(!reserved);
  grpc_completion_queue_attributes attr = {1, GRPC_CQ_NEXT,
                                           GRPC_CQ_DEFAULT_POLLING};
  return g_default_cq_factory.vtable->create(&g_default_cq_factory, &attr);
}

grpc_completion_queue* grpc_completion_queue_create_for_pluck(void* reserved) {
  GPR_ASSERT(!reserved);
  grpc_completion_queue_attributes attr = {1, GRPC_CQ_PLUCK,
                                           GRPC_CQ_DEFAULT_POLLING};
  return g_default_cq_factory.vtable->create(&g_default_cq_factory, &attr);
}

grpc_completion_queue* grpc_completion_queue_create(
    const grpc_completion_queue_factory* factory,
    const grpc_completion_queue_attributes* attr, void* reserved) {
  GPR_ASSERT(!reserved);
  return factory->vtable->create(factory, attr);
}
