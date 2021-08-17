/*
 *
 * Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPR_TLS_STDCPP_H
#define GRPC_CORE_LIB_GPR_TLS_STDCPP_H

#include <grpc/support/port_platform.h>

/** Thread local storage based on C++ thread_local.
   #include tls.h to use this - and see that file for documentation */

/** Use GPR_TLS_DECL to declare tls static variables outside a class */
#define GPR_TLS_DECL(name) thread_local static intptr_t name = 0

/** Use GPR_TLS_CLASS_DECL to declare tls static variable members of a class.
 *  GPR_TLS_CLASS_DEF needs to be called to define this member. */
#define GPR_TLS_CLASS_DECL(name) thread_local static intptr_t name

#define GPR_TLS_CLASS_DEF(name) thread_local intptr_t name = 0

#define gpr_tls_init(tls) \
  do {                    \
  } while (0)

#define gpr_tls_destroy(tls) \
  do {                       \
  } while (0)

#define gpr_tls_set(tls, new_value) (*(tls) = (new_value))

#define gpr_tls_get(tls) (*(tls))

#endif /* GRPC_CORE_LIB_GPR_TLS_STDCPP_H */
