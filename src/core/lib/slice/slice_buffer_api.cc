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

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"

void grpc_slice_buffer_destroy(grpc_slice_buffer* sb) {
  grpc_core::ExecCtx exec_ctx;
  grpc_slice_buffer_destroy_internal(sb);
}

void grpc_slice_buffer_reset_and_unref(grpc_slice_buffer* sb) {
  grpc_core::ExecCtx exec_ctx;
  grpc_slice_buffer_reset_and_unref_internal(sb);
}
