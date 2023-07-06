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
#include "absl/debugging/leak_check.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_ring.h"
#include "absl/strings/internal/cord_rep_ring_reader.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

using testing::Eq;

// Creates a flat for testing
CordRep* MakeFlat(absl::string_view s) {
  CordRepFlat* flat = CordRepFlat::New(s.length());
  memcpy(flat->Data(), s.data(), s.length());
  flat->length = s.length();
  return flat;
}

CordRepRing* FromFlats(Span<absl::string_view const> flats) {
  CordRepRing* ring = CordRepRing::Create(MakeFlat(flats[0]), flats.size() - 1);
  for (int i = 1; i < flats.size(); ++i) {
    ring = CordRepRing::Append(ring, MakeFlat(flats[i]));
  }
  return ring;
}

std::array<absl::string_view, 12> TestFlats() {
  return {"abcdefghij", "klmnopqrst", "uvwxyz",     "ABCDEFGHIJ",
          "KLMNOPQRST", "UVWXYZ",     "1234567890", "~!@#$%^&*()_",
          "+-=",        "[]\\{}|;':", ",/<>?",      "."};
}

TEST(CordRingReaderTest, DefaultInstance) {
  CordRepRingReader reader;
  EXPECT_FALSE(static_cast<bool>(reader));
  EXPECT_THAT(reader.ring(), Eq(nullptr));
#ifndef NDEBUG
  EXPECT_DEATH_IF_SUPPORTED(reader.length(), ".*");
  EXPECT_DEATH_IF_SUPPORTED(reader.consumed(), ".*");
  EXPECT_DEATH_IF_SUPPORTED(reader.remaining(), ".*");
  EXPECT_DEATH_IF_SUPPORTED(reader.Next(), ".*");
  EXPECT_DEATH_IF_SUPPORTED(reader.Seek(0), ".*");
#endif
}

TEST(CordRingReaderTest, Reset) {
  CordRepRingReader reader;
  auto flats = TestFlats();
  CordRepRing* ring = FromFlats(flats);

  absl::string_view first = reader.Reset(ring);
  EXPECT_THAT(first, Eq(flats[0]));
  EXPECT_TRUE(static_cast<bool>(reader));
  EXPECT_THAT(reader.ring(), Eq(ring));
  EXPECT_THAT(reader.index(), Eq(ring->head()));
  EXPECT_THAT(reader.node(), Eq(ring->entry_child(ring->head())));
  EXPECT_THAT(reader.length(), Eq(ring->length));
  EXPECT_THAT(reader.consumed(), Eq(flats[0].length()));
  EXPECT_THAT(reader.remaining(), Eq(ring->length - reader.consumed()));

  reader.Reset();
  EXPECT_FALSE(static_cast<bool>(reader));
  EXPECT_THAT(reader.ring(), Eq(nullptr));

  CordRep::Unref(ring);
}

TEST(CordRingReaderTest, Next) {
  CordRepRingReader reader;
  auto flats = TestFlats();
  CordRepRing* ring = FromFlats(flats);
  CordRepRing::index_type head = ring->head();

  reader.Reset(ring);
  size_t consumed = reader.consumed();
  size_t remaining = reader.remaining();
  for (int i = 1; i < flats.size(); ++i) {
    CordRepRing::index_type index = ring->advance(head, i);
    consumed += flats[i].length();
    remaining -= flats[i].length();
    absl::string_view next = reader.Next();
    ASSERT_THAT(next, Eq(flats[i]));
    ASSERT_THAT(reader.index(), Eq(index));
    ASSERT_THAT(reader.node(), Eq(ring->entry_child(index)));
    ASSERT_THAT(reader.consumed(), Eq(consumed));
    ASSERT_THAT(reader.remaining(), Eq(remaining));
  }

#ifndef NDEBUG
  EXPECT_DEATH_IF_SUPPORTED(reader.Next(), ".*");
#endif

  CordRep::Unref(ring);
}

TEST(CordRingReaderTest, SeekForward) {
  CordRepRingReader reader;
  auto flats = TestFlats();
  CordRepRing* ring = FromFlats(flats);
  CordRepRing::index_type head = ring->head();

  reader.Reset(ring);
  size_t consumed = 0;
  size_t remaining = ring->length;
  for (int i = 0; i < flats.size(); ++i) {
    CordRepRing::index_type index = ring->advance(head, i);
    size_t offset = consumed;
    consumed += flats[i].length();
    remaining -= flats[i].length();
    for (int off = 0; off < flats[i].length(); ++off) {
      absl::string_view chunk = reader.Seek(offset + off);
      ASSERT_THAT(chunk, Eq(flats[i].substr(off)));
      ASSERT_THAT(reader.index(), Eq(index));
      ASSERT_THAT(reader.node(), Eq(ring->entry_child(index)));
      ASSERT_THAT(reader.consumed(), Eq(consumed));
      ASSERT_THAT(reader.remaining(), Eq(remaining));
    }
  }

  CordRep::Unref(ring);
}

TEST(CordRingReaderTest, SeekBackward) {
  CordRepRingReader reader;
  auto flats = TestFlats();
  CordRepRing* ring = FromFlats(flats);
  CordRepRing::index_type head = ring->head();

  reader.Reset(ring);
  size_t consumed = ring->length;
  size_t remaining = 0;
  for (int i = flats.size() - 1; i >= 0; --i) {
    CordRepRing::index_type index = ring->advance(head, i);
    size_t offset = consumed - flats[i].length();
    for (int off = 0; off < flats[i].length(); ++off) {
      absl::string_view chunk = reader.Seek(offset + off);
      ASSERT_THAT(chunk, Eq(flats[i].substr(off)));
      ASSERT_THAT(reader.index(), Eq(index));
      ASSERT_THAT(reader.node(), Eq(ring->entry_child(index)));
      ASSERT_THAT(reader.consumed(), Eq(consumed));
      ASSERT_THAT(reader.remaining(), Eq(remaining));
    }
    consumed -= flats[i].length();
    remaining += flats[i].length();
  }
#ifndef NDEBUG
  EXPECT_DEATH_IF_SUPPORTED(reader.Seek(ring->length), ".*");
#endif
  CordRep::Unref(ring);
}

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
