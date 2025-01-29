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

#include <grpc/support/json.h>
#include <stdint.h>
#include <string.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/util/dump_args.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"

namespace grpc_core {
namespace {

void ParseRoundTrips(std::string input) {
  auto json = JsonParse(input);
  if (json.ok()) {
    auto text2 = JsonDump(*json);
    auto json2 = JsonParse(text2);
    CHECK_OK(json2);
    EXPECT_EQ(*json, *json2)
        << GRPC_DUMP_ARGS(absl::CEscape(input), absl::CEscape(text2));
  }
}
FUZZ_TEST(JsonTest, ParseRoundTrips);

}  // namespace
}  // namespace grpc_core
