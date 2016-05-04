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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/pops.h"

struct grpc_pops {
  union {
    grpc_pollset *pollset;
    grpc_pollset_set *pollset_set;
  } pops;
  enum pops_tag { POLLSET, POLLSET_SET } tag;
};

grpc_pops *grpc_pops_create_from_pollset_set(grpc_pollset_set *pollset_set) {
  grpc_pops *pops = gpr_malloc(sizeof(grpc_pops));
  pops->pops.pollset_set = pollset_set;
  pops->tag = POLLSET_SET;
  return pops;
}

grpc_pops *grpc_pops_create_from_pollset(grpc_pollset *pollset) {
  grpc_pops *pops = gpr_malloc(sizeof(grpc_pops));
  pops->pops.pollset = pollset;
  pops->tag = POLLSET;
  return pops;
}

void grpc_pops_destroy(grpc_pops *pops) { gpr_free(pops); }

grpc_pollset *grpc_pops_pollset(grpc_pops *pops) {
  if (pops->tag == POLLSET) {
    return pops->pops.pollset;
  }
  return NULL;
}

grpc_pollset_set *grpc_pops_pollset_set(grpc_pops *pops) {
  if (pops->tag == POLLSET_SET) {
    return pops->pops.pollset_set;
  }
  return NULL;
}

void grpc_pops_add_to_pollset_set(grpc_exec_ctx *exec_ctx, grpc_pops *pops,
                                  grpc_pollset_set *pss_dst) {
  if (pops->tag == POLLSET) {
    GPR_ASSERT(pops->pops.pollset != NULL);
    grpc_pollset_set_add_pollset(exec_ctx, pss_dst, pops->pops.pollset);
  } else if (pops->tag == POLLSET_SET) {
    GPR_ASSERT(pops->pops.pollset_set != NULL);
    grpc_pollset_set_add_pollset_set(exec_ctx, pss_dst, pops->pops.pollset_set);
  } else {
    gpr_log(GPR_ERROR, "Invalid grpc_pops tag '%d'", pops->tag);
    abort();
  }
}

void grpc_pops_del_to_pollset_set(grpc_exec_ctx *exec_ctx, grpc_pops *pops,
                                  grpc_pollset_set *pss_dst) {
  if (pops->tag == POLLSET) {
    GPR_ASSERT(pops->pops.pollset != NULL);
    grpc_pollset_set_del_pollset(exec_ctx, pss_dst, pops->pops.pollset);
  } else if (pops->tag == POLLSET_SET) {
    GPR_ASSERT(pops->pops.pollset_set != NULL);
    grpc_pollset_set_del_pollset_set(exec_ctx, pss_dst, pops->pops.pollset_set);
  } else {
    gpr_log(GPR_ERROR, "Invalid grpc_pops tag '%d'", pops->tag);
    abort();
  }
}
