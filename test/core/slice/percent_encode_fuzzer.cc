//
//
// Copyright 2016 gRPC authors.
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
#include <stdint.h>
#include <string.h>

#include <utility>

#include "absl/log/check.h"
#include "fuzztest/fuzztest.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice.h"

using fuzztest::Arbitrary;
using fuzztest::ElementOf;
using fuzztest::VectorOf;

namespace grpc_core {
namespace {

void RoundTrips(std::vector<uint8_t> buffer, PercentEncodingType type) {
  auto input = Slice::FromCopiedBuffer(
      reinterpret_cast<const char*>(buffer.data()), buffer.size());
  auto output = PercentEncodeSlice(input.Ref(), type);
  auto permissive_decoded_output =
      PermissivePercentDecodeSlice(std::move(output));
  // decoded output must always match the input
  CHECK(input == permissive_decoded_output);
}
FUZZ_TEST(MyTestSuite, RoundTrips)
    .WithDomains(VectorOf(Arbitrary<uint8_t>()),
                 ElementOf({PercentEncodingType::URL,
                            PercentEncodingType::Compatible}));

void DecodeDoesntCrash(std::vector<uint8_t> buffer) {
  PermissivePercentDecodeSlice(Slice::FromCopiedBuffer(
      reinterpret_cast<const char*>(buffer.data()), buffer.size()));
}
FUZZ_TEST(MyTestSuite, DecodeDoesntCrash)
    .WithDomains(VectorOf(Arbitrary<uint8_t>()));

}  // namespace
}  // namespace grpc_core
