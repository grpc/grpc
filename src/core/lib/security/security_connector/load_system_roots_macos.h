/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_LOAD_SYSTEM_ROOTS_MACOS_H
#define GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_LOAD_SYSTEM_ROOTS_MACOS_H

#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <errno.h>
#include <sys/sysctl.h>

namespace grpc_core {

typedef int (*grpc_macos_system_roots_getter)(CFDataRef* data,
                                              CFDataRef* untrusted_data);

// Fills roots with system root certificates, uses get_roots to retrieve them
// (pass in nullptr for get_roots during normal execution, otherwise pass in
// mock function for testing). Returns -1 on failure, 0 otherwise.
int GetMacOSRootCerts(grpc_slice* roots,
                      grpc_macos_system_roots_getter get_roots);

}  // namespace grpc_core

#endif /* GPR_APPLE */
#endif /* GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_LOAD_SYSTEM_ROOTS_MACOS_H \
        */
