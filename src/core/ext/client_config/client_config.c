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

#include "src/core/ext/client_config/client_config.h"

#include <string.h>

#include <grpc/support/alloc.h>

struct grpc_client_config {
  gpr_refcount refs;
  grpc_lb_policy *lb_policy;
};

grpc_client_config *grpc_client_config_create() {
  grpc_client_config *c = gpr_malloc(sizeof(*c));
  memset(c, 0, sizeof(*c));
  gpr_ref_init(&c->refs, 1);
  return c;
}

void grpc_client_config_ref(grpc_client_config *c) { gpr_ref(&c->refs); }

void grpc_client_config_unref(grpc_exec_ctx *exec_ctx, grpc_client_config *c) {
  if (gpr_unref(&c->refs)) {
    if (c->lb_policy != NULL) {
      GRPC_LB_POLICY_UNREF(exec_ctx, c->lb_policy, "client_config");
    }
    gpr_free(c);
  }
}

void grpc_client_config_set_lb_policy(grpc_client_config *c,
                                      grpc_lb_policy *lb_policy) {
  GPR_ASSERT(c->lb_policy == NULL);
  if (lb_policy) {
    GRPC_LB_POLICY_REF(lb_policy, "client_config");
  }
  c->lb_policy = lb_policy;
}

grpc_lb_policy *grpc_client_config_get_lb_policy(grpc_client_config *c) {
  return c->lb_policy;
}
