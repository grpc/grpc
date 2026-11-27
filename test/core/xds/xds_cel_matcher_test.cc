//
// Copyright 2026 gRPC authors.
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

#include "src/core/xds/grpc/cel/xds_cel_matcher.h"

#include <memory>
#include <optional>
#include <utility>

#include "src/core/call/metadata_batch.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/xds/grpc/cel/xds_cel.h"
#include "src/core/xds/grpc/cel/xds_cel_request_resolver.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "test/core/xds/cel_expr_builder.h"
#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

class CelInputMatcherTest : public ::testing::Test {
 protected:
  // Returns a matcher for `request.headers[header] == value`.  Each call
  // uses a fresh builder, so identical arguments yield identical protos.
  std::unique_ptr<CelInputMatcher> HeaderEqualsMatcher(
      absl::string_view header, absl::string_view value) {
    CelExprBuilder builder(arena_.ptr());
    auto program = CelProgram::Create(builder.HeaderEquals(header, value),
                                      CelProgram::ResultType::kBool);
    EXPECT_TRUE(program.ok()) << program.status();
    if (!program.ok()) return nullptr;
    return std::make_unique<CelInputMatcher>(std::move(*program));
  }

  // Returns true if predicate matches an RPC with the given header.
  static bool MatchesHeader(const XdsMatcherList::Predicate& predicate,
                            absl::string_view key, absl::string_view value) {
    grpc_metadata_batch metadata;
    metadata.Append(key, Slice::FromCopiedString(value),
                    [](absl::string_view, const Slice&) { FAIL(); });
    return predicate.Match(RpcMatchContext(&metadata));
  }

  upb::Arena arena_;
};

TEST_F(CelInputMatcherTest, SinglePredicate) {
  auto matcher = HeaderEqualsMatcher("x-foo", "bar");
  ASSERT_NE(matcher, nullptr);
  auto predicate = XdsMatcherList::CreateSinglePredicate(
      std::make_unique<HttpAttributesCelMatchInput>(), std::move(matcher));
  ASSERT_NE(predicate, nullptr);
  EXPECT_TRUE(MatchesHeader(*predicate, "x-foo", "bar"));
  EXPECT_FALSE(MatchesHeader(*predicate, "x-foo", "baz"));
  // A missing header makes the expression evaluate to an error, which
  // does not match.
  EXPECT_FALSE(MatchesHeader(*predicate, "x-other", "bar"));
}

TEST_F(CelInputMatcherTest, NoInputDoesNotMatch) {
  auto matcher = HeaderEqualsMatcher("x-foo", "bar");
  ASSERT_NE(matcher, nullptr);
  EXPECT_FALSE(matcher->Match(std::nullopt));
}

TEST_F(CelInputMatcherTest, Equals) {
  auto matcher1 = HeaderEqualsMatcher("x-foo", "bar");
  auto matcher1_copy = HeaderEqualsMatcher("x-foo", "bar");
  auto matcher2 = HeaderEqualsMatcher("x-foo", "baz");
  ASSERT_NE(matcher1, nullptr);
  ASSERT_NE(matcher1_copy, nullptr);
  ASSERT_NE(matcher2, nullptr);
  EXPECT_TRUE(matcher1->Equals(*matcher1_copy));
  EXPECT_FALSE(matcher1->Equals(*matcher2));
}

TEST_F(CelInputMatcherTest, ToString) {
  auto matcher = HeaderEqualsMatcher("x-foo", "bar");
  ASSERT_NE(matcher, nullptr);
  EXPECT_THAT(matcher->ToString(), ::testing::StartsWith("CelMatcher{"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
