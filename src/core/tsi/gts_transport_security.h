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

#ifndef GRPC_CORE_TSI_GTS_TRANSPORT_SECURITY_H
#define GRPC_CORE_TSI_GTS_TRANSPORT_SECURITY_H

#include <grpc/grpc.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>

typedef struct gts_shared_resource {
  gpr_thd_id gts_thread_id;
  grpc_channel* gts_channel;
  grpc_completion_queue* gts_cq;
  gpr_mu gts_mu;
} gts_shared_resource;

/* This method returns the address of gts_shared_resource object shared by all
   TSI handshakes. */
gts_shared_resource** gts_get_shared_resource(void);

/* This method destroys the shared gts_shared_resource object used in GTS
   implementation. */
void gts_destroy(void);

#endif  // GRPC_CORE_TSI_GTS_TRANSPORT_SECURITY_H

