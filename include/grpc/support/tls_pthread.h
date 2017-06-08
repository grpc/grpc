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
