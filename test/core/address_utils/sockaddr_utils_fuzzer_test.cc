//
// Copyright 2022 gRPC authors.
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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <grpc/grpc.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/uri/uri_parser.h"

bool squelch = true;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > GRPC_MAX_SOCKADDR_SIZE) return 0;
  grpc_resolved_address address;
  memset(&address, 0, sizeof(address));
  memcpy(address.addr, data, size);
  address.len = size;

  absl::StatusOr<std::string> uri = grpc_sockaddr_to_uri(&address);
  if (!uri.ok()) return 0;
  absl::StatusOr<grpc_core::URI> parsed_uri =
      grpc_core::URI::Parse(uri.value());

  GPR_ASSERT(parsed_uri.ok());
  return 0;
}
