// Copyright 2020 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstdlib>
#include <ctime>
#include <memory>
#include <random>
#include <sstream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/debugging/leak_check.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_ring.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

extern thread_local bool cord_ring;

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

using RandomEngine = std::mt19937_64;

using ::absl::cord_internal::CordRep;
using ::absl::cord_internal::CordRepConcat;
using ::absl::cord_internal::CordRepExternal;
using ::absl::cord_internal::CordRepFlat;
using ::absl::cord_internal::CordRepRing;
using ::absl::cord_internal::CordRepSubstring;

using ::absl::cord_internal::EXTERNAL;
using ::absl::cord_internal::SUBSTRING;

using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::Eq;
using testing::Ge;
using testing::Le;
using testing::Lt;
using testing::Ne;
using testing::SizeIs;

using index_type = CordRepRing::index_type;

enum InputShareMode { kPrivate, kShared, kSharedIndirect };

// TestParam class used by all test fixtures.
// Not all fixtures use all possible input combinations
struct TestParam {
  TestParam() = default;
  explicit TestParam(InputShareMode input_share_mode)
      : input_share_mode(input_share_mode) {}

  // Run the test with the 'rep under test' to be privately owned.
  // Otherwise, the rep has a shared ref count of 2 or higher.
  bool refcount_is_one = true;

  // Run the test with the 'rep under test' being allocated with enough capacity
  // to accommodate any modifications made to it. Otherwise, the rep has zero
  // extra (reserve) capacity.
  bool with_capacity = true;

  // For test providing possibly shared input such as Append(.., CordpRep*),
  // this field defines if that input is adopted with a refcount of one
  // (privately owned / donated), or shared. For composite inputs such as
  // 'substring of flat', we also have the 'shared indirect' value which means
  // the top level node is not shared, but the contained child node is shared.
  InputShareMode input_share_mode = kPrivate;

  std::string ToString() const {
    return absl::StrCat(refcount_is_one ? "Private" : "Shared",
                        with_capacity ? "" : "_NoCapacity",
                        (input_share_mode == kPrivate) ? ""
                        : (input_share_mode == kShared)
                            ? "_SharedInput"
                            : "_IndirectSharedInput");
  }
};
using TestParams = std::vector<TestParam>;

// Matcher validating when mutable copies are required / performed.
MATCHER_P2(EqIfPrivate, param, rep,
           absl::StrCat("Equal 0x", absl::Hex(rep), " if private")) {
  return param.refcount_is_one ? arg == rep : true;
}

// Matcher validating when mutable copies are required / performed.
MATCHER_P2(EqIfPrivateAndCapacity, param, rep,
           absl::StrCat("Equal 0x", absl::Hex(rep),
                        " if private and capacity")) {
  return (param.refcount_is_one && param.with_capacity) ? arg == rep : true;
}

// Matcher validating a shared ring was re-allocated. Should only be used for
// tests doing exactly one update as subsequent updates could return the
// original (freed and re-used) pointer.
MATCHER_P2(NeIfShared, param, rep,
           absl::StrCat("Not equal 0x", absl::Hex(rep), " if shared")) {
  return param.refcount_is_one ? true : arg != rep;
}

MATCHER_P2(EqIfInputPrivate, param, rep, "Equal if input is private") {
  return param.input_share_mode == kPrivate ? arg == rep : arg != rep;
}

// Matcher validating the core in-variants of the CordRepRing instance.
MATCHER(IsValidRingBuffer, "RingBuffer is valid") {
  std::stringstream ss;
  if (!arg->IsValid(ss)) {
    *result_listener << "\nERROR: " << ss.str() << "\nRING = " << *arg;
    return false;
  }
  return true;
}

// Returns the flats contained in the provided CordRepRing
std::vector<string_view> ToFlats(const CordRepRing* r) {
  std::vector<string_view> flats;
  flats.reserve(r->entries());
  index_type pos = r->head();
  do {
    flats.push_back(r->entry_data(pos));
  } while ((pos = r->advance(pos)) != r->tail());
  return flats;
}

class not_a_string_view {
 public:
  explicit not_a_string_view(absl::string_view s)
      : data_(s.data()), size_(s.size()) {}
  explicit not_a_string_view(const void* data, size_t size)
      : data_(data), size_(size) {}

  not_a_string_view remove_prefix(size_t n) const {
    return not_a_string_view(static_cast<const char*>(data_) + n, size_ - n);
  }

  not_a_string_view remove_suffix(size_t n) const {
    return not_a_string_view(data_, size_ - n);
  }

  const void* data() const { return data_; }
  size_t size() const { return size_; }

 private:
  const void* data_;
  size_t size_;
};

bool operator==(not_a_string_view lhs, not_a_string_view rhs) {
  return lhs.data() == rhs.data() && lhs.size() == rhs.size();
}

std::ostream& operator<<(std::ostream& s, not_a_string_view rhs) {
  return s << "{ data: " << rhs.data() << " size: " << rhs.size() << "}";
}

std::vector<not_a_string_view> ToRawFlats(const CordRepRing* r) {
  std::vector<not_a_string_view> flats;
  flats.reserve(r->entries());
  index_type pos = r->head();
  do {
    flats.emplace_back(r->entry_data(pos));
  } while ((pos = r->advance(pos)) != r->tail());
  return flats;
}

// Returns the value contained in the provided CordRepRing
std::string ToString(const CordRepRing* r) {
  std::string value;
  value.reserve(r->length);
  index_type pos = r->head();
  do {
    absl::string_view sv = r->entry_data(pos);
    value.append(sv.data(), sv.size());
  } while ((pos = r->advance(pos)) != r->tail());
  return value;
}

// Creates a flat for testing
CordRep* MakeFlat(absl::string_view s, size_t extra = 0) {
  CordRepFlat* flat = CordRepFlat::New(s.length() + extra);
  memcpy(flat->Data(), s.data(), s.length());
  flat->length = s.length();
  return flat;
}

// Creates an external node for testing
CordRepExternal* MakeExternal(absl::string_view s) {
  struct Rep : public CordRepExternal {
    std::string s;
    explicit Rep(absl::string_view s) : s(s) {
      this->tag = EXTERNAL;
      this->base = s.data();
      this->length = s.length();
      this->releaser_invoker = [](CordRepExternal* self) {
        delete static_cast<Rep*>(self);
      };
    }
  };
  return new Rep(s);
}

CordRepExternal* MakeFakeExternal(size_t length) {
  struct Rep : public CordRepExternal {
    std::string s;
    explicit Rep(size_t len) {
      this->tag = EXTERNAL;
      this->base = reinterpret_cast<const char*>(this->storage);
      this->length = len;
      this->releaser_invoker = [](CordRepExternal* self) {
        delete static_cast<Rep*>(self);
      };
    }
  };
  return new Rep(length);
}

// Creates a flat or an external node for testing depending on the size.
CordRep* MakeLeaf(absl::string_view s, size_t extra = 0) {
  if (s.size() <= absl::cord_internal::kMaxFlatLength) {
    return MakeFlat(s, extra);
  } else {
    return MakeExternal(s);
  }
}

// Creates a substring node
CordRepSubstring* MakeSubstring(size_t start, size_t len, CordRep* rep) {
  auto* sub = new CordRepSubstring;
  sub->tag = SUBSTRING;
  sub->start = start;
  sub->length = (len <= 0) ? rep->length - start + len : len;
  sub->child = rep;
  return sub;
}

// Creates a substring node removing the specified prefix
CordRepSubstring* RemovePrefix(size_t start, CordRep* rep) {
  return MakeSubstring(start, rep->length - start, rep);
}

// Creates a substring node removing the specified suffix
CordRepSubstring* RemoveSuffix(size_t length, CordRep* rep) {
  return MakeSubstring(0, rep->length - length, rep);
}

enum Composition { kMix, kAppend, kPrepend };

Composition RandomComposition() {
  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  return (rng() & 1) ? kMix : ((rng() & 1) ? kAppend : kPrepend);
}

absl::string_view ToString(Composition composition) {
  switch (composition) {
    case kAppend:
      return "Append";
    case kPrepend:
      return "Prepend";
    case kMix:
      return "Mix";
  }
  assert(false);
  return "???";
}

constexpr const char* kFox = "The quick brown fox jumps over the lazy dog";
constexpr const char* kFoxFlats[] = {"The ", "quick ", "brown ",
                                     "fox ", "jumps ", "over ",
                                     "the ", "lazy ",  "dog"};

CordRepRing* FromFlats(Span<const char* const> flats,
                       Composition composition = kAppend) {
  if (flats.empty()) return nullptr;
  CordRepRing* ring = nullptr;
  switch (composition) {
    case kAppend:
      ring = CordRepRing::Create(MakeLeaf(flats.front()), flats.size() - 1);
      for (int i = 1; i < flats.size(); ++i) {
        ring = CordRepRing::Append(ring, MakeLeaf(flats[i]));
      }
      break;
    case kPrepend:
      ring = CordRepRing::Create(MakeLeaf(flats.back()), flats.size() - 1);
      for (int i = static_cast<int>(flats.size() - 2); i >= 0; --i) {
        ring = CordRepRing::Prepend(ring, MakeLeaf(flats[i]));
      }
      break;
    case kMix:
      size_t middle1 = flats.size() / 2, middle2 = middle1;
      ring = CordRepRing::Create(MakeLeaf(flats[middle1]), flats.size() - 1);
      if (!flats.empty()) {
        if ((flats.size() & 1) == 0) {
          ring = CordRepRing::Prepend(ring, MakeLeaf(flats[--middle1]));
        }
        for (int i = 1; i <= middle1; ++i) {
          ring = CordRepRing::Prepend(ring, MakeLeaf(flats[middle1 - i]));
          ring = CordRepRing::Append(ring, MakeLeaf(flats[middle2 + i]));
        }
      }
      break;
  }
  EXPECT_THAT(ToFlats(ring), ElementsAreArray(flats));
  return ring;
}

std::ostream& operator<<(std::ostream& s, const TestParam& param) {
  return s << param.ToString();
}

std::string TestParamToString(const testing::TestParamInfo<TestParam>& info) {
  return info.param.ToString();
}

class CordRingTest : public testing::Test {
 public:
  ~CordRingTest() override {
    for (CordRep* rep : unrefs_) {
      CordRep::Unref(rep);
    }
  }

  template <typename CordRepType>
  CordRepType* NeedsUnref(CordRepType* rep) {
    assert(rep);
    unrefs_.push_back(rep);
    return rep;
  }

  template <typename CordRepType>
  CordRepType* Ref(CordRepType* rep) {
    CordRep::Ref(rep);
    return NeedsUnref(rep);
  }

 private:
  std::vector<CordRep*> unrefs_;
};

class CordRingTestWithParam : public testing::TestWithParam<TestParam> {
 public:
  ~CordRingTestWithParam() override {
    for (CordRep* rep : unrefs_) {
      CordRep::Unref(rep);
    }
  }

  CordRepRing* CreateWithCapacity(CordRep* child, size_t extra_capacity) {
    if (!GetParam().with_capacity) extra_capacity = 0;
    CordRepRing* ring = CordRepRing::Create(child, extra_capacity);
    ring->SetCapacityForTesting(1 + extra_capacity);
    return RefIfShared(ring);
  }

  bool Shared() const { return !GetParam().refcount_is_one; }
  bool InputShared() const { return GetParam().input_share_mode == kShared; }
  bool InputSharedIndirect() const {
    return GetParam().input_share_mode == kSharedIndirect;
  }

  template <typename CordRepType>
  CordRepType* NeedsUnref(CordRepType* rep) {
    assert(rep);
    unrefs_.push_back(rep);
    return rep;
  }

  template <typename CordRepType>
  CordRepType* Ref(CordRepType* rep) {
    CordRep::Ref(rep);
    return NeedsUnref(rep);
  }

  template <typename CordRepType>
  CordRepType* RefIfShared(CordRepType* rep) {
    return Shared() ? Ref(rep) : rep;
  }

  template <typename CordRepType>
  CordRepType* RefIfInputShared(CordRepType* rep) {
    return InputShared() ? Ref(rep) : rep;
  }

  template <typename CordRepType>
  CordRepType* RefIfInputSharedIndirect(CordRepType* rep) {
    return InputSharedIndirect() ? Ref(rep) : rep;
  }

 private:
  std::vector<CordRep*> unrefs_;
};

class CordRingCreateTest : public CordRingTestWithParam {
 public:
  static TestParams CreateTestParams() {
    TestParams params;
    params.emplace_back(InputShareMode::kPrivate);
    params.emplace_back(InputShareMode::kShared);
    return params;
  }
};

class CordRingSubTest : public CordRingTestWithParam {
 public:
  static TestParams CreateTestParams() {
    TestParams params;
    for (bool refcount_is_one : {true, false}) {
      TestParam param;
      param.refcount_is_one = refcount_is_one;
      params.push_back(param);
    }
    return params;
  }
};

class CordRingBuildTest : public CordRingTestWithParam {
 public:
  static TestParams CreateTestParams() {
    TestParams params;
    for (bool refcount_is_one : {true, false}) {
      for (bool with_capacity : {true, false}) {
        TestParam param;
        param.refcount_is_one = refcount_is_one;
        param.with_capacity = with_capacity;
        params.push_back(param);
      }
    }
    return params;
  }
};

class CordRingCreateFromTreeTest : public CordRingTestWithParam {
 public:
  static TestParams CreateTestParams() {
    TestParams params;
    params.emplace_back(InputShareMode::kPrivate);
    params.emplace_back(InputShareMode::kShared);
    params.emplace_back(InputShareMode::kSharedIndirect);
    return params;
  }
};

class CordRingBuildInputTest : public CordRingTestWithParam {
 public:
  static TestParams CreateTestParams() {
    TestParams params;
    for (bool refcount_is_one : {true, false}) {
      for (bool with_capacity : {true, false}) {
        for (InputShareMode share_mode : {kPrivate, kShared, kSharedIndirect}) {
          TestParam param;
          param.refcount_is_one = refcount_is_one;
          param.with_capacity = with_capacity;
          param.input_share_mode = share_mode;
          params.push_back(param);
        }
      }
    }
    return params;
  }
};

INSTANTIATE_TEST_SUITE_P(WithParam, CordRingSubTest,
                         testing::ValuesIn(CordRingSubTest::CreateTestParams()),
                         TestParamToString);

INSTANTIATE_TEST_SUITE_P(
    WithParam, CordRingCreateTest,
    testing::ValuesIn(CordRingCreateTest::CreateTestParams()),
    TestParamToString);

INSTANTIATE_TEST_SUITE_P(
    WithParam, CordRingCreateFromTreeTest,
    testing::ValuesIn(CordRingCreateFromTreeTest::CreateTestParams()),
    TestParamToString);

INSTANTIATE_TEST_SUITE_P(
    WithParam, CordRingBuildTest,
    testing::ValuesIn(CordRingBuildTest::CreateTestParams()),
    TestParamToString);

INSTANTIATE_TEST_SUITE_P(
    WithParam, CordRingBuildInputTest,
    testing::ValuesIn(CordRingBuildInputTest::CreateTestParams()),
    TestParamToString);

TEST_P(CordRingCreateTest, CreateFromFlat) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz";
  CordRepRing* result = NeedsUnref(CordRepRing::Create(MakeFlat(str1)));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result->length, Eq(str1.size()));
  EXPECT_THAT(ToFlats(result), ElementsAre(str1));
}

TEST_P(CordRingCreateTest, CreateFromRing) {
  CordRepRing* ring = RefIfShared(FromFlats(kFoxFlats));
  CordRepRing* result = NeedsUnref(CordRepRing::Create(ring));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivate(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAreArray(kFoxFlats));
}

TEST_P(CordRingCreateFromTreeTest, CreateFromSubstringRing) {
  CordRepRing* ring = RefIfInputSharedIndirect(FromFlats(kFoxFlats));
  CordRep* sub = RefIfInputShared(MakeSubstring(2, 11, ring));
  CordRepRing* result = NeedsUnref(CordRepRing::Create(sub));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfInputPrivate(GetParam(), ring));
  EXPECT_THAT(ToString(result), string_view(kFox).substr(2, 11));
}

TEST_F(CordRingTest, CreateWithIllegalExtraCapacity) {
#if defined(ABSL_HAVE_EXCEPTIONS)
  CordRep* flat = NeedsUnref(MakeFlat("Hello world"));
  try {
    CordRepRing::Create(flat, CordRepRing::kMaxCapacity);
    GTEST_FAIL() << "expected std::length_error exception";
  } catch (const std::length_error&) {
  }
#elif defined(GTEST_HAS_DEATH_TEST)
  CordRep* flat = NeedsUnref(MakeFlat("Hello world"));
  EXPECT_DEATH(CordRepRing::Create(flat, CordRepRing::kMaxCapacity), ".*");
#endif
}

TEST_P(CordRingCreateFromTreeTest, CreateFromSubstringOfFlat) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz";
  auto* flat = RefIfInputShared(MakeFlat(str1));
  auto* child = RefIfInputSharedIndirect(MakeSubstring(4, 20, flat));
  CordRepRing* result = NeedsUnref(CordRepRing::Create(child));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result->length, Eq(20));
  EXPECT_THAT(ToFlats(result), ElementsAre(str1.substr(4, 20)));
}

TEST_P(CordRingCreateTest, CreateFromExternal) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz";
  auto* child = RefIfInputShared(MakeExternal(str1));
  CordRepRing* result = NeedsUnref(CordRepRing::Create(child));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result->length, Eq(str1.size()));
  EXPECT_THAT(ToFlats(result), ElementsAre(str1));
}

TEST_P(CordRingCreateFromTreeTest, CreateFromSubstringOfExternal) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz";
  auto* external = RefIfInputShared(MakeExternal(str1));
  auto* child = RefIfInputSharedIndirect(MakeSubstring(1, 24, external));
  CordRepRing* result = NeedsUnref(CordRepRing::Create(child));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result->length, Eq(24));
  EXPECT_THAT(ToFlats(result), ElementsAre(str1.substr(1, 24)));
}

TEST_P(CordRingCreateFromTreeTest, CreateFromSubstringOfLargeExternal) {
  auto* external = RefIfInputShared(MakeFakeExternal(1 << 20));
  auto str = not_a_string_view(external->base, 1 << 20)
                 .remove_prefix(1 << 19)
                 .remove_suffix(6);
  auto* child =
      RefIfInputSharedIndirect(MakeSubstring(1 << 19, (1 << 19) - 6, external));
  CordRepRing* result = NeedsUnref(CordRepRing::Create(child));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result->length, Eq(str.size()));
  EXPECT_THAT(ToRawFlats(result), ElementsAre(str));
}

TEST_P(CordRingCreateTest, Properties) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz";
  CordRepRing* result = NeedsUnref(CordRepRing::Create(MakeFlat(str1), 120));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result->head(), Eq(0));
  EXPECT_THAT(result->tail(), Eq(1));
  EXPECT_THAT(result->capacity(), Ge(120 + 1));
  EXPECT_THAT(result->capacity(), Le(2 * 120 + 1));
  EXPECT_THAT(result->entries(), Eq(1));
  EXPECT_THAT(result->begin_pos(), Eq(0));
}

TEST_P(CordRingCreateTest, EntryForNewFlat) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz";
  CordRep* child = MakeFlat(str1);
  CordRepRing* result = NeedsUnref(CordRepRing::Create(child, 120));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result->entry_child(0), Eq(child));
  EXPECT_THAT(result->entry_end_pos(0), Eq(str1.length()));
  EXPECT_THAT(result->entry_data_offset(0), Eq(0));
}

TEST_P(CordRingCreateTest, EntryForNewFlatSubstring) {
  absl::string_view str1 = "1234567890abcdefghijklmnopqrstuvwxyz";
  CordRep* child = MakeFlat(str1);
  CordRep* substring = MakeSubstring(10, 26, child);
  CordRepRing* result = NeedsUnref(CordRepRing::Create(substring, 1));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result->entry_child(0), Eq(child));
  EXPECT_THAT(result->entry_end_pos(0), Eq(26));
  EXPECT_THAT(result->entry_data_offset(0), Eq(10));
}

TEST_P(CordRingBuildTest, AppendFlat) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz";
  absl::string_view str2 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  CordRepRing* ring = CreateWithCapacity(MakeExternal(str1), 1);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, MakeFlat(str2)));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(result->length, Eq(str1.size() + str2.size()));
  EXPECT_THAT(ToFlats(result), ElementsAre(str1, str2));
}

TEST_P(CordRingBuildTest, PrependFlat) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz";
  absl::string_view str2 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  CordRepRing* ring = CreateWithCapacity(MakeExternal(str1), 1);
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, MakeFlat(str2)));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(result->length, Eq(str1.size() + str2.size()));
  EXPECT_THAT(ToFlats(result), ElementsAre(str2, str1));
}

TEST_P(CordRingBuildTest, AppendString) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz";
  absl::string_view str2 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  CordRepRing* ring = CreateWithCapacity(MakeExternal(str1), 1);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, str2));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(result->length, Eq(str1.size() + str2.size()));
  EXPECT_THAT(ToFlats(result), ElementsAre(str1, str2));
}

TEST_P(CordRingBuildTest, AppendStringHavingExtra) {
  absl::string_view str1 = "1234";
  absl::string_view str2 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  CordRepRing* ring = CreateWithCapacity(MakeFlat(str1, 26), 0);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, str2));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result->length, Eq(str1.size() + str2.size()));
  EXPECT_THAT(result, EqIfPrivate(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
}

TEST_P(CordRingBuildTest, AppendStringHavingPartialExtra) {
  absl::string_view str1 = "1234";
  absl::string_view str2 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  // Create flat with at least one extra byte. We don't expect to have sized
  // alloc and capacity rounding to grant us enough to not make it partial.
  auto* flat = MakeFlat(str1, 1);
  size_t avail = flat->flat()->Capacity() - flat->length;
  ASSERT_THAT(avail, Lt(str2.size())) << " adjust test for larger flats!";

  // Construct the flats we do expect using all of `avail`.
  absl::string_view str1a = str2.substr(0, avail);
  absl::string_view str2a = str2.substr(avail);

  CordRepRing* ring = CreateWithCapacity(flat, 1);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, str2));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result->length, Eq(str1.size() + str2.size()));
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  if (GetParam().refcount_is_one) {
    EXPECT_THAT(ToFlats(result), ElementsAre(StrCat(str1, str1a), str2a));
  } else {
    EXPECT_THAT(ToFlats(result), ElementsAre(str1, str2));
  }
}

TEST_P(CordRingBuildTest, AppendStringHavingExtraInSubstring) {
  absl::string_view str1 = "123456789_1234";
  absl::string_view str2 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  CordRep* flat = RemovePrefix(10, MakeFlat(str1, 26));
  CordRepRing* ring = CreateWithCapacity(flat, 0);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, str2));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivate(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(result->length, Eq(4 + str2.size()));
  if (GetParam().refcount_is_one) {
    EXPECT_THAT(ToFlats(result), ElementsAre(StrCat("1234", str2)));
  } else {
    EXPECT_THAT(ToFlats(result), ElementsAre("1234", str2));
  }
}

TEST_P(CordRingBuildTest, AppendStringHavingSharedExtra) {
  absl::string_view str1 = "123456789_1234";
  absl::string_view str2 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  for (int shared_type = 0; shared_type < 2; ++shared_type) {
    SCOPED_TRACE(absl::StrCat("Shared extra type ", shared_type));

    // Create a flat that is shared in some way.
    CordRep* flat = nullptr;
    CordRep* flat1 = nullptr;
    if (shared_type == 0) {
      // Shared flat
      flat = CordRep::Ref(MakeFlat(str1.substr(10), 100));
    } else if (shared_type == 1) {
      // Shared flat inside private substring
      flat1 = CordRep::Ref(MakeFlat(str1));
      flat = RemovePrefix(10, flat1);
    } else {
      // Private flat inside shared substring
      flat = CordRep::Ref(RemovePrefix(10, MakeFlat(str1, 100)));
    }

    CordRepRing* ring = CreateWithCapacity(flat, 1);
    CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, str2));
    ASSERT_THAT(result, IsValidRingBuffer());
    EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
    EXPECT_THAT(result, NeIfShared(GetParam(), ring));
    EXPECT_THAT(result->length, Eq(4 + str2.size()));
    EXPECT_THAT(ToFlats(result), ElementsAre("1234", str2));

    CordRep::Unref(shared_type == 1 ? flat1 : flat);
  }
}

TEST_P(CordRingBuildTest, AppendStringWithExtra) {
  absl::string_view str1 = "1234";
  absl::string_view str2 = "1234567890";
  absl::string_view str3 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  CordRepRing* ring = CreateWithCapacity(MakeExternal(str1), 1);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, str2, 26));
  result = CordRepRing::Append(result, str3);
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result->length, Eq(str1.size() + str2.size() + str3.size()));
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre(str1, StrCat(str2, str3)));
}

TEST_P(CordRingBuildTest, PrependString) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz";
  absl::string_view str2 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  // Use external rep to avoid appending to first flat
  CordRepRing* ring = CreateWithCapacity(MakeExternal(str1), 1);
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, str2));
  ASSERT_THAT(result, IsValidRingBuffer());
  if (GetParam().with_capacity && GetParam().refcount_is_one) {
    EXPECT_THAT(result, Eq(ring));
  } else {
    EXPECT_THAT(result, Ne(ring));
  }
  EXPECT_THAT(result->length, Eq(str1.size() + str2.size()));
  EXPECT_THAT(ToFlats(result), ElementsAre(str2, str1));
}

TEST_P(CordRingBuildTest, PrependStringHavingExtra) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz1234";
  absl::string_view str2 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  CordRep* flat = RemovePrefix(26, MakeFlat(str1));
  CordRepRing* ring = CreateWithCapacity(flat, 0);
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, str2));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivate(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(result->length, Eq(4 + str2.size()));
  if (GetParam().refcount_is_one) {
    EXPECT_THAT(ToFlats(result), ElementsAre(StrCat(str2, "1234")));
  } else {
    EXPECT_THAT(ToFlats(result), ElementsAre(str2, "1234"));
  }
}

TEST_P(CordRingBuildTest, PrependStringHavingSharedExtra) {
  absl::string_view str1 = "123456789_ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  absl::string_view str2 = "abcdefghij";
  absl::string_view str1a = str1.substr(10);
  for (int shared_type = 1; shared_type < 2; ++shared_type) {
    SCOPED_TRACE(absl::StrCat("Shared extra type ", shared_type));

    // Create a flat that is shared in some way.
    CordRep* flat = nullptr;
    CordRep* flat1 = nullptr;
    if (shared_type == 1) {
      // Shared flat inside private substring
      flat = RemovePrefix(10, flat1 = CordRep::Ref(MakeFlat(str1)));
    } else {
      // Private flat inside shared substring
      flat = CordRep::Ref(RemovePrefix(10, MakeFlat(str1, 100)));
    }

    CordRepRing* ring = CreateWithCapacity(flat, 1);
    CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, str2));
    ASSERT_THAT(result, IsValidRingBuffer());
    EXPECT_THAT(result->length, Eq(str1a.size() + str2.size()));
    EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
    EXPECT_THAT(result, NeIfShared(GetParam(), ring));
    EXPECT_THAT(ToFlats(result), ElementsAre(str2, str1a));
    CordRep::Unref(shared_type == 1 ? flat1 : flat);
  }
}

TEST_P(CordRingBuildTest, PrependStringWithExtra) {
  absl::string_view str1 = "1234";
  absl::string_view str2 = "1234567890";
  absl::string_view str3 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  CordRepRing* ring = CreateWithCapacity(MakeExternal(str1), 1);
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, str2, 26));
  ASSERT_THAT(result, IsValidRingBuffer());
  result = CordRepRing::Prepend(result, str3);
  EXPECT_THAT(result->length, Eq(str1.size() + str2.size() + str3.size()));
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre(StrCat(str3, str2), str1));
}

TEST_P(CordRingBuildTest, AppendPrependStringMix) {
  const auto& flats = kFoxFlats;
  CordRepRing* ring = CreateWithCapacity(MakeFlat(flats[4]), 8);
  CordRepRing* result = ring;
  for (int i = 1; i <= 4; ++i) {
    result = CordRepRing::Prepend(result, flats[4 - i]);
    result = CordRepRing::Append(result, flats[4 + i]);
  }
  NeedsUnref(result);
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(ToString(result), kFox);
}

TEST_P(CordRingBuildTest, AppendPrependStringMixWithExtra) {
  const auto& flats = kFoxFlats;
  CordRepRing* ring = CreateWithCapacity(MakeFlat(flats[4], 100), 8);
  CordRepRing* result = ring;
  for (int i = 1; i <= 4; ++i) {
    result = CordRepRing::Prepend(result, flats[4 - i], 100);
    result = CordRepRing::Append(result, flats[4 + i], 100);
  }
  NeedsUnref(result);
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  if (GetParam().refcount_is_one) {
    EXPECT_THAT(ToFlats(result),
                ElementsAre("The quick brown fox ", "jumps over the lazy dog"));
  } else {
    EXPECT_THAT(ToFlats(result), ElementsAre("The quick brown fox ", "jumps ",
                                             "over the lazy dog"));
  }
}

TEST_P(CordRingBuildTest, AppendPrependStringMixWithPrependedExtra) {
  const auto& flats = kFoxFlats;
  CordRep* flat = MakeFlat(StrCat(std::string(50, '.'), flats[4]), 50);
  CordRepRing* ring = CreateWithCapacity(RemovePrefix(50, flat), 0);
  CordRepRing* result = ring;
  for (int i = 1; i <= 4; ++i) {
    result = CordRepRing::Prepend(result, flats[4 - i], 100);
    result = CordRepRing::Append(result, flats[4 + i], 100);
  }
  result = NeedsUnref(result);
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivate(GetParam(), ring));
  if (GetParam().refcount_is_one) {
    EXPECT_THAT(ToFlats(result), ElementsAre(kFox));
  } else {
    EXPECT_THAT(ToFlats(result), ElementsAre("The quick brown fox ", "jumps ",
                                             "over the lazy dog"));
  }
}

TEST_P(CordRingSubTest, SubRing) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  string_view all = kFox;
  for (size_t offset = 0; offset < all.size() - 1; ++offset) {
    CordRepRing* ring = RefIfShared(FromFlats(flats, composition));
    CordRepRing* result = CordRepRing::SubRing(ring, offset, 0);
    EXPECT_THAT(result, nullptr);

    for (size_t len = 1; len < all.size() - offset; ++len) {
      ring = RefIfShared(FromFlats(flats, composition));
      result = NeedsUnref(CordRepRing::SubRing(ring, offset, len));
      ASSERT_THAT(result, IsValidRingBuffer());
      ASSERT_THAT(result, EqIfPrivate(GetParam(), ring));
      ASSERT_THAT(result, NeIfShared(GetParam(), ring));
      ASSERT_THAT(ToString(result), Eq(all.substr(offset, len)));
    }
  }
}

TEST_P(CordRingSubTest, SubRingFromLargeExternal) {
  auto composition = RandomComposition();
  std::string large_string(1 << 20, '.');
  const char* flats[] = {
      "abcdefghijklmnopqrstuvwxyz",
      large_string.c_str(),
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
  };
  std::string buffer = absl::StrCat(flats[0], flats[1], flats[2]);
  absl::string_view all = buffer;
  for (size_t offset = 0; offset < 30; ++offset) {
    CordRepRing* ring = RefIfShared(FromFlats(flats, composition));
    CordRepRing* result = CordRepRing::SubRing(ring, offset, 0);
    EXPECT_THAT(result, nullptr);

    for (size_t len = all.size() - 30; len < all.size() - offset; ++len) {
      ring = RefIfShared(FromFlats(flats, composition));
      result = NeedsUnref(CordRepRing::SubRing(ring, offset, len));
      ASSERT_THAT(result, IsValidRingBuffer());
      ASSERT_THAT(result, EqIfPrivate(GetParam(), ring));
      ASSERT_THAT(result, NeIfShared(GetParam(), ring));
      auto str = ToString(result);
      ASSERT_THAT(str, SizeIs(len));
      ASSERT_THAT(str, Eq(all.substr(offset, len)));
    }
  }
}

TEST_P(CordRingSubTest, RemovePrefix) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  string_view all = kFox;
  CordRepRing* ring = RefIfShared(FromFlats(flats, composition));
  CordRepRing* result = CordRepRing::RemovePrefix(ring, all.size());
  EXPECT_THAT(result, nullptr);

  for (size_t len = 1; len < all.size(); ++len) {
    ring = RefIfShared(FromFlats(flats, composition));
    result = NeedsUnref(CordRepRing::RemovePrefix(ring, len));
    ASSERT_THAT(result, IsValidRingBuffer());
    EXPECT_THAT(result, EqIfPrivate(GetParam(), ring));
    ASSERT_THAT(result, NeIfShared(GetParam(), ring));
    EXPECT_THAT(ToString(result), Eq(all.substr(len)));
  }
}

TEST_P(CordRingSubTest, RemovePrefixFromLargeExternal) {
  CordRepExternal* external1 = MakeFakeExternal(1 << 20);
  CordRepExternal* external2 = MakeFakeExternal(1 << 20);
  CordRepRing* ring = CordRepRing::Create(external1, 1);
  ring = CordRepRing::Append(ring, external2);
  CordRepRing* result = NeedsUnref(CordRepRing::RemovePrefix(ring, 1 << 16));
  EXPECT_THAT(
      ToRawFlats(result),
      ElementsAre(
          not_a_string_view(external1->base, 1 << 20).remove_prefix(1 << 16),
          not_a_string_view(external2->base, 1 << 20)));
}

TEST_P(CordRingSubTest, RemoveSuffix) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  string_view all = kFox;
  CordRepRing* ring = RefIfShared(FromFlats(flats, composition));
  CordRepRing* result = CordRepRing::RemoveSuffix(ring, all.size());
  EXPECT_THAT(result, nullptr);

  for (size_t len = 1; len < all.size(); ++len) {
    ring = RefIfShared(FromFlats(flats, composition));
    result = NeedsUnref(CordRepRing::RemoveSuffix(ring, len));
    ASSERT_THAT(result, IsValidRingBuffer());
    ASSERT_THAT(result, EqIfPrivate(GetParam(), ring));
    ASSERT_THAT(result, NeIfShared(GetParam(), ring));
    ASSERT_THAT(ToString(result), Eq(all.substr(0, all.size() - len)));
  }
}

TEST_P(CordRingSubTest, AppendRing) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats).subspan(1);
  CordRepRing* ring = CreateWithCapacity(MakeFlat(kFoxFlats[0]), flats.size());
  CordRepRing* child = FromFlats(flats, composition);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, child));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivate(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAreArray(kFoxFlats));
}

TEST_P(CordRingBuildInputTest, AppendRingWithFlatOffset) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Head"), flats.size());
  CordRep* child = RefIfInputSharedIndirect(FromFlats(flats, composition));
  CordRep* stripped = RemovePrefix(10, child);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre("Head", "brown ", "fox ", "jumps ",
                                           "over ", "the ", "lazy ", "dog"));
}

TEST_P(CordRingBuildInputTest, AppendRingWithBrokenOffset) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Head"), flats.size());
  CordRep* child = RefIfInputSharedIndirect(FromFlats(flats, composition));
  CordRep* stripped = RemovePrefix(21, child);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result),
              ElementsAre("Head", "umps ", "over ", "the ", "lazy ", "dog"));
}

TEST_P(CordRingBuildInputTest, AppendRingWithFlatLength) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Head"), flats.size());
  CordRep* child = RefIfInputSharedIndirect(FromFlats(flats, composition));
  CordRep* stripped = RemoveSuffix(8, child);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre("Head", "The ", "quick ", "brown ",
                                           "fox ", "jumps ", "over ", "the "));
}

TEST_P(CordRingBuildTest, AppendRingWithBrokenFlatLength) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Head"), flats.size());
  CordRep* child = RefIfInputSharedIndirect(FromFlats(flats, composition));
  CordRep* stripped = RemoveSuffix(15, child);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre("Head", "The ", "quick ", "brown ",
                                           "fox ", "jumps ", "ov"));
}

TEST_P(CordRingBuildTest, AppendRingMiddlePiece) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Head"), flats.size());
  CordRep* child = RefIfInputSharedIndirect(FromFlats(flats, composition));
  CordRep* stripped = MakeSubstring(7, child->length - 27, child);
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result),
              ElementsAre("Head", "ck ", "brown ", "fox ", "jum"));
}

TEST_P(CordRingBuildTest, AppendRingSinglePiece) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Head"), flats.size());
  CordRep* child = RefIfInputSharedIndirect(FromFlats(flats, composition));
  CordRep* stripped = RefIfInputShared(MakeSubstring(11, 3, child));
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre("Head", "row"));
}

TEST_P(CordRingBuildInputTest, AppendRingSinglePieceWithPrefix) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  size_t extra_capacity = 1 + (GetParam().with_capacity ? flats.size() : 0);
  CordRepRing* ring = CordRepRing::Create(MakeFlat("Head"), extra_capacity);
  ring->SetCapacityForTesting(1 + extra_capacity);
  ring = RefIfShared(CordRepRing::Prepend(ring, MakeFlat("Prepend")));
  assert(ring->IsValid(std::cout));
  CordRepRing* child = RefIfInputSharedIndirect(FromFlats(flats, composition));
  CordRep* stripped = RefIfInputShared(MakeSubstring(11, 3, child));
  CordRepRing* result = NeedsUnref(CordRepRing::Append(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre("Prepend", "Head", "row"));
}

TEST_P(CordRingBuildInputTest, PrependRing) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto fox = MakeSpan(kFoxFlats);
  auto flats = MakeSpan(fox).subspan(0, fox.size() - 1);
  CordRepRing* ring = CreateWithCapacity(MakeFlat(fox.back()), flats.size());
  CordRepRing* child = RefIfInputShared(FromFlats(flats, composition));
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, child));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAreArray(kFoxFlats));
}

TEST_P(CordRingBuildInputTest, PrependRingWithFlatOffset) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Tail"), flats.size());
  CordRep* child = RefIfInputShared(FromFlats(flats, composition));
  CordRep* stripped = RefIfInputSharedIndirect(RemovePrefix(10, child));
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre("brown ", "fox ", "jumps ", "over ",
                                           "the ", "lazy ", "dog", "Tail"));
}

TEST_P(CordRingBuildInputTest, PrependRingWithBrokenOffset) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Tail"), flats.size());
  CordRep* child = RefIfInputShared(FromFlats(flats, composition));
  CordRep* stripped = RefIfInputSharedIndirect(RemovePrefix(21, child));
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, stripped));
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result),
              ElementsAre("umps ", "over ", "the ", "lazy ", "dog", "Tail"));
}

TEST_P(CordRingBuildInputTest, PrependRingWithFlatLength) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Tail"), flats.size());
  CordRep* child = RefIfInputShared(FromFlats(flats, composition));
  CordRep* stripped = RefIfInputSharedIndirect(RemoveSuffix(8, child));
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre("The ", "quick ", "brown ", "fox ",
                                           "jumps ", "over ", "the ", "Tail"));
}

TEST_P(CordRingBuildInputTest, PrependRingWithBrokenFlatLength) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Tail"), flats.size());
  CordRep* child = RefIfInputShared(FromFlats(flats, composition));
  CordRep* stripped = RefIfInputSharedIndirect(RemoveSuffix(15, child));
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre("The ", "quick ", "brown ", "fox ",
                                           "jumps ", "ov", "Tail"));
}

TEST_P(CordRingBuildInputTest, PrependRingMiddlePiece) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Tail"), flats.size());
  CordRep* child = RefIfInputShared(FromFlats(flats, composition));
  CordRep* stripped =
      RefIfInputSharedIndirect(MakeSubstring(7, child->length - 27, child));
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result),
              ElementsAre("ck ", "brown ", "fox ", "jum", "Tail"));
}

TEST_P(CordRingBuildInputTest, PrependRingSinglePiece) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CreateWithCapacity(MakeFlat("Tail"), flats.size());
  CordRep* child = RefIfInputShared(FromFlats(flats, composition));
  CordRep* stripped = RefIfInputSharedIndirect(MakeSubstring(11, 3, child));
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre("row", "Tail"));
}

TEST_P(CordRingBuildInputTest, PrependRingSinglePieceWithPrefix) {
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  auto flats = MakeSpan(kFoxFlats);
  size_t extra_capacity = 1 + (GetParam().with_capacity ? flats.size() : 0);
  CordRepRing* ring = CordRepRing::Create(MakeFlat("Tail"), extra_capacity);
  ring->SetCapacityForTesting(1 + extra_capacity);
  ring = RefIfShared(CordRepRing::Prepend(ring, MakeFlat("Prepend")));
  CordRep* child = RefIfInputShared(FromFlats(flats, composition));
  CordRep* stripped = RefIfInputSharedIndirect(MakeSubstring(11, 3, child));
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, stripped));
  ASSERT_THAT(result, IsValidRingBuffer());
  EXPECT_THAT(result, EqIfPrivateAndCapacity(GetParam(), ring));
  EXPECT_THAT(result, NeIfShared(GetParam(), ring));
  EXPECT_THAT(ToFlats(result), ElementsAre("row", "Prepend", "Tail"));
}

TEST_F(CordRingTest, Find) {
  constexpr const char* flats[] = {
      "abcdefghij", "klmnopqrst", "uvwxyz",     "ABCDEFGHIJ",
      "KLMNOPQRST", "UVWXYZ",     "1234567890", "~!@#$%^&*()_",
      "+-=",        "[]\\{}|;':", ",/<>?",      "."};
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  CordRepRing* ring = NeedsUnref(FromFlats(flats, composition));
  std::string value = ToString(ring);
  for (int i = 0; i < value.length(); ++i) {
    CordRepRing::Position found = ring->Find(i);
    auto data = ring->entry_data(found.index);
    ASSERT_THAT(found.offset, Lt(data.length()));
    ASSERT_THAT(data[found.offset], Eq(value[i]));
  }
}

TEST_F(CordRingTest, FindWithHint) {
  constexpr const char* flats[] = {
      "abcdefghij", "klmnopqrst", "uvwxyz",     "ABCDEFGHIJ",
      "KLMNOPQRST", "UVWXYZ",     "1234567890", "~!@#$%^&*()_",
      "+-=",        "[]\\{}|;':", ",/<>?",      "."};
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  CordRepRing* ring = NeedsUnref(FromFlats(flats, composition));
  std::string value = ToString(ring);

#if defined(GTEST_HAS_DEATH_TEST)
  // Test hint beyond valid position
  index_type head = ring->head();
  EXPECT_DEBUG_DEATH(ring->Find(ring->advance(head), 0), ".*");
  EXPECT_DEBUG_DEATH(ring->Find(ring->advance(head), 9), ".*");
  EXPECT_DEBUG_DEATH(ring->Find(ring->advance(head, 3), 24), ".*");
#endif

  int flat_pos = 0;
  size_t flat_offset = 0;
  for (auto sflat : flats) {
    string_view flat(sflat);
    for (int offset = 0; offset < flat.length(); ++offset) {
      for (int start = 0; start <= flat_pos; ++start) {
        index_type hint = ring->advance(ring->head(), start);
        CordRepRing::Position found = ring->Find(hint, flat_offset + offset);
        ASSERT_THAT(found.index, Eq(ring->advance(ring->head(), flat_pos)));
        ASSERT_THAT(found.offset, Eq(offset));
      }
    }
    ++flat_pos;
    flat_offset += flat.length();
  }
}

TEST_F(CordRingTest, FindInLargeRing) {
  constexpr const char* flats[] = {
      "abcdefghij", "klmnopqrst", "uvwxyz",     "ABCDEFGHIJ",
      "KLMNOPQRST", "UVWXYZ",     "1234567890", "~!@#$%^&*()_",
      "+-=",        "[]\\{}|;':", ",/<>?",      "."};
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  CordRepRing* ring = FromFlats(flats, composition);
  for (int i = 0; i < 13; ++i) {
    ring = CordRepRing::Append(ring, FromFlats(flats, composition));
  }
  NeedsUnref(ring);
  std::string value = ToString(ring);
  for (int i = 0; i < value.length(); ++i) {
    CordRepRing::Position pos = ring->Find(i);
    auto data = ring->entry_data(pos.index);
    ASSERT_THAT(pos.offset, Lt(data.length()));
    ASSERT_THAT(data[pos.offset], Eq(value[i]));
  }
}

TEST_F(CordRingTest, FindTail) {
  constexpr const char* flats[] = {
      "abcdefghij", "klmnopqrst", "uvwxyz",     "ABCDEFGHIJ",
      "KLMNOPQRST", "UVWXYZ",     "1234567890", "~!@#$%^&*()_",
      "+-=",        "[]\\{}|;':", ",/<>?",      "."};
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  CordRepRing* ring = NeedsUnref(FromFlats(flats, composition));
  std::string value = ToString(ring);

  for (int i = 0; i < value.length(); ++i) {
    CordRepRing::Position pos = ring->FindTail(i + 1);
    auto data = ring->entry_data(ring->retreat(pos.index));
    ASSERT_THAT(pos.offset, Lt(data.length()));
    ASSERT_THAT(data[data.length() - pos.offset - 1], Eq(value[i]));
  }
}

TEST_F(CordRingTest, FindTailWithHint) {
  constexpr const char* flats[] = {
      "abcdefghij", "klmnopqrst", "uvwxyz",     "ABCDEFGHIJ",
      "KLMNOPQRST", "UVWXYZ",     "1234567890", "~!@#$%^&*()_",
      "+-=",        "[]\\{}|;':", ",/<>?",      "."};
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  CordRepRing* ring = NeedsUnref(FromFlats(flats, composition));
  std::string value = ToString(ring);

  // Test hint beyond valid position
#if defined(GTEST_HAS_DEATH_TEST)
  index_type head = ring->head();
  EXPECT_DEBUG_DEATH(ring->FindTail(ring->advance(head), 1), ".*");
  EXPECT_DEBUG_DEATH(ring->FindTail(ring->advance(head), 10), ".*");
  EXPECT_DEBUG_DEATH(ring->FindTail(ring->advance(head, 3), 26), ".*");
#endif

  for (int i = 0; i < value.length(); ++i) {
    CordRepRing::Position pos = ring->FindTail(i + 1);
    auto data = ring->entry_data(ring->retreat(pos.index));
    ASSERT_THAT(pos.offset, Lt(data.length()));
    ASSERT_THAT(data[data.length() - pos.offset - 1], Eq(value[i]));
  }
}

TEST_F(CordRingTest, FindTailInLargeRing) {
  constexpr const char* flats[] = {
      "abcdefghij", "klmnopqrst", "uvwxyz",     "ABCDEFGHIJ",
      "KLMNOPQRST", "UVWXYZ",     "1234567890", "~!@#$%^&*()_",
      "+-=",        "[]\\{}|;':", ",/<>?",      "."};
  auto composition = RandomComposition();
  SCOPED_TRACE(ToString(composition));
  CordRepRing* ring = FromFlats(flats, composition);
  for (int i = 0; i < 13; ++i) {
    ring = CordRepRing::Append(ring, FromFlats(flats, composition));
  }
  NeedsUnref(ring);
  std::string value = ToString(ring);
  for (int i = 0; i < value.length(); ++i) {
    CordRepRing::Position pos = ring->FindTail(i + 1);
    auto data = ring->entry_data(ring->retreat(pos.index));
    ASSERT_THAT(pos.offset, Lt(data.length()));
    ASSERT_THAT(data[data.length() - pos.offset - 1], Eq(value[i]));
  }
}

TEST_F(CordRingTest, GetCharacter) {
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = CordRepRing::Create(MakeFlat("Tail"), flats.size());
  CordRep* child = FromFlats(flats, kAppend);
  CordRepRing* result = NeedsUnref(CordRepRing::Prepend(ring, child));
  std::string value = ToString(result);
  for (int i = 0; i < value.length(); ++i) {
    ASSERT_THAT(result->GetCharacter(i), Eq(value[i]));
  }
}

TEST_F(CordRingTest, GetCharacterWithSubstring) {
  absl::string_view str1 = "abcdefghijklmnopqrstuvwxyz";
  auto* child = MakeSubstring(4, 20, MakeFlat(str1));
  CordRepRing* result = NeedsUnref(CordRepRing::Create(child));
  ASSERT_THAT(result, IsValidRingBuffer());
  std::string value = ToString(result);
  for (int i = 0; i < value.length(); ++i) {
    ASSERT_THAT(result->GetCharacter(i), Eq(value[i]));
  }
}

TEST_F(CordRingTest, IsFlatSingleFlat) {
  for (bool external : {false, true}) {
    SCOPED_TRACE(external ? "With External" : "With Flat");
    absl::string_view str = "Hello world";
    CordRep* rep = external ? MakeExternal(str) : MakeFlat(str);
    CordRepRing* ring = NeedsUnref(CordRepRing::Create(rep));

    // The ring is a single non-fragmented flat:
    absl::string_view fragment;
    EXPECT_TRUE(ring->IsFlat(nullptr));
    EXPECT_TRUE(ring->IsFlat(&fragment));
    EXPECT_THAT(fragment, Eq("Hello world"));
    fragment = "";
    EXPECT_TRUE(ring->IsFlat(0, 11, nullptr));
    EXPECT_TRUE(ring->IsFlat(0, 11, &fragment));
    EXPECT_THAT(fragment, Eq("Hello world"));

    // Arbitrary ranges must check true as well.
    EXPECT_TRUE(ring->IsFlat(1, 4, &fragment));
    EXPECT_THAT(fragment, Eq("ello"));
    EXPECT_TRUE(ring->IsFlat(6, 5, &fragment));
    EXPECT_THAT(fragment, Eq("world"));
  }
}

TEST_F(CordRingTest, IsFlatMultiFlat) {
  for (bool external : {false, true}) {
    SCOPED_TRACE(external ? "With External" : "With Flat");
    absl::string_view str1 = "Hello world";
    absl::string_view str2 = "Halt and catch fire";
    CordRep* rep1 = external ? MakeExternal(str1) : MakeFlat(str1);
    CordRep* rep2 = external ? MakeExternal(str2) : MakeFlat(str2);
    CordRepRing* ring = CordRepRing::Append(CordRepRing::Create(rep1), rep2);
    NeedsUnref(ring);

    // The ring is fragmented, IsFlat() on the entire cord must be false.
    EXPECT_FALSE(ring->IsFlat(nullptr));
    absl::string_view fragment = "Don't touch this";
    EXPECT_FALSE(ring->IsFlat(&fragment));
    EXPECT_THAT(fragment, Eq("Don't touch this"));

    // Check for ranges exactly within both flats.
    EXPECT_TRUE(ring->IsFlat(0, 11, &fragment));
    EXPECT_THAT(fragment, Eq("Hello world"));
    EXPECT_TRUE(ring->IsFlat(11, 19, &fragment));
    EXPECT_THAT(fragment, Eq("Halt and catch fire"));

    // Check for arbitrary partial range inside each flat.
    EXPECT_TRUE(ring->IsFlat(1, 4, &fragment));
    EXPECT_THAT(fragment, "ello");
    EXPECT_TRUE(ring->IsFlat(26, 4, &fragment));
    EXPECT_THAT(fragment, "fire");

    // Check ranges spanning across both flats
    fragment = "Don't touch this";
    EXPECT_FALSE(ring->IsFlat(1, 18, &fragment));
    EXPECT_FALSE(ring->IsFlat(10, 2, &fragment));
    EXPECT_THAT(fragment, Eq("Don't touch this"));
  }
}

TEST_F(CordRingTest, Dump) {
  std::stringstream ss;
  auto flats = MakeSpan(kFoxFlats);
  CordRepRing* ring = NeedsUnref(FromFlats(flats, kPrepend));
  ss << *ring;
}

}  // namespace
ABSL_NAMESPACE_END
}  // namespace absl
