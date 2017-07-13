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

#ifndef GRPC_CORE_LIB_IOMGR_TIMER_UV_H
#define GRPC_CORE_LIB_IOMGR_TIMER_UV_H

#include "src/core/lib/iomgr/exec_ctx.h"

struct grpc_timer {
  grpc_closure *closure;
  /* This is actually a uv_timer_t*, but we want to keep platform-specific
     types out of headers */
  void *uv_timer;
  int pending;
};

#endif /* GRPC_CORE_LIB_IOMGR_TIMER_UV_H */
