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

#ifndef GRPC_CORE_EXT_CLIENT_CONFIG_LB_POLICY_FACTORY_H
#define GRPC_CORE_EXT_CLIENT_CONFIG_LB_POLICY_FACTORY_H

#include "src/core/ext/client_config/client_channel_factory.h"
#include "src/core/ext/client_config/lb_policy.h"
#include "src/core/lib/iomgr/resolve_address.h"

#include "src/core/lib/iomgr/exec_ctx.h"

typedef struct grpc_lb_policy_factory grpc_lb_policy_factory;
typedef struct grpc_lb_policy_factory_vtable grpc_lb_policy_factory_vtable;

/** grpc_lb_policy provides grpc_client_config objects to grpc_channel
    objects */
struct grpc_lb_policy_factory {
  const grpc_lb_policy_factory_vtable *vtable;
};

typedef struct grpc_lb_policy_args {
  grpc_resolved_addresses *addresses;
  grpc_client_channel_factory *client_channel_factory;
} grpc_lb_policy_args;

struct grpc_lb_policy_factory_vtable {
  void (*ref)(grpc_lb_policy_factory *factory);
  void (*unref)(grpc_lb_policy_factory *factory);

  /** Implementation of grpc_lb_policy_factory_create_lb_policy */
  grpc_lb_policy *(*create_lb_policy)(grpc_exec_ctx *exec_ctx,
                                      grpc_lb_policy_factory *factory,
                                      grpc_lb_policy_args *args);

  /** Name for the LB policy this factory implements */
  const char *name;
};

void grpc_lb_policy_factory_ref(grpc_lb_policy_factory *factory);
void grpc_lb_policy_factory_unref(grpc_lb_policy_factory *factory);

/** Create a lb_policy instance. */
grpc_lb_policy *grpc_lb_policy_factory_create_lb_policy(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy_factory *factory,
    grpc_lb_policy_args *args);

#endif /* GRPC_CORE_EXT_CLIENT_CONFIG_LB_POLICY_FACTORY_H */
