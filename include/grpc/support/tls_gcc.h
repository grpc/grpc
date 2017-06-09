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

#ifndef GRPC_SUPPORT_TLS_GCC_H
#define GRPC_SUPPORT_TLS_GCC_H

#include <stdbool.h>

#include <grpc/support/log.h>

/** Thread local storage based on gcc compiler primitives.
   #include tls.h to use this - and see that file for documentation */

#ifndef NDEBUG

struct gpr_gcc_thread_local {
  intptr_t value;
  bool *inited;
};

#define GPR_TLS_DECL(name)           \
  static bool name##_inited = false; \
  static __thread struct gpr_gcc_thread_local name = {0, &(name##_inited)}

#define gpr_tls_init(tls)                  \
  do {                                     \
    GPR_ASSERT(*((tls)->inited) == false); \
    *((tls)->inited) = true;               \
  } while (0)

/** It is allowed to call gpr_tls_init after gpr_tls_destroy is called. */
#define gpr_tls_destroy(tls)      \
  do {                            \
    GPR_ASSERT(*((tls)->inited)); \
    *((tls)->inited) = false;     \
  } while (0)

#define gpr_tls_set(tls, new_value) \
  do {                              \
    GPR_ASSERT(*((tls)->inited));   \
    (tls)->value = (new_value);     \
  } while (0)

#define gpr_tls_get(tls)          \
  ({                              \
    GPR_ASSERT(*((tls)->inited)); \
    (tls)->value;                 \
  })

#else /* NDEBUG */

struct gpr_gcc_thread_local {
  intptr_t value;
};

#define GPR_TLS_DECL(name) \
  static __thread struct gpr_gcc_thread_local name = {0}

#define gpr_tls_init(tls) \
  do {                    \
  } while (0)
#define gpr_tls_destroy(tls) \
  do {                       \
  } while (0)
#define gpr_tls_set(tls, new_value) (((tls)->value) = (new_value))
#define gpr_tls_get(tls) ((tls)->value)

#endif /* NDEBUG */

#endif /* GRPC_SUPPORT_TLS_GCC_H */
