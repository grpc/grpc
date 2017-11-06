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

#ifndef GRPC_CORE_LIB_IOMGR_IOMGR_H
#define GRPC_CORE_LIB_IOMGR_IOMGR_H

#include <grpc/impl/codegen/exec_ctx_fwd.h>
#include "src/core/lib/iomgr/port.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initializes the iomgr. */
void grpc_iomgr_init(grpc_exec_ctx* exec_ctx);

/** Starts any background threads for iomgr. */
void grpc_iomgr_start(grpc_exec_ctx* exec_ctx);

/** Signals the intention to shutdown the iomgr. Expects to be able to flush
 * exec_ctx. */
void grpc_iomgr_shutdown(grpc_exec_ctx* exec_ctx);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_IOMGR_IOMGR_H */
