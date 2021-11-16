//
// Copyright 2021 gRPC authors.
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

#include "src/core/lib/transport/parsed_metadata.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

struct CharTrait {
  using MementoType = char;
  static const char* key() { return "key"; }
  static char test_memento() { return 'a'; }
  static char test_value() { return 'a'; }
  static size_t test_memento_transport_size() { return 34; }
  static char MementoToValue(char memento) { return memento; }
  static char ParseMemento(const grpc_slice& slice) {
    return *GRPC_SLICE_START_PTR(slice);
  }
  static std::string DisplayValue(char value) { return std::string(1, value); }
};

struct Int32Trait {
  using MementoType = int32_t;
  static const char* key() { return "key2"; }
  static int32_t test_memento() { return -1; }
  static int32_t test_value() { return -1; }
  static size_t test_memento_transport_size() { return 478; }
  static int32_t MementoToValue(int32_t memento) { return memento; }
  static int32_t ParseMemento(const grpc_slice& slice) {
    int32_t out;
    GPR_ASSERT(absl::SimpleAtoi(StringViewFromSlice(slice), &out));
    return out;
  }
  static std::string DisplayValue(int32_t value) {
    return std::to_string(value);
  }
};

struct Int64Trait {
  using MementoType = int64_t;
  static const char* key() { return "key3"; }
  static int64_t test_memento() { return 83481847284179298; }
  static int64_t test_value() { return -83481847284179298; }
  static size_t test_memento_transport_size() { return 87; }
  static int64_t MementoToValue(int64_t memento) { return -memento; }
  static int64_t ParseMemento(const grpc_slice& slice) {
    int64_t out;
    GPR_ASSERT(absl::SimpleAtoi(StringViewFromSlice(slice), &out));
    return out;
  }
  static std::string DisplayValue(int64_t value) {
    return std::to_string(value);
  }
};

struct IntptrTrait {
  using MementoType = intptr_t;
  static const char* key() { return "key4"; }
  static intptr_t test_memento() { return 8374298; }
  static intptr_t test_value() { return test_memento() / 2; }
  static size_t test_memento_transport_size() { return 800; }
  static intptr_t MementoToValue(intptr_t memento) { return memento / 2; }
  static intptr_t ParseMemento(const grpc_slice& slice) {
    intptr_t out;
    GPR_ASSERT(absl::SimpleAtoi(StringViewFromSlice(slice), &out));
    return out;
  }
  static std::string DisplayValue(intptr_t value) {
    return std::to_string(value);
  }
};

struct StringTrait {
  using MementoType = std::string;
  static const char* key() { return "key5-bin"; }
  static std::string test_memento() { return "hello"; }
  static std::string test_value() { return "hi hello"; }
  static size_t test_memento_transport_size() { return 599; }
  static std::string MementoToValue(std::string memento) {
    return "hi " + memento;
  }
  static std::string ParseMemento(const grpc_slice& slice) {
    auto view = StringViewFromSlice(slice);
    return std::string(view.begin(), view.end());
  }
  static std::string DisplayValue(const std::string& value) { return value; }
};

class FakeContainer {
 public:
  void Set(CharTrait, char x) { SetChar(x); }
  void Set(Int32Trait, int32_t x) { SetInt32(x); }
  void Set(Int64Trait, int64_t x) { SetInt64(x); }
  void Set(IntptrTrait, intptr_t x) { SetIntptr(x); }
  void Set(StringTrait, std::string x) { SetString(x); }

  void Set(const ::grpc_core::ParsedMetadata<FakeContainer>& metadata) {
    EXPECT_EQ(GRPC_ERROR_NONE, metadata.SetOnContainer(this));
  }

  MOCK_METHOD1(SetChar, void(char));
  MOCK_METHOD1(SetInt32, void(int32_t));
  MOCK_METHOD1(SetInt64, void(int64_t));
  MOCK_METHOD1(SetIntptr, void(intptr_t));
  MOCK_METHOD1(SetString, void(std::string));
};

using ParsedMetadata = ::grpc_core::ParsedMetadata<FakeContainer>;

TEST(ParsedMetadataTest, Noop) { ParsedMetadata(); }

TEST(ParsedMetadataTest, DebugString) {
  ParsedMetadata parsed(CharTrait(), 'x', 36);
  EXPECT_EQ(parsed.DebugString(), "key: x");
}

TEST(ParsedMetadataTest, IsNotBinary) {
  ParsedMetadata parsed(CharTrait(), 'x', 36);
  EXPECT_FALSE(parsed.is_binary_header());
}

TEST(ParsedMetadataTest, IsBinary) {
  ParsedMetadata parsed(StringTrait(), "s", 36);
  EXPECT_TRUE(parsed.is_binary_header());
}

TEST(ParsedMetadataTest, Set) {
  FakeContainer c;
  ParsedMetadata p(CharTrait(), 'x', 36);
  EXPECT_CALL(c, SetChar('x')).Times(1);
  c.Set(p);
  p = ParsedMetadata(Int32Trait(), -1, 478);
  EXPECT_CALL(c, SetInt32(-1)).Times(1);
  c.Set(p);
  p = ParsedMetadata(Int64Trait(), 83481847284179298, 87);
  EXPECT_CALL(c, SetInt64(-83481847284179298)).Times(1);
  c.Set(p);
  p = ParsedMetadata(IntptrTrait(), 8374298, 800);
  EXPECT_CALL(c, SetIntptr(4187149)).Times(1);
  c.Set(p);
  p = ParsedMetadata(StringTrait(), "hello", 599);
  EXPECT_CALL(c, SetString("hi hello")).Times(1);
  c.Set(p);
}

template <typename T>
class TraitSpecializedTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(TraitSpecializedTest);

TYPED_TEST_P(TraitSpecializedTest, Noop) {
  ParsedMetadata(TypeParam(), TypeParam::test_memento(),
                 TypeParam::test_memento_transport_size());
}

TYPED_TEST_P(TraitSpecializedTest, CanMove) {
  ParsedMetadata a(TypeParam(), TypeParam::test_memento(),
                   TypeParam::test_memento_transport_size());
  ParsedMetadata b = std::move(a);
  a = std::move(b);
}

TYPED_TEST_P(TraitSpecializedTest, DebugString) {
  ParsedMetadata p(TypeParam(), TypeParam::test_memento(),
                   TypeParam::test_memento_transport_size());
  EXPECT_EQ(p.DebugString(),
            absl::StrCat(TypeParam::key(), ": ",
                         TypeParam::DisplayValue(TypeParam::test_memento())));
}

TYPED_TEST_P(TraitSpecializedTest, TransportSize) {
  ParsedMetadata p(TypeParam(), TypeParam::test_memento(),
                   TypeParam::test_memento_transport_size());
  EXPECT_EQ(p.transport_size(), TypeParam::test_memento_transport_size());
}

REGISTER_TYPED_TEST_SUITE_P(TraitSpecializedTest, Noop, CanMove, DebugString,
                            TransportSize);

using InterestingTraits = ::testing::Types<CharTrait, Int32Trait, Int64Trait,
                                           IntptrTrait, StringTrait>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, TraitSpecializedTest, InterestingTraits);

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
};
