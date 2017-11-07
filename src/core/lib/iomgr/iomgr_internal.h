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

#ifndef GRPC_CORE_LIB_IOMGR_IOMGR_INTERNAL_H
#define GRPC_CORE_LIB_IOMGR_IOMGR_INTERNAL_H

#include <stdbool.h>

#include "src/core/lib/iomgr/iomgr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grpc_iomgr_object {
  char* name;
  struct grpc_iomgr_object* next;
  struct grpc_iomgr_object* prev;
} grpc_iomgr_object;

void grpc_iomgr_register_object(grpc_iomgr_object* obj, const char* name);
void grpc_iomgr_unregister_object(grpc_iomgr_object* obj);

void grpc_iomgr_platform_init(void);
/** flush any globally queued work from iomgr */
void grpc_iomgr_platform_flush(void);
/** tear down all platform specific global iomgr structures */
void grpc_iomgr_platform_shutdown(void);

bool grpc_iomgr_abort_on_leaks(void);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_IOMGR_IOMGR_INTERNAL_H */
