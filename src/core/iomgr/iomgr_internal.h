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

int grpc_maybe_call_delayed_callbacks(gpr_mu *drop_mu, int success);
void grpc_iomgr_add_delayed_callback(grpc_iomgr_closure *iocb, int success);

void grpc_iomgr_register_object_internal(grpc_iomgr_object *obj,
                                         const char *name,
                                         const char *file,
                                         int line);
void grpc_iomgr_unregister_object_internal(grpc_iomgr_object *obj,
                                           const char *file,
                                           int line);

#define grpc_iomgr_register_object(obj, name) \
  grpc_iomgr_register_object_internal(obj, name, __FILE__, __LINE__)

#define grpc_iomgr_unregister_object(obj) \
  grpc_iomgr_unregister_object_internal(obj, __FILE__, __LINE__)

void grpc_iomgr_platform_init(void);
void grpc_iomgr_platform_shutdown(void);

#endif  /* GRPC_INTERNAL_CORE_IOMGR_IOMGR_INTERNAL_H */
