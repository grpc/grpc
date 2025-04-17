// Copyright 2024 gRPC authors.
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

#include <limits>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/util/useful.h"

namespace grpc_core {

template <typename T>
void ClampWorks(T value, T min, T max) {
  if (max < min) return;  // invalid args
  T result = Clamp(value, min, max);
  EXPECT_LE(result, max);
  EXPECT_GE(result, min);
}

void ClampWorksInt(int value, int min, int max) { ClampWorks(value, min, max); }
FUZZ_TEST(MyTestSuite, ClampWorksInt);
void ClampWorksUint64(uint64_t value, uint64_t min, uint64_t max) {
  ClampWorks(value, min, max);
}
FUZZ_TEST(MyTestSuite, ClampWorksUint64);
void ClampWorksUint8(uint8_t value, uint8_t min, uint8_t max) {
  ClampWorks(value, min, max);
}
FUZZ_TEST(MyTestSuite, ClampWorksUint8);
void ClampWorksInt8(int8_t value, int8_t min, int8_t max) {
  ClampWorks(value, min, max);
}
FUZZ_TEST(MyTestSuite, ClampWorksInt8);

template <typename T, typename Bigger>
void SaturatingAddWorks(T a, T b) {
  T result = SaturatingAdd(a, b);
  Bigger expect = Clamp<Bigger>(static_cast<Bigger>(a) + static_cast<Bigger>(b),
                                std::numeric_limits<T>::min(),
                                std::numeric_limits<T>::max());
  EXPECT_EQ(result, expect);
}

void SaturatingAddWorksInt32(int32_t a, int32_t b) {
  SaturatingAddWorks<int32_t, int64_t>(a, b);
}
FUZZ_TEST(MyTestSuite, SaturatingAddWorksInt32);
void SaturatingAddWorksUint32(uint32_t a, uint32_t b) {
  SaturatingAddWorks<uint32_t, uint64_t>(a, b);
}
FUZZ_TEST(MyTestSuite, SaturatingAddWorksUint32);
void SaturatingAddWorksInt8(int8_t a, int8_t b) {
  SaturatingAddWorks<int8_t, int16_t>(a, b);
}
FUZZ_TEST(MyTestSuite, SaturatingAddWorksInt8);
void SaturatingAddWorksUint8(uint8_t a, uint8_t b) {
  SaturatingAddWorks<uint8_t, uint16_t>(a, b);
}
FUZZ_TEST(MyTestSuite, SaturatingAddWorksUint8);

}  // namespace grpc_core
