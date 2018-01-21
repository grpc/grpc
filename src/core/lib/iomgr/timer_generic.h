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

#ifndef GRPC_CORE_LIB_IOMGR_TIMER_GENERIC_H
#define GRPC_CORE_LIB_IOMGR_TIMER_GENERIC_H

#include <grpc/support/time.h>
#include "src/core/lib/iomgr/exec_ctx.h"

struct grpc_timer
{
  gpr_atm deadline;
  uint32_t heap_index;		/* INVALID_HEAP_INDEX if not in heap */
  bool pending;
  struct grpc_timer *next;
  struct grpc_timer *prev;
  grpc_closure *closure;
#ifndef NDEBUG
  struct grpc_timer *hash_table_next;
#endif
};

#endif /* GRPC_CORE_LIB_IOMGR_TIMER_GENERIC_H */
