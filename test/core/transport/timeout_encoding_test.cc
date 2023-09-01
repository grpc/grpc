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

#include "src/core/lib/transport/timeout_encoding.h"

#include <initializer_list>
#include <string>

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_core {
namespace {

void assert_encodes_as(Duration ts, const char* s) {
  EXPECT_EQ(absl::string_view(s),
            Timeout::FromDuration(ts).Encode().as_string_view())
      << " ts=" << ts.ToString();
}

TEST(TimeoutTest, Encoding) {
  assert_encodes_as(Duration::Milliseconds(-1), "1n");
  assert_encodes_as(Duration::Milliseconds(-10), "1n");
  assert_encodes_as(Duration::Milliseconds(1), "1m");
  assert_encodes_as(Duration::Milliseconds(10), "10m");
  assert_encodes_as(Duration::Milliseconds(100), "100m");
  assert_encodes_as(Duration::Milliseconds(890), "890m");
  assert_encodes_as(Duration::Milliseconds(900), "900m");
  assert_encodes_as(Duration::Milliseconds(901), "901m");
  assert_encodes_as(Duration::Milliseconds(1000), "1S");
  assert_encodes_as(Duration::Milliseconds(2000), "2S");
  assert_encodes_as(Duration::Milliseconds(2500), "2500m");
  assert_encodes_as(Duration::Milliseconds(59900), "59900m");
  assert_encodes_as(Duration::Seconds(50), "50S");
  assert_encodes_as(Duration::Seconds(59), "59S");
  assert_encodes_as(Duration::Seconds(60), "1M");
  assert_encodes_as(Duration::Seconds(80), "80S");
  assert_encodes_as(Duration::Seconds(90), "90S");
  assert_encodes_as(Duration::Seconds(120), "2M");
  assert_encodes_as(Duration::Minutes(20), "20M");
  assert_encodes_as(Duration::Hours(1), "1H");
  assert_encodes_as(Duration::Hours(10), "10H");
  assert_encodes_as(Duration::Hours(1) - Duration::Milliseconds(100), "1H");
  assert_encodes_as(Duration::Hours(100), "100H");
  assert_encodes_as(Duration::Hours(100000), "27000H");
}

void assert_decodes_as(const char* buffer, Duration expected) {
  EXPECT_EQ(expected, ParseTimeout(Slice::FromCopiedString(buffer)));
}

void decode_suite(char ext, Duration (*answer)(int64_t x)) {
  long test_vals[] = {1,       12,       123,       1234,     12345,   123456,
                      1234567, 12345678, 123456789, 98765432, 9876543, 987654,
                      98765,   9876,     987,       98,       9};
  for (unsigned i = 0; i < GPR_ARRAY_SIZE(test_vals); i++) {
    std::string input = absl::StrFormat("%ld%c", test_vals[i], ext);
    assert_decodes_as(input.c_str(), answer(test_vals[i]));

    input = absl::StrFormat("   %ld%c", test_vals[i], ext);
    assert_decodes_as(input.c_str(), answer(test_vals[i]));

    input = absl::StrFormat("%ld %c", test_vals[i], ext);
    assert_decodes_as(input.c_str(), answer(test_vals[i]));

    input = absl::StrFormat("%ld %c  ", test_vals[i], ext);
    assert_decodes_as(input.c_str(), answer(test_vals[i]));
  }
}

TEST(TimeoutTest, DecodingSucceeds) {
  decode_suite('n', Duration::NanosecondsRoundUp);
  decode_suite('u', Duration::MicrosecondsRoundUp);
  decode_suite('m', Duration::Milliseconds);
  decode_suite('S', Duration::Seconds);
  decode_suite('M', Duration::Minutes);
  decode_suite('H', Duration::Hours);
  assert_decodes_as("1000000000S", Duration::Seconds(1000 * 1000 * 1000));
  assert_decodes_as("1000000000000000000000u", Duration::Infinity());
  assert_decodes_as("1000000001S", Duration::Infinity());
  assert_decodes_as("2000000001S", Duration::Infinity());
  assert_decodes_as("9999999999S", Duration::Infinity());
}

void assert_decoding_fails(const char* s) {
  EXPECT_EQ(absl::nullopt, ParseTimeout(Slice::FromCopiedString(s)))
      << " s=" << s;
}

TEST(TimeoutTest, DecodingFails) {
  assert_decoding_fails("");
  assert_decoding_fails(" ");
  assert_decoding_fails("x");
  assert_decoding_fails("1");
  assert_decoding_fails("1x");
  assert_decoding_fails("1ux");
  assert_decoding_fails("!");
  assert_decoding_fails("n1");
  assert_decoding_fails("-1u");
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
