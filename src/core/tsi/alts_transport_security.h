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

#ifndef GRPC_CORE_TSI_ALTS_TRANSPORT_SECURITY_H
#define GRPC_CORE_TSI_ALTS_TRANSPORT_SECURITY_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/thd.h"

typedef struct alts_shared_resource {
  grpc_core::Thread thread;
  grpc_channel* channel;
  grpc_completion_queue* cq;
  gpr_mu mu;
  gpr_cv cq_cv;
  gpr_cv res_cv;
  bool can_destroy_resource;
  bool is_cq_drained;
  gpr_refcount refcount;
} alts_shared_resource;

/* This method returns the address of alts_shared_resource object shared by all
 *    TSI handshakes. */
alts_shared_resource* alts_get_shared_resource(void);

/* This method signals the thread that invokes grpc_tsi_alts_shutdown() to
 * continue with destroying the cq as a part of shutdown process. */
void grpc_tsi_alts_signal_for_cq_destroy(void);

/** This method add a ref to the alts_shared_resource object. */
void grpc_tsi_g_alts_resource_ref();

/** This method removes a ref from the alts_shared_resource object. */
void grpc_tsi_g_alts_resource_unref();

#endif /* GRPC_CORE_TSI_ALTS_TRANSPORT_SECURITY_H */
