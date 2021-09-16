/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"

void grpc_call_details_init(grpc_call_details* details) {
  GRPC_API_TRACE("grpc_call_details_init(details=%p)", 1, (details));
  details->method = grpc_empty_slice();
  details->host = grpc_empty_slice();
}

void grpc_call_details_destroy(grpc_call_details* details) {
  GRPC_API_TRACE("grpc_call_details_destroy(details=%p)", 1, (details));
  grpc_core::ExecCtx exec_ctx;
  grpc_slice_unref_internal(details->method);
  grpc_slice_unref_internal(details->host);
}
