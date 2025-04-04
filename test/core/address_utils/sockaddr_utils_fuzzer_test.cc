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

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "fuzztest/fuzztest.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/util/uri.h"

using fuzztest::Arbitrary;
using fuzztest::VectorOf;

void CheckUriIsParseable(std::vector<uint8_t> buffer) {
  grpc_resolved_address address;
  memset(&address, 0, sizeof(address));
  memcpy(address.addr, buffer.data(), buffer.size());
  address.len = buffer.size();
  absl::StatusOr<std::string> uri = grpc_sockaddr_to_uri(&address);
  if (!uri.ok()) return;
  absl::StatusOr<grpc_core::URI> parsed_uri =
      grpc_core::URI::Parse(uri.value());
  CHECK_OK(parsed_uri);
}
FUZZ_TEST(MyTestSuite, CheckUriIsParseable)
    .WithDomains(VectorOf(Arbitrary<uint8_t>())
                     .WithMaxSize(GRPC_MAX_SOCKADDR_SIZE)
                     .WithMinSize(1));
