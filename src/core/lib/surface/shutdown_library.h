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

#ifndef GRPC_CORE_LIB_SURFACE_SHUTDOWN_LIBRARY_H
#define GRPC_CORE_LIB_SURFACE_SHUTDOWN_LIBRARY_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

/* Register a function to be called when grpc_final_shutdown_library() is called. */
GRPCAPI void grpc_on_shutdown_callback(void (*func)());
/* Run an arbitrary function on an arg */
GRPCAPI void grpc_on_shutdown_callback_with_arg(void (*f)(const void*),
                                                const void* arg);

namespace grpc_core {

/* Helper function to wrap a new call and register the corresponding
 * delete-call for grpc_final_shutdown_library */
template <typename T>
T* OnShutdownDelete(T* p) {
  grpc_on_shutdown_callback_with_arg(
      [](const void* pp) { delete static_cast<const T*>(pp); }, p);
  return p;
}

/* Helper function to wrap a malloc call and register the corresponding
 * free-call for grpc_final_shutdown_library */
inline void* OnShutdownFree(void* p) {
  grpc_on_shutdown_callback_with_arg(
      [](const void* pp) { free(const_cast<void *>(pp)); }, p);
  return p;
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SURFACE_SHUTDOWN_LIBRARY_H */
