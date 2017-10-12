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

#ifndef GRPC_CORE_LIB_SUPPORT_MPMCQ_BOUNDED_H
#define GRPC_CORE_LIB_SUPPORT_MPMCQ_BOUNDED_H

#include <grpc/support/atm.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Multiple-producer Multi-consumer lock free queue, based upon the
// implementation from Dmitry Vyukov here:
// http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue

typedef struct gpr_mpmcq_cell {
  gpr_atm sequence;
  void *data;
} gpr_mpmcq_cell;

// Queue type
typedef struct gpr_mpmcq_bounded {
  gpr_atm head;

  char padding_0[GPR_CACHELINE_SIZE];
  gpr_mpmcq_cell *buffer;
  size_t buffer_mask;

  char padding_1[GPR_CACHELINE_SIZE];
  gpr_atm enqueue_pos;

  char padding_2[GPR_CACHELINE_SIZE];
  gpr_atm dequeue_pos;

  char padding_3[GPR_CACHELINE_SIZE];
} gpr_mpmcq_bounded;

/* Init and destroy queue */
void gpr_mpmcq_bounded_init(gpr_mpmcq_bounded *q, size_t buffer_size);
void gpr_mpmcq_bounded_destroy(gpr_mpmcq_bounded *q);

/* Push and Pop */
bool gpr_mpmcq_bounded_push(gpr_mpmcq_bounded *q, void *data);
bool gpr_mpmcq_bounded_pop(gpr_mpmcq_bounded *q, void **data);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SUPPORT_MPMCQ_BOUNDED_H */
