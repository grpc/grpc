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

#include "src/core/xds/grpc/xds_cel_request_resolver.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "cel-c/activation.h"
#include "cel-c/alloc.h"
#include "cel-c/arena.h"
#include "cel-c/status.h"
#include "cel-c/string_view_absl.h"
#include "cel-c/value.h"
#include "cel/expr/checked.upb.h"
#include "cel/expr/syntax.upb.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/xds/grpc/xds_cel.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "test/core/xds/cel_expr_builder.h"
#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

using ResultType = CelProgram::ResultType;

class CelRequestResolverTest : public ::testing::Test {
 protected:
  CelRequestResolverTest() {
    metadata_.Set(HttpPathMetadata(),
                  Slice::FromStaticString("/package.Service/Method"));
    metadata_.Set(HttpAuthorityMetadata(),
                  Slice::FromStaticString("server.example.com"));
    Append("x-foo", "bar");
    Append("user-agent", "test-agent");
    Append("x-data-bin", "binary");
  }

  void Append(absl::string_view key, absl::string_view value) {
    metadata_.Append(key, Slice::FromCopiedString(value),
                     [&](absl::string_view error, const Slice&) {
                       FAIL() << "failed to append " << key << ": " << error;
                     });
  }

  std::optional<CelProgram> Compile(const cel_expr_CheckedExpr* checked,
                                    ResultType result_type) {
    auto program = CelProgram::Create(checked, result_type);
    EXPECT_TRUE(program.ok()) << program.status();
    if (!program.ok()) return std::nullopt;
    return std::move(*program);
  }

  // Compiles `request.<field>` and evaluates it as a string.
  std::optional<std::string> EvaluateRequestField(absl::string_view field) {
    cel_expr_Expr* root;
    auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
    builder_.SetRequestField(root, field);
    auto program = Compile(checked, ResultType::kString);
    if (!program.has_value()) return std::nullopt;
    return program->EvaluateString(resolver_);
  }

  // Compiles `has(request.<field>)` and evaluates it.
  std::optional<bool> EvaluateHasRequestField(absl::string_view field) {
    cel_expr_Expr* root;
    auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
    builder_.SetRequestField(root, field, /*test_only=*/true);
    auto program = Compile(checked, ResultType::kBool);
    if (!program.has_value()) return std::nullopt;
    return program->EvaluateBool(resolver_);
  }

  // Compiles `request.headers[key]` and evaluates it as a string.
  std::optional<std::string> EvaluateHeader(absl::string_view key) {
    cel_expr_Expr* root;
    auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
    auto args = builder_.SetCall(root, "_[_]", 2);
    builder_.SetRequestField(args[0], "headers");
    builder_.SetStringConst(args[1], key);
    auto program = Compile(checked, ResultType::kString);
    if (!program.has_value()) return std::nullopt;
    return program->EvaluateString(resolver_);
  }

  // Compiles `key in request.headers` and evaluates it.
  std::optional<bool> EvaluateInHeaders(absl::string_view key) {
    cel_expr_Expr* root;
    auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
    auto args = builder_.SetCall(root, "@in", 2);
    builder_.SetStringConst(args[0], key);
    builder_.SetRequestField(args[1], "headers");
    auto program = Compile(checked, ResultType::kBool);
    if (!program.has_value()) return std::nullopt;
    return program->EvaluateBool(resolver_);
  }

  upb::Arena arena_;
  CelExprBuilder builder_{arena_.ptr()};
  grpc_metadata_batch metadata_;
  RpcMatchContext context_{&metadata_};
  CelRequestResolver resolver_{context_};
};

TEST_F(CelRequestResolverTest, PathAndHost) {
  EXPECT_EQ(EvaluateRequestField("path"), "/package.Service/Method");
  EXPECT_EQ(EvaluateRequestField("url_path"), "/package.Service/Method");
  EXPECT_EQ(EvaluateRequestField("host"), "server.example.com");
}

TEST_F(CelRequestResolverTest, Constants) {
  EXPECT_EQ(EvaluateRequestField("method"), "POST");
  EXPECT_EQ(EvaluateRequestField("query"), "");
}

TEST_F(CelRequestResolverTest, HeaderBackedAttributes) {
  EXPECT_EQ(EvaluateRequestField("useragent"), "test-agent");
  EXPECT_EQ(EvaluateHasRequestField("useragent"), true);
  // Absent headers.
  EXPECT_EQ(EvaluateRequestField("referer"), std::nullopt);
  EXPECT_EQ(EvaluateHasRequestField("referer"), false);
  EXPECT_EQ(EvaluateHasRequestField("id"), false);
  Append("referer", "https://example.com");
  Append("x-request-id", "1234");
  EXPECT_EQ(EvaluateRequestField("referer"), "https://example.com");
  EXPECT_EQ(EvaluateRequestField("id"), "1234");
}

TEST_F(CelRequestResolverTest, UnsupportedAttributesAreAbsent) {
  for (absl::string_view field : {"scheme", "time", "protocol", "unknown"}) {
    EXPECT_EQ(EvaluateHasRequestField(field), false) << field;
    EXPECT_EQ(EvaluateRequestField(field), std::nullopt) << field;
  }
}

TEST_F(CelRequestResolverTest, Headers) {
  EXPECT_EQ(EvaluateHeader("x-foo"), "bar");
  // Lookups are case-insensitive.
  EXPECT_EQ(EvaluateHeader("X-Foo"), "bar");
  EXPECT_EQ(EvaluateHeader("user-agent"), "test-agent");
  EXPECT_EQ(EvaluateHeader("content-type"), "application/grpc");
  // Missing and binary headers.
  EXPECT_EQ(EvaluateHeader("missing"), std::nullopt);
  EXPECT_EQ(EvaluateHeader("x-data-bin"), std::nullopt);
}

TEST_F(CelRequestResolverTest, MultipleHeaderValuesAreJoined) {
  Append("x-foo", "baz");
  EXPECT_EQ(EvaluateHeader("x-foo"), "bar,baz");
}

TEST_F(CelRequestResolverTest, InHeaders) {
  EXPECT_EQ(EvaluateInHeaders("x-foo"), true);
  EXPECT_EQ(EvaluateInHeaders("missing"), false);
  EXPECT_EQ(EvaluateInHeaders("x-data-bin"), false);
}

TEST_F(CelRequestResolverTest, MapEquality) {
  // Map equality goes through the size and iteration paths of the map.
  for (absl::string_view field : {"", "headers"}) {
    cel_expr_Expr* root;
    auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
    auto args = builder_.SetCall(root, "_==_", 2);
    for (cel_expr_Expr* arg : args) {
      if (field.empty()) {
        builder_.SetIdent(arg, "request");
      } else {
        builder_.SetRequestField(arg, field);
      }
    }
    auto program = Compile(checked, ResultType::kBool);
    ASSERT_TRUE(program.has_value());
    EXPECT_EQ(program->EvaluateBool(resolver_), true) << field;
  }
}

TEST_F(CelRequestResolverTest, TeHeaderIsHidden) {
  Append("te", "trailers");
  EXPECT_EQ(EvaluateHeader("te"), std::nullopt);
  EXPECT_EQ(EvaluateInHeaders("te"), false);
}

// size() of the lazily computed maps, via the interpreter.
TEST_F(CelRequestResolverTest, Size) {
  int64_t num_headers = 0;
  context_.ForEachHeaderName([&](absl::string_view) { ++num_headers; });
  // :path, :authority, x-foo, user-agent and content-type.
  EXPECT_EQ(num_headers, 5);
  struct TestCase {
    absl::string_view field;  // Empty for `request` itself.
    bool receiver_style;
    int64_t expected;
  };
  for (const TestCase& test_case : {
           TestCase{"headers", false, num_headers},  // size(request.headers)
           TestCase{"headers", true, num_headers},   // request.headers.size()
           TestCase{"", false, 7},                   // size(request)
       }) {
    cel_expr_Expr* root;
    auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
    auto eq_args = builder_.SetCall(root, "_==_", 2);
    cel_expr_Expr* size_arg;
    if (test_case.receiver_style) {
      size_arg = builder_.NewExpr(cel_expr_Expr_Call_mutable_target(
          cel_expr_Expr_mutable_call_expr(eq_args[0], arena_.ptr()),
          arena_.ptr()));
      cel_expr_Expr_Call_set_function(
          cel_expr_Expr_mutable_call_expr(eq_args[0], arena_.ptr()),
          cel_StringView_FromAbsl("size"));
    } else {
      size_arg = builder_.SetCall(eq_args[0], "size", 1)[0];
    }
    if (test_case.field.empty()) {
      builder_.SetIdent(size_arg, "request");
    } else {
      builder_.SetRequestField(size_arg, test_case.field);
    }
    builder_.SetIntConst(eq_args[1], test_case.expected);
    auto program = Compile(checked, ResultType::kBool);
    ASSERT_TRUE(program.has_value());
    EXPECT_EQ(program->EvaluateBool(resolver_), true)
        << test_case.field << " receiver_style=" << test_case.receiver_style;
  }
}

// Exercises the map vtables directly, independent of the interpreter.
class CelRequestMapTest : public CelRequestResolverTest {
 protected:
  CelRequestMapTest() {
    cel_Status_Construct(&status_);
    cel_arena_ = cel_Arena_New(cel_DefaultAllocator);
    value_context_.alloc = cel_DefaultAllocator;
    value_context_.arena = cel_arena_;
  }

  ~CelRequestMapTest() override {
    EXPECT_TRUE(cel_Status_Ok(&status_));
    cel_Arena_Delete(cel_arena_);
    cel_Status_Destruct(&status_);
  }

  cel_Value ResolveRequest() {
    cel_Value value;
    EXPECT_EQ(
        resolver_.vtable->Find(&resolver_, cel_StringView_FromAbsl("request"),
                               &value, cel_arena_, &status_),
        cel_Trilean_kTrue);
    EXPECT_TRUE(cel_Value_IsMap(&value));
    return value;
  }

  cel_Value ResolveHeaders() {
    cel_Value request = ResolveRequest();
    cel_MapValueKey key;
    cel_MapValueKey_SetString(&key, cel_StringView_FromAbsl("headers"));
    cel_Value headers;
    EXPECT_TRUE(cel_MapValue_Find(cel_Value_GetMap(&request), &value_context_,
                                  &key, &headers, &status_));
    EXPECT_TRUE(cel_Value_IsMap(&headers));
    return headers;
  }

  int64_t Size(const cel_Value& map) {
    cel_Value size;
    EXPECT_TRUE(cel_MapValue_Size(cel_Value_GetMap(&map), &value_context_,
                                  &size, &status_));
    return cel_Value_GetInt(&size);
  }

  // Returns all entries of the map.  Map values are shown as "<map>".
  std::map<std::string, std::string> Entries(const cel_Value& map) {
    std::map<std::string, std::string> entries;
    cel_MapValueIterator* iter = cel_MapValue_NewIterator(
        cel_Value_GetMap(&map), &value_context_, &status_);
    EXPECT_NE(iter, nullptr);
    if (iter == nullptr) return entries;
    cel_MapValueKey key;
    cel_Value value;
    while (cel_MapValueIterator_Next(iter, &value_context_, &key, &value,
                                     &status_)) {
      std::string key_str(
          cel_StringView_ToAbsl(cel_MapValueKey_GetString(&key)));
      entries[key_str] =
          cel_Value_IsMap(&value)
              ? "<map>"
              : std::string(cel_StringView_ToAbsl(cel_Value_GetString(&value)));
    }
    cel_MapValueIterator_Delete(iter);
    return entries;
  }

  cel_Status status_;
  cel_Arena* cel_arena_;
  cel_ValueContext value_context_{};
};

TEST_F(CelRequestMapTest, RequestEntries) {
  cel_Value request = ResolveRequest();
  EXPECT_EQ(Size(request), 7);
  EXPECT_THAT(
      Entries(request),
      ::testing::UnorderedElementsAre(
          ::testing::Pair("path", "/package.Service/Method"),
          ::testing::Pair("url_path", "/package.Service/Method"),
          ::testing::Pair("host", "server.example.com"),
          ::testing::Pair("useragent", "test-agent"),
          ::testing::Pair("method", "POST"), ::testing::Pair("query", ""),
          ::testing::Pair("headers", "<map>")));
}

TEST_F(CelRequestMapTest, HeaderEntries) {
  cel_Value headers = ResolveHeaders();
  std::map<std::string, std::string> entries = Entries(headers);
  EXPECT_EQ(Size(headers), static_cast<int64_t>(entries.size()));
  // Binary headers are excluded; content-type is always present.
  EXPECT_THAT(entries, ::testing::IsSupersetOf({
                           ::testing::Pair("x-foo", "bar"),
                           ::testing::Pair("user-agent", "test-agent"),
                           ::testing::Pair("content-type", "application/grpc"),
                       }));
  EXPECT_EQ(entries.count("x-data-bin"), 0);
}

TEST_F(CelRequestMapTest, OtherVariablesAreUnresolved) {
  cel_Value value;
  EXPECT_EQ(
      resolver_.vtable->Find(&resolver_, cel_StringView_FromAbsl("response"),
                             &value, cel_arena_, &status_),
      cel_Trilean_kFalse);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
