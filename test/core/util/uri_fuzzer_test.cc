//
//
// Copyright 2015 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <stdint.h>
#include <string.h>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/dump_args.h"
#include "src/core/util/uri.h"

namespace grpc_core {
namespace {

void ParseRoundTrips(std::string buffer) {
  auto uri = URI::Parse(buffer);
  if (!uri.ok()) return;
  auto buffer2 = uri->ToString();
  auto uri2 = URI::Parse(buffer2);
  CHECK_OK(uri2);
  EXPECT_EQ(uri->ToString(), uri2->ToString())
      << GRPC_DUMP_ARGS(absl::CEscape(buffer), absl::CEscape(buffer2));
  EXPECT_EQ(uri->scheme(), uri2->scheme())
      << GRPC_DUMP_ARGS(absl::CEscape(buffer), absl::CEscape(buffer2));
  EXPECT_EQ(uri->authority(), uri2->authority())
      << GRPC_DUMP_ARGS(absl::CEscape(buffer), absl::CEscape(buffer2));
  EXPECT_EQ(uri->path(), uri2->path())
      << GRPC_DUMP_ARGS(absl::CEscape(buffer), absl::CEscape(buffer2));
  EXPECT_EQ(uri->query_parameter_pairs(), uri2->query_parameter_pairs())
      << GRPC_DUMP_ARGS(absl::CEscape(buffer), absl::CEscape(buffer2));
  EXPECT_EQ(uri->fragment(), uri2->fragment())
      << GRPC_DUMP_ARGS(absl::CEscape(buffer), absl::CEscape(buffer2));
  EXPECT_EQ(uri, uri2);
}
FUZZ_TEST(UriTest, ParseRoundTrips);

TEST(UriTest, ParseRoundTripsRegression) { ParseRoundTrips("W:////\244"); }

}  // namespace
}  // namespace grpc_core
