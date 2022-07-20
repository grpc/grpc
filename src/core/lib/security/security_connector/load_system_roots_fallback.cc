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

#include <grpc/support/port_platform.h>

#if !defined(GPR_LINUX) && !defined(GPR_ANDROID) && !defined(GPR_FREEBSD) && \
    !defined(GPR_APPLE)

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>

#include "src/core/lib/security/security_connector/load_system_roots.h"

namespace grpc_core {

grpc_slice LoadSystemRootCerts() { return grpc_empty_slice(); }

}  // namespace grpc_core

#endif /* !(GPR_LINUX || GPR_ANDROID || GPR_FREEBSD || GPR_APPLE) */
