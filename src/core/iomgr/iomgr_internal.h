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

#ifndef GRPC_INTERNAL_CORE_IOMGR_IOMGR_INTERNAL_H
#define GRPC_INTERNAL_CORE_IOMGR_IOMGR_INTERNAL_H

#include "src/core/iomgr/iomgr.h"
#include <grpc/support/sync.h>

typedef struct grpc_iomgr_object {
  char *name;
  struct grpc_iomgr_object *next;
  struct grpc_iomgr_object *prev;
} grpc_iomgr_object;

void grpc_pollset_global_init(void);
void grpc_pollset_global_shutdown(void);

void grpc_iomgr_register_object(grpc_iomgr_object *obj, const char *name);
void grpc_iomgr_unregister_object(grpc_iomgr_object *obj);

void grpc_iomgr_platform_init(void);
/** flush any globally queued work from iomgr */
void grpc_iomgr_platform_flush(void);
/** tear down all platform specific global iomgr structures */
void grpc_iomgr_platform_shutdown(void);

#endif /* GRPC_INTERNAL_CORE_IOMGR_IOMGR_INTERNAL_H */
