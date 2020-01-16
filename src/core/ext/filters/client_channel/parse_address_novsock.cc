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

#include <grpc/support/port_platform.h>
#include "src/core/ext/filters/client_channel/parse_address.h"

#ifndef GRPC_HAVE_LINUX_VSOCK

bool grpc_parse_vsock(const grpc_uri* uri,
                      grpc_resolved_address* resolved_addr) {
  abort();
}

#endif /* GRPC_HAVE_LINUX_VSOCK */
