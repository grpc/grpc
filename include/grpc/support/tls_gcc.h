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

#ifndef GRPC_SUPPORT_TLS_GCC_H
#define GRPC_SUPPORT_TLS_GCC_H

#include <stdbool.h>

#include <grpc/support/log.h>

/* Thread local storage based on gcc compiler primitives.
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

/* It is allowed to call gpr_tls_init after gpr_tls_destroy is called. */
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
