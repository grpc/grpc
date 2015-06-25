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

#ifndef GRPC_INTERNAL_CORE_CLIENT_CONFIG_LB_POLICY_H
#define GRPC_INTERNAL_CORE_CLIENT_CONFIG_LB_POLICY_H

#include "src/core/client_config/subchannel.h"

/** A load balancing policy: specified by a vtable and a struct (which
    is expected to be extended to contain some parameters) */
typedef struct grpc_lb_policy grpc_lb_policy;
typedef struct grpc_lb_policy_vtable grpc_lb_policy_vtable;

typedef void (*grpc_lb_completion)(void *cb_arg, grpc_subchannel *subchannel,
                                   grpc_status_code status, const char *errmsg);

struct grpc_lb_policy_vtable {
  void (*ref)(grpc_lb_policy *policy);
  void (*unref)(grpc_lb_policy *policy);

  void (*shutdown)(grpc_lb_policy *policy);

  /** implement grpc_lb_policy_pick */
  void (*pick)(grpc_lb_policy *policy, grpc_pollset *pollset,
               grpc_metadata_batch *initial_metadata, grpc_subchannel **target,
               grpc_iomgr_closure *on_complete);
};

void grpc_lb_policy_ref(grpc_lb_policy *policy);
void grpc_lb_policy_unref(grpc_lb_policy *policy);

/** Start shutting down (fail any pending picks) */
void grpc_lb_policy_shutdown(grpc_lb_policy *policy);

/** Given initial metadata in \a initial_metadata, find an appropriate
    target for this rpc, and 'return' it by calling \a on_complete after setting
    \a target.
    Picking can be asynchronous. Any IO should be done under \a pollset. */
void grpc_lb_policy_pick(grpc_lb_policy *policy, grpc_pollset *pollset,
                         grpc_metadata_batch *initial_metadata,
                         grpc_subchannel **target,
                         grpc_iomgr_closure *on_complete);

#endif /* GRPC_INTERNAL_CORE_CONFIG_LB_POLICY_H */
