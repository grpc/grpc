// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Unit test for memutil.cc

#include "absl/strings/internal/memutil.h"

#include <cstdlib>

#include "gtest/gtest.h"
#include "absl/strings/ascii.h"

namespace {

static char* memcasechr(const char* s, int c, size_t slen) {
  c = absl::ascii_tolower(c);
  for (; slen; ++s, --slen) {
    if (absl::ascii_tolower(*s) == c) return const_cast<char*>(s);
  }
  return nullptr;
}

static const char* memcasematch(const char* phaystack, size_t haylen,
                                const char* pneedle, size_t neelen) {
  if (0 == neelen) {
    return phaystack;  // even if haylen is 0
  }
  if (haylen < neelen) return nullptr;

  const char* match;
  const char* hayend = phaystack + haylen - neelen + 1;
  while ((match = static_cast<char*>(
              memcasechr(phaystack, pneedle[0], hayend - phaystack)))) {
    if (absl::strings_internal::memcasecmp(match, pneedle, neelen) == 0)
      return match;
    else
      phaystack = match + 1;
  }
  return nullptr;
}

TEST(MemUtilTest, AllTests) {
  // check memutil functions
  char a[1000];
  absl::strings_internal::memcat(a, 0, "hello", sizeof("hello") - 1);
  absl::strings_internal::memcat(a, 5, " there", sizeof(" there") - 1);

  EXPECT_EQ(absl::strings_internal::memcasecmp(a, "heLLO there",
                                               sizeof("hello there") - 1),
            0);
  EXPECT_EQ(absl::strings_internal::memcasecmp(a, "heLLO therf",
                                               sizeof("hello there") - 1),
            -1);
  EXPECT_EQ(absl::strings_internal::memcasecmp(a, "heLLO therf",
                                               sizeof("hello there") - 2),
            0);
  EXPECT_EQ(absl::strings_internal::memcasecmp(a, "whatever", 0), 0);

  char* p = absl::strings_internal::memdup("hello", 5);
  free(p);

  p = absl::strings_internal::memrchr("hello there", 'e',
                                      sizeof("hello there") - 1);
  EXPECT_TRUE(p && p[-1] == 'r');
  p = absl::strings_internal::memrchr("hello there", 'e',
                                      sizeof("hello there") - 2);
  EXPECT_TRUE(p && p[-1] == 'h');
  p = absl::strings_internal::memrchr("hello there", 'u',
                                      sizeof("hello there") - 1);
  EXPECT_TRUE(p == nullptr);

  int len = absl::strings_internal::memspn("hello there",
                                           sizeof("hello there") - 1, "hole");
  EXPECT_EQ(len, sizeof("hello") - 1);
  len = absl::strings_internal::memspn("hello there", sizeof("hello there") - 1,
                                       "u");
  EXPECT_EQ(len, 0);
  len = absl::strings_internal::memspn("hello there", sizeof("hello there") - 1,
                                       "");
  EXPECT_EQ(len, 0);
  len = absl::strings_internal::memspn("hello there", sizeof("hello there") - 1,
                                       "trole h");
  EXPECT_EQ(len, sizeof("hello there") - 1);
  len = absl::strings_internal::memspn("hello there!",
                                       sizeof("hello there!") - 1, "trole h");
  EXPECT_EQ(len, sizeof("hello there") - 1);
  len = absl::strings_internal::memspn("hello there!",
                                       sizeof("hello there!") - 2, "trole h!");
  EXPECT_EQ(len, sizeof("hello there!") - 2);

  len = absl::strings_internal::memcspn("hello there",
                                        sizeof("hello there") - 1, "leho");
  EXPECT_EQ(len, 0);
  len = absl::strings_internal::memcspn("hello there",
                                        sizeof("hello there") - 1, "u");
  EXPECT_EQ(len, sizeof("hello there") - 1);
  len = absl::strings_internal::memcspn("hello there",
                                        sizeof("hello there") - 1, "");
  EXPECT_EQ(len, sizeof("hello there") - 1);
  len = absl::strings_internal::memcspn("hello there",
                                        sizeof("hello there") - 1, " ");
  EXPECT_EQ(len, 5);

  p = absl::strings_internal::mempbrk("hello there", sizeof("hello there") - 1,
                                      "leho");
  EXPECT_TRUE(p && p[1] == 'e' && p[2] == 'l');
  p = absl::strings_internal::mempbrk("hello there", sizeof("hello there") - 1,
                                      "nu");
  EXPECT_TRUE(p == nullptr);
  p = absl::strings_internal::mempbrk("hello there!",
                                      sizeof("hello there!") - 2, "!");
  EXPECT_TRUE(p == nullptr);
  p = absl::strings_internal::mempbrk("hello there", sizeof("hello there") - 1,
                                      " t ");
  EXPECT_TRUE(p && p[-1] == 'o' && p[1] == 't');

  {
    const char kHaystack[] = "0123456789";
    EXPECT_EQ(absl::strings_internal::memmem(kHaystack, 0, "", 0), kHaystack);
    EXPECT_EQ(absl::strings_internal::memmem(kHaystack, 10, "012", 3),
              kHaystack);
    EXPECT_EQ(absl::strings_internal::memmem(kHaystack, 10, "0xx", 1),
              kHaystack);
    EXPECT_EQ(absl::strings_internal::memmem(kHaystack, 10, "789", 3),
              kHaystack + 7);
    EXPECT_EQ(absl::strings_internal::memmem(kHaystack, 10, "9xx", 1),
              kHaystack + 9);
    EXPECT_TRUE(absl::strings_internal::memmem(kHaystack, 10, "9xx", 3) ==
                nullptr);
    EXPECT_TRUE(absl::strings_internal::memmem(kHaystack, 10, "xxx", 1) ==
                nullptr);
  }
  {
    const char kHaystack[] = "aBcDeFgHiJ";
    EXPECT_EQ(absl::strings_internal::memcasemem(kHaystack, 0, "", 0),
              kHaystack);
    EXPECT_EQ(absl::strings_internal::memcasemem(kHaystack, 10, "Abc", 3),
              kHaystack);
    EXPECT_EQ(absl::strings_internal::memcasemem(kHaystack, 10, "Axx", 1),
              kHaystack);
    EXPECT_EQ(absl::strings_internal::memcasemem(kHaystack, 10, "hIj", 3),
              kHaystack + 7);
    EXPECT_EQ(absl::strings_internal::memcasemem(kHaystack, 10, "jxx", 1),
              kHaystack + 9);
    EXPECT_TRUE(absl::strings_internal::memcasemem(kHaystack, 10, "jxx", 3) ==
                nullptr);
    EXPECT_TRUE(absl::strings_internal::memcasemem(kHaystack, 10, "xxx", 1) ==
                nullptr);
  }
  {
    const char kHaystack[] = "0123456789";
    EXPECT_EQ(absl::strings_internal::memmatch(kHaystack, 0, "", 0), kHaystack);
    EXPECT_EQ(absl::strings_internal::memmatch(kHaystack, 10, "012", 3),
              kHaystack);
    EXPECT_EQ(absl::strings_internal::memmatch(kHaystack, 10, "0xx", 1),
              kHaystack);
    EXPECT_EQ(absl::strings_internal::memmatch(kHaystack, 10, "789", 3),
              kHaystack + 7);
    EXPECT_EQ(absl::strings_internal::memmatch(kHaystack, 10, "9xx", 1),
              kHaystack + 9);
    EXPECT_TRUE(absl::strings_internal::memmatch(kHaystack, 10, "9xx", 3) ==
                nullptr);
    EXPECT_TRUE(absl::strings_internal::memmatch(kHaystack, 10, "xxx", 1) ==
                nullptr);
  }
}

}  // namespace
