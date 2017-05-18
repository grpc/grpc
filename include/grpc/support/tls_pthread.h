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

#ifndef GRPC_SUPPORT_TLS_PTHREAD_H
#define GRPC_SUPPORT_TLS_PTHREAD_H

#include <grpc/support/log.h> /* for GPR_ASSERT */
#include <pthread.h>

/** Thread local storage based on pthread library calls.
   #include tls.h to use this - and see that file for documentation */

struct gpr_pthread_thread_local {
  pthread_key_t key;
};

#define GPR_TLS_DECL(name) static struct gpr_pthread_thread_local name = {0}

#define gpr_tls_init(tls) GPR_ASSERT(0 == pthread_key_create(&(tls)->key, NULL))
#define gpr_tls_destroy(tls) pthread_key_delete((tls)->key)
#define gpr_tls_get(tls) ((intptr_t)pthread_getspecific((tls)->key))
#ifdef __cplusplus
extern "C" {
#endif
intptr_t gpr_tls_set(struct gpr_pthread_thread_local *tls, intptr_t value);
#ifdef __cplusplus
}
#endif

#endif /* GRPC_SUPPORT_TLS_PTHREAD_H */
