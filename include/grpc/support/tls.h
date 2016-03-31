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

#ifndef GRPC_SUPPORT_TLS_H
#define GRPC_SUPPORT_TLS_H

#include <grpc/support/port_platform.h>

/* Thread local storage.

   A minimal wrapper that should be implementable across many compilers,
   and implementable efficiently across most modern compilers.

   Thread locals have type intptr_t.

   Declaring a thread local variable 'foo':
     GPR_TLS_DECL(foo);
   Thread locals always have static scope.

   Initializing a thread local (must be done at library initialization
   time):
     gpr_tls_init(&foo);

   Destroying a thread local:
     gpr_tls_destroy(&foo);

   Setting a thread local (returns new_value):
     gpr_tls_set(&foo, new_value);

   Accessing a thread local:
     current_value = gpr_tls_get(&foo, value);

   ALL functions here may be implemented as macros. */

#ifdef GPR_GCC_TLS
#include <grpc/support/tls_gcc.h>
#endif

#ifdef GPR_MSVC_TLS
#include <grpc/support/tls_msvc.h>
#endif

#ifdef GPR_PTHREAD_TLS
#include <grpc/support/tls_pthread.h>
#endif

#endif /* GRPC_SUPPORT_TLS_H */
