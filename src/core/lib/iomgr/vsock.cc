//
//
// Copyright 2023 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/vsock.h"

#ifdef GRPC_HAVE_VSOCK

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/transport/error_utils.h"

absl::StatusOr<std::vector<grpc_resolved_address>> grpc_resolve_vsock_address(
    absl::string_view name) {
  grpc_resolved_address addr;
  grpc_error_handle error = grpc_core::VSockaddrPopulate(name, &addr);
  GRPC_RETURN_IF_ERROR(error);
  return std::vector<grpc_resolved_address>({addr});
}

int grpc_is_vsock(const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  return addr->sa_family == AF_VSOCK;
}
#else
absl::StatusOr<std::vector<grpc_resolved_address>> grpc_resolve_vsock_address(
    absl::string_view /*name*/) {
  return absl::InvalidArgumentError("VSOCK is not supported.");
}

int grpc_is_vsock(const grpc_resolved_address* /*resolved_addr*/) { return 0; }
#endif
