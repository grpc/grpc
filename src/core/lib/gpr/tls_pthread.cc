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

#include <errno.h>
#include <grpc/support/time.h>

#ifdef GPR_PTHREAD_TLS

#include "src/core/lib/gpr/tls.h"

intptr_t gpr_tls_set(struct gpr_pthread_thread_local* tls, intptr_t value) {
  int ret= pthread_setspecific(tls->key, (void*)value);
  if (ret != 0) {
    gpr_log(GPR_ERROR, "pthread_setspecific(0x%lx) returned %d", tls->key, ret);
  }
  GPR_ASSERT(0 == ret);
  gpr_log(GPR_ERROR, "pthread_setspecific(0x%lx)", tls->key);
  return value;
}

void gpr_tls_init(struct gpr_pthread_thread_local* tls) {
  int ret;
  do {
    ret = pthread_key_create(&(tls)->key, NULL);
    if (ret != 0) {
      gpr_log(GPR_ERROR, "pthread_key_create returned %d", ret);
    }
    GPR_ASSERT(0 == ret || EAGAIN == ret);
    if (ret == EAGAIN) {
      gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                   gpr_time_from_millis(100, GPR_TIMESPAN)));
    }
  } while (ret == EAGAIN);

  gpr_log(GPR_ERROR, "pthread_key_create(0x%lx)", tls->key);
}

int gpr_tls_destroy(struct gpr_pthread_thread_local* tls) {
  gpr_log(GPR_ERROR, "pthread_key_delete(0x%lx)", tls->key);
  int ret = pthread_key_delete((tls)->key);
  if (ret != 0) {
    gpr_log(GPR_ERROR, "pthread_key_delete(0x%lx) returned %d", tls->key, ret);
  }
  return ret;
}

#endif /* GPR_PTHREAD_TLS */
