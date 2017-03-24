/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/lib/surface/completion_queue_factory.h"
#include "src/core/lib/surface/completion_queue.h"

#include <grpc/support/log.h>

/* TODO (sreek) - Currently this does not use the attributes arg. This will be
   added in a future PR */
static grpc_completion_queue* default_create(
    const grpc_completion_queue_factory* factory,
    const grpc_completion_queue_attributes* attributes) {
  return grpc_completion_queue_create(NULL);
}

static grpc_completion_queue_factory_vtable default_vtable = {default_create};

static const grpc_completion_queue_factory g_default_cq_factory = {
    "Default Factory", NULL, &default_vtable};

const grpc_completion_queue_factory* grpc_completion_queue_factory_lookup(
    const grpc_completion_queue_attributes* attributes) {
  /* As we add more fields to grpc_completion_queue_attributes, we may have to
     change this assert to:
         GPR_ASSERT (attributes->version >= 1 &&
             attributes->version <= GRPC_CQ_CURRENT_VERSION) */
  GPR_ASSERT(attributes->version == 1);

  /* The default factory can handle version 1 of the attributes structure. We
     may have to change this as more fields are added to the structure */
  return &g_default_cq_factory;
}

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
