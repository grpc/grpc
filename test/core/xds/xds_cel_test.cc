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

#include "src/core/xds/grpc/xds_cel.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cel-c/activation.h"
#include "cel-c/string_view_absl.h"
#include "cel-c/value.h"
#include "cel/expr/checked.upb.h"
#include "cel/expr/syntax.upb.h"
#include "test/core/xds/cel_expr_builder.h"
#include "upb/mem/arena.hpp"
#include "gtest/gtest.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

using ResultType = CelProgram::ResultType;

//
// MapResolver
//

// Variable resolver backed by a map of names to values.
class MapResolver final : public cel_VariableResolver {
 public:
  MapResolver() { vtable = &kVTable; }

  void SetBool(absl::string_view name, bool value) {
    cel_Value v;
    cel_Value_SetBool(&v, value);
    bindings_[std::string(name)] = v;
  }

  void SetString(absl::string_view name, absl::string_view value) {
    strings_.push_back(std::make_unique<std::string>(value));
    cel_Value v;
    cel_Value_SetString(&v, cel_StringView_FromAbsl(*strings_.back()));
    bindings_[std::string(name)] = v;
  }

 private:
  static cel_Trilean Find(const cel_VariableResolver* resolver,
                          cel_StringView name, cel_Value* value,
                          cel_Arena* /*arena*/, cel_Status* /*status*/) {
    const auto* self = static_cast<const MapResolver*>(resolver);
    auto it = self->bindings_.find(std::string(cel_StringView_ToAbsl(name)));
    if (it == self->bindings_.end()) return cel_Trilean_kFalse;
    *value = it->second;
    return cel_Trilean_kTrue;
  }

  static constexpr cel_VariableResolverVTable kVTable = {&Find};

  std::map<std::string, cel_Value> bindings_;
  std::vector<std::unique_ptr<std::string>> strings_;
};

//
// CelProgramTest fixture
//

class CelProgramTest : public ::testing::Test {
 protected:
  struct MapEntry {
    absl::string_view key;
    absl::string_view value;
  };

  // Builds `request.matches(pattern)`.
  cel_expr_CheckedExpr* NewMatchesExpr(absl::string_view pattern) {
    cel_expr_Expr* root;
    auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
    auto exprs = builder_.SetReceiverCall(root, "matches", 1);
    builder_.SetIdent(exprs[0], "request");
    builder_.SetStringConst(exprs[1], pattern);
    return checked;
  }

  // Builds `1 + 2 == 3`, recording overload_id for the +, or no overload if
  // empty.
  cel_expr_CheckedExpr* NewAddExpr(absl::string_view overload_id) {
    cel_expr_Expr* root;
    auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
    auto eq_args = builder_.SetCall(root, "_==_", 2);
    auto add_args = builder_.SetCall(eq_args[0], "_+_", 2);
    builder_.SetIntConst(add_args[0], 1);
    builder_.SetIntConst(add_args[1], 2);
    builder_.SetIntConst(eq_args[1], 3);
    if (!overload_id.empty()) {
      builder_.SetOverload(checked, eq_args[0], overload_id);
    }
    return checked;
  }

  // Builds the string expression `request`, typed dyn.
  cel_expr_CheckedExpr* NewDynRequestExpr() {
    cel_expr_Expr* root;
    auto* checked = builder_.NewDynCheckedExpr(&root);
    builder_.SetIdent(root, "request");
    return checked;
  }

  // Makes expr a list literal of string constants.
  void SetStringList(cel_expr_Expr* expr,
                     const std::vector<absl::string_view>& elements) {
    cel_expr_Expr_CreateList* list =
        cel_expr_Expr_mutable_list_expr(expr, arena_.ptr());
    for (absl::string_view element : elements) {
      builder_.SetStringConst(
          builder_.NewExpr(
              cel_expr_Expr_CreateList_add_elements(list, arena_.ptr())),
          element);
    }
  }

  // Makes expr a map literal {key: value, ...} of string constants.
  void SetStringMap(cel_expr_Expr* expr, const std::vector<MapEntry>& entries) {
    cel_expr_Expr_CreateStruct* map =
        cel_expr_Expr_mutable_struct_expr(expr, arena_.ptr());
    for (const MapEntry& map_entry : entries) {
      cel_expr_Expr_CreateStruct_Entry* entry =
          cel_expr_Expr_CreateStruct_add_entries(map, arena_.ptr());
      cel_expr_Expr_CreateStruct_Entry_set_id(entry, next_entry_id_++);
      builder_.SetStringConst(
          builder_.NewExpr(cel_expr_Expr_CreateStruct_Entry_mutable_map_key(
              entry, arena_.ptr())),
          map_entry.key);
      builder_.SetStringConst(
          builder_.NewExpr(cel_expr_Expr_CreateStruct_Entry_mutable_value(
              entry, arena_.ptr())),
          map_entry.value);
    }
  }

  // Compiles and evaluates a bool expression built by build(root).
  std::optional<bool> EvaluateBoolExpr(
      absl::FunctionRef<void(cel_expr_Expr*)> build) {
    cel_expr_Expr* root;
    auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
    build(root);
    auto program = Compile(checked, ResultType::kBool);
    if (program == nullptr) return std::nullopt;
    return program->EvaluateBool(resolver_);
  }

  // Builds `<call> == expected`, where <call> is built by build_call().
  std::optional<bool> EvaluateIntEquals(
      absl::FunctionRef<void(cel_expr_Expr*)> build_call, int64_t expected) {
    return EvaluateBoolExpr([&](cel_expr_Expr* root) {
      auto args = builder_.SetCall(root, "_==_", 2);
      build_call(args[0]);
      builder_.SetIntConst(args[1], expected);
    });
  }

  std::unique_ptr<CelProgram> Compile(const cel_expr_CheckedExpr* checked,
                                      ResultType result_type) {
    auto program = CelProgram::Create(checked, result_type);
    EXPECT_TRUE(program.ok()) << program.status();
    return program.ok() ? std::move(*program) : nullptr;
  }

  upb::Arena arena_;
  CelExprBuilder builder_{arena_.ptr()};
  MapResolver resolver_;
  // Map entry ids, kept well clear of the expr ids assigned by builder_.
  int64_t next_entry_id_ = 1000000;
};

//
// Compilation and evaluation
//

TEST_F(CelProgramTest, BoolConstant) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetBoolConst(root, true);
  auto program = Compile(checked, ResultType::kBool);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->EvaluateBool(resolver_), true);
}

TEST_F(CelProgramTest, EvaluateStringOnBoolResultReturnsNullopt) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetBoolConst(root, true);
  auto program = Compile(checked, ResultType::kBool);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->EvaluateString(resolver_), std::nullopt);
}

TEST_F(CelProgramTest, StringConstant) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
  builder_.SetStringConst(root, "foo");
  auto program = Compile(checked, ResultType::kString);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->EvaluateString(resolver_), "foo");
}

TEST_F(CelProgramTest, EvaluateBoolOnStringResultReturnsNullopt) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
  builder_.SetStringConst(root, "foo");
  auto program = Compile(checked, ResultType::kString);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->EvaluateBool(resolver_), std::nullopt);
}

TEST_F(CelProgramTest, IdentResolvedFromResolver) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetIdent(root, "request");
  auto program = Compile(checked, ResultType::kBool);
  ASSERT_NE(program, nullptr);
  resolver_.SetBool("request", true);
  EXPECT_EQ(program->EvaluateBool(resolver_), true);
  resolver_.SetBool("request", false);
  EXPECT_EQ(program->EvaluateBool(resolver_), false);
}

TEST_F(CelProgramTest, UnresolvedIdentFailsEvaluation) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetIdent(root, "request");
  auto program = Compile(checked, ResultType::kBool);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->EvaluateBool(resolver_), std::nullopt);
}

TEST_F(CelProgramTest, StringEquality) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  auto args = builder_.SetCall(root, "_==_", 2);
  builder_.SetIdent(args[0], "request");
  builder_.SetStringConst(args[1], "bar");
  auto program = Compile(checked, ResultType::kBool);
  ASSERT_NE(program, nullptr);
  resolver_.SetString("request", "bar");
  EXPECT_EQ(program->EvaluateBool(resolver_), true);
  resolver_.SetString("request", "baz");
  EXPECT_EQ(program->EvaluateBool(resolver_), false);
}

TEST_F(CelProgramTest, StartsWith) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  auto exprs = builder_.SetReceiverCall(root, "startsWith", 1);
  builder_.SetIdent(exprs[0], "request");
  builder_.SetStringConst(exprs[1], "ba");
  auto program = Compile(checked, ResultType::kBool);
  ASSERT_NE(program, nullptr);
  resolver_.SetString("request", "bar");
  EXPECT_EQ(program->EvaluateBool(resolver_), true);
  resolver_.SetString("request", "foo");
  EXPECT_EQ(program->EvaluateBool(resolver_), false);
}

TEST_F(CelProgramTest, Matches) {
  auto program = Compile(NewMatchesExpr("^ba+r$"), ResultType::kBool);
  ASSERT_NE(program, nullptr);
  resolver_.SetString("request", "baaar");
  EXPECT_EQ(program->EvaluateBool(resolver_), true);
  resolver_.SetString("request", "foo");
  EXPECT_EQ(program->EvaluateBool(resolver_), false);
}

TEST_F(CelProgramTest, IntAddition) {
  auto program = Compile(NewAddExpr("add_int64"), ResultType::kBool);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->EvaluateBool(resolver_), true);
}

TEST_F(CelProgramTest, SameExpressionComparesEqual) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
  builder_.SetStringConst(root, "foo");
  auto program = Compile(checked, ResultType::kString);
  auto program_copy = Compile(checked, ResultType::kString);
  ASSERT_NE(program, nullptr);
  ASSERT_NE(program_copy, nullptr);
  EXPECT_EQ(*program, *program_copy);
}

TEST_F(CelProgramTest, DifferentExpressionsCompareNotEqual) {
  cel_expr_Expr* root1;
  auto* checked1 = builder_.NewCheckedExpr(&root1, ResultType::kString);
  builder_.SetStringConst(root1, "foo");
  cel_expr_Expr* root2;
  auto* checked2 = builder_.NewCheckedExpr(&root2, ResultType::kString);
  builder_.SetStringConst(root2, "bar");
  auto program1 = Compile(checked1, ResultType::kString);
  auto program2 = Compile(checked2, ResultType::kString);
  ASSERT_NE(program1, nullptr);
  ASSERT_NE(program2, nullptr);
  EXPECT_NE(*program1, *program2);
}

TEST_F(CelProgramTest, ToString) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetBoolConst(root, true);
  auto program = Compile(checked, ResultType::kBool);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->ToString(), "CelProgram{checked_expr_size=16}");
}

//
// Validation
//

TEST_F(CelProgramTest, NullExpression) {
  EXPECT_EQ(CelProgram::Create(nullptr, ResultType::kBool).status(),
            absl::InternalError("CEL expression not set"));
}

TEST_F(CelProgramTest, EmptyExpression) {
  EXPECT_EQ(CelProgram::Create(cel_expr_CheckedExpr_new(arena_.ptr()),
                               ResultType::kBool)
                .status(),
            absl::InternalError("CEL expression not set"));
}

TEST_F(CelProgramTest, MissingResultType) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root);
  builder_.SetBoolConst(root, true);
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("CEL expression has no type for its result"));
}

TEST_F(CelProgramTest, StringResultRejectedForBool) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
  builder_.SetStringConst(root, "foo");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("CEL expression must evaluate to bool"));
}

TEST_F(CelProgramTest, BoolResultRejectedForString) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetBoolConst(root, true);
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kString).status(),
            absl::InternalError("CEL expression must evaluate to string"));
}

TEST_F(CelProgramTest, UnsupportedVariable) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  auto args = builder_.SetCall(root, "_==_", 2);
  builder_.SetIdent(args[0], "response");
  builder_.SetStringConst(args[1], "foo");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("CEL variable \"response\" is not supported"));
}

TEST_F(CelProgramTest, Comprehension) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  cel_expr_Expr_mutable_comprehension_expr(root, arena_.ptr());
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("CEL comprehensions are not supported"));
}

TEST_F(CelProgramTest, NestedComprehension) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  auto args = builder_.SetCall(root, "_&&_", 2);
  builder_.SetBoolConst(args[0], true);
  cel_expr_Expr* list = cel_expr_Expr_CreateList_add_elements(
      cel_expr_Expr_mutable_list_expr(args[1], arena_.ptr()), arena_.ptr());
  cel_expr_Expr_mutable_comprehension_expr(builder_.NewExpr(list),
                                           arena_.ptr());
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("CEL comprehensions are not supported"));
}

TEST_F(CelProgramTest, StringConversion) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
  builder_.SetIdent(builder_.SetCall(root, "string", 1)[0], "request");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kString).status(),
            absl::InternalError("CEL string() conversions are not supported"));
}

TEST_F(CelProgramTest, StringConcatenation) {
  EXPECT_EQ(
      CelProgram::Create(NewAddExpr("add_string"), ResultType::kBool).status(),
      absl::InternalError(
          "CEL string and list concatenation are not supported"));
}

TEST_F(CelProgramTest, ListConcatenation) {
  EXPECT_EQ(
      CelProgram::Create(NewAddExpr("add_list"), ResultType::kBool).status(),
      absl::InternalError(
          "CEL string and list concatenation are not supported"));
}

TEST_F(CelProgramTest, AdditionWithoutOverload) {
  EXPECT_EQ(CelProgram::Create(NewAddExpr(""), ResultType::kBool).status(),
            absl::InternalError("CEL expression has no overload for +"));
}

TEST_F(CelProgramTest, NonConstantRegex) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  // Global-style matches(request, request).
  auto args = builder_.SetCall(root, "matches", 2);
  builder_.SetIdent(args[0], "request");
  builder_.SetIdent(args[1], "request");
  EXPECT_EQ(
      CelProgram::Create(checked, ResultType::kBool).status(),
      absl::InternalError("CEL matches() pattern must be a constant string"));
}

TEST_F(CelProgramTest, InvalidRegex) {
  EXPECT_EQ(CelProgram::Create(NewMatchesExpr("(unclosed"), ResultType::kBool)
                .status(),
            absl::InternalError(
                "CEL matches() pattern is invalid: missing ): (unclosed"));
}

TEST_F(CelProgramTest, RegexAtProgramSizeLimit) {
  EXPECT_NE(Compile(NewMatchesExpr("a{96}"), ResultType::kBool), nullptr);
}

TEST_F(CelProgramTest, RegexTooLarge) {
  EXPECT_EQ(
      CelProgram::Create(NewMatchesExpr("a{97}"), ResultType::kBool).status(),
      absl::InternalError(
          "CEL matches() pattern program size 101 exceeds the limit of "
          "100"));
}

//
// Result type dyn
//

TEST_F(CelProgramTest, DynResultAcceptedForString) {
  auto program = Compile(NewDynRequestExpr(), ResultType::kString);
  ASSERT_NE(program, nullptr);
  resolver_.SetString("request", "foo");
  EXPECT_EQ(program->EvaluateString(resolver_), "foo");
}

TEST_F(CelProgramTest, DynResultWithNonStringValueReturnsNullopt) {
  auto program = Compile(NewDynRequestExpr(), ResultType::kString);
  ASSERT_NE(program, nullptr);
  resolver_.SetBool("request", true);
  EXPECT_EQ(program->EvaluateString(resolver_), std::nullopt);
}

TEST_F(CelProgramTest, DynResultRejectedForBool) {
  EXPECT_EQ(CelProgram::Create(NewDynRequestExpr(), ResultType::kBool).status(),
            absl::InternalError("CEL expression must evaluate to bool"));
}

//
// CelStringExtractor
//

TEST_F(CelProgramTest, StringExtractorReturnsResult) {
  auto program = Compile(NewDynRequestExpr(), ResultType::kString);
  ASSERT_NE(program, nullptr);
  CelStringExtractor extractor(std::move(program), "default");
  resolver_.SetString("request", "foo");
  EXPECT_EQ(extractor.Extract(resolver_), "foo");
}

TEST_F(CelProgramTest, StringExtractorFallsBackToDefault) {
  auto program = Compile(NewDynRequestExpr(), ResultType::kString);
  ASSERT_NE(program, nullptr);
  CelStringExtractor extractor(std::move(program), "default");
  resolver_.SetBool("request", true);
  EXPECT_EQ(extractor.Extract(resolver_), "default");
}

TEST_F(CelProgramTest, StringExtractorWithoutDefaultReturnsNullopt) {
  auto program = Compile(NewDynRequestExpr(), ResultType::kString);
  ASSERT_NE(program, nullptr);
  CelStringExtractor extractor(std::move(program), std::nullopt);
  resolver_.SetBool("request", true);
  EXPECT_EQ(extractor.Extract(resolver_), std::nullopt);
}

TEST_F(CelProgramTest, StringExtractorsWithSameProgramAndDefaultAreEqual) {
  const auto* checked = NewDynRequestExpr();
  auto program1 = Compile(checked, ResultType::kString);
  auto program2 = Compile(checked, ResultType::kString);
  ASSERT_NE(program1, nullptr);
  ASSERT_NE(program2, nullptr);
  CelStringExtractor extractor1(std::move(program1), "default");
  CelStringExtractor extractor2(std::move(program2), "default");
  EXPECT_EQ(extractor1, extractor2);
}

TEST_F(CelProgramTest, StringExtractorsWithDifferentDefaultsAreNotEqual) {
  const auto* checked = NewDynRequestExpr();
  auto program1 = Compile(checked, ResultType::kString);
  auto program2 = Compile(checked, ResultType::kString);
  ASSERT_NE(program1, nullptr);
  ASSERT_NE(program2, nullptr);
  CelStringExtractor extractor1(std::move(program1), "default");
  CelStringExtractor extractor2(std::move(program2), "other");
  EXPECT_NE(extractor1, extractor2);
}

TEST_F(CelProgramTest, StringExtractorToStringWithDefault) {
  auto program = Compile(NewDynRequestExpr(), ResultType::kString);
  ASSERT_NE(program, nullptr);
  CelStringExtractor extractor(std::move(program), "default");
  EXPECT_EQ(extractor.ToString(),
            "CelStringExtractor{CelProgram{checked_expr_size=23}, "
            "default_value=default}");
}

TEST_F(CelProgramTest, StringExtractorToStringWithoutDefault) {
  auto program = Compile(NewDynRequestExpr(), ResultType::kString);
  ASSERT_NE(program, nullptr);
  CelStringExtractor extractor(std::move(program), std::nullopt);
  EXPECT_EQ(extractor.ToString(),
            "CelStringExtractor{CelProgram{checked_expr_size=23}}");
}

//
// Functions
//

TEST_F(CelProgramTest, UnknownFunction) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetIdent(builder_.SetCall(root, "my_custom_func", 1)[0], "request");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError(
                "failed to compile CEL expression: cel: function with name "
                "my_custom_func taking 1 args not found"));
}

TEST_F(CelProgramTest, Ternary) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
  auto args = builder_.SetCall(root, "_?_:_", 3);
  builder_.SetIdent(args[0], "request");
  builder_.SetStringConst(args[1], "yes");
  builder_.SetStringConst(args[2], "no");
  auto program = Compile(checked, ResultType::kString);
  ASSERT_NE(program, nullptr);
  resolver_.SetBool("request", true);
  EXPECT_EQ(program->EvaluateString(resolver_), "yes");
  resolver_.SetBool("request", false);
  EXPECT_EQ(program->EvaluateString(resolver_), "no");
}

TEST_F(CelProgramTest, SizeOfString) {
  resolver_.SetString("request", "abc");
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  builder_.SetIdent(builder_.SetCall(call, "size", 1)[0],
                                    "request");
                },
                3),
            true);
}

TEST_F(CelProgramTest, SizeOfListGlobalStyle) {
  // size(['a', 'b'])
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  SetStringList(builder_.SetCall(call, "size", 1)[0],
                                {"a", "b"});
                },
                2),
            true);
}

TEST_F(CelProgramTest, SizeOfListReceiverStyle) {
  // ['a', 'b', 'c'].size()
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  SetStringList(builder_.SetReceiverCall(call, "size", 0)[0],
                                {"a", "b", "c"});
                },
                3),
            true);
}

TEST_F(CelProgramTest, SizeOfMap) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  SetStringMap(builder_.SetCall(call, "size", 1)[0],
                               {{"k1", "v"}, {"k2", "v"}});
                },
                2),
            true);
}

//
// Timestamps and durations
//

TEST_F(CelProgramTest, TimestampComparison) {
  EXPECT_EQ(
      EvaluateBoolExpr([&](cel_expr_Expr* root) {
        auto args = builder_.SetCall(root, "_<_", 2);
        builder_.SetStringConst(builder_.SetCall(args[0], "timestamp", 1)[0],
                                "2025-01-01T00:00:00Z");
        builder_.SetStringConst(builder_.SetCall(args[1], "timestamp", 1)[0],
                                "2026-01-01T00:00:00Z");
      }),
      true);
}

TEST_F(CelProgramTest, InvalidTimestampFailsEvaluation) {
  EXPECT_EQ(
      EvaluateBoolExpr([&](cel_expr_Expr* root) {
        auto args = builder_.SetCall(root, "_<_", 2);
        builder_.SetStringConst(builder_.SetCall(args[0], "timestamp", 1)[0],
                                "not a timestamp");
        builder_.SetStringConst(builder_.SetCall(args[1], "timestamp", 1)[0],
                                "2026-01-01T00:00:00Z");
      }),
      std::nullopt);
}

TEST_F(CelProgramTest, TimestampGetHours) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs = builder_.SetReceiverCall(call, "getHours", 0);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "timestamp", 1)[0],
                      "2025-12-01T10:20:30.456Z");
                },
                10),
            true);
}

TEST_F(CelProgramTest, TimestampGetMinutes) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs = builder_.SetReceiverCall(call, "getMinutes", 0);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "timestamp", 1)[0],
                      "2025-12-01T10:20:30.456Z");
                },
                20),
            true);
}

TEST_F(CelProgramTest, TimestampGetSeconds) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs = builder_.SetReceiverCall(call, "getSeconds", 0);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "timestamp", 1)[0],
                      "2025-12-01T10:20:30.456Z");
                },
                30),
            true);
}

TEST_F(CelProgramTest, TimestampGetMilliseconds) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs =
                      builder_.SetReceiverCall(call, "getMilliseconds", 0);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "timestamp", 1)[0],
                      "2025-12-01T10:20:30.456Z");
                },
                456),
            true);
}

TEST_F(CelProgramTest, TimestampGetHoursWithPositiveUtcOffset) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs = builder_.SetReceiverCall(call, "getHours", 1);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "timestamp", 1)[0],
                      "2025-12-01T10:20:30.456Z");
                  builder_.SetStringConst(exprs[1], "+05:30");
                },
                15),
            true);
}

TEST_F(CelProgramTest, TimestampGetMinutesWithPositiveUtcOffset) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs = builder_.SetReceiverCall(call, "getMinutes", 1);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "timestamp", 1)[0],
                      "2025-12-01T10:20:30.456Z");
                  builder_.SetStringConst(exprs[1], "+05:30");
                },
                50),
            true);
}

TEST_F(CelProgramTest, TimestampGetHoursWithNegativeUtcOffset) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs = builder_.SetReceiverCall(call, "getHours", 1);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "timestamp", 1)[0],
                      "2025-12-01T10:20:30.456Z");
                  builder_.SetStringConst(exprs[1], "-05:30");
                },
                4),
            true);
}

TEST_F(CelProgramTest, TimestampGetHoursInUtcTimeZone) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs = builder_.SetReceiverCall(call, "getHours", 1);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "timestamp", 1)[0],
                      "2025-12-01T10:20:30.456Z");
                  builder_.SetStringConst(exprs[1], "UTC");
                },
                10),
            true);
}

TEST_F(CelProgramTest, TimestampGetHoursWithZeroUtcOffset) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs = builder_.SetReceiverCall(call, "getHours", 1);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "timestamp", 1)[0],
                      "2025-12-01T10:20:30.456Z");
                  builder_.SetStringConst(exprs[1], "+00:00");
                },
                10),
            true);
}

TEST_F(CelProgramTest, TimestampGetHoursWithInvalidTimeZoneFailsEvaluation) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs = builder_.SetReceiverCall(call, "getHours", 1);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "timestamp", 1)[0],
                      "2025-12-01T10:20:30.456Z");
                  builder_.SetStringConst(exprs[1], "Not/AZone");
                },
                10),
            std::nullopt);
}

TEST_F(CelProgramTest, DurationGetHours) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs = builder_.SetReceiverCall(call, "getHours", 0);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "duration", 1)[0], "3h25m");
                },
                3),
            true);
}

// Duration getters return the total, not the component.
TEST_F(CelProgramTest, DurationGetMinutesReturnsTotalMinutes) {
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  auto exprs = builder_.SetReceiverCall(call, "getMinutes", 0);
                  builder_.SetStringConst(
                      builder_.SetCall(exprs[0], "duration", 1)[0], "3h25m");
                },
                205),
            true);
}

// The date getters are not implemented by cel-c, so expressions using them
// are rejected rather than failing at runtime.
TEST_F(CelProgramTest, UnsupportedTimestampGetter) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  auto eq_args = builder_.SetCall(root, "_==_", 2);
  builder_.SetStringConst(builder_.SetCall(builder_.SetReceiverCall(
                                               eq_args[0], "getFullYear", 0)[0],
                                           "timestamp", 1)[0],
                          "2025-12-01T10:20:30Z");
  builder_.SetIntConst(eq_args[1], 2025);
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError(
                "failed to compile CEL expression: cel: function with name "
                "getFullYear taking 1 args not found"));
}

//
// List equality
//

TEST_F(CelProgramTest, ListsWithSameElementsAreEqual) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_==_", 2);
              SetStringList(args[0], {"a", "x"});
              SetStringList(args[1], {"a", "x"});
            }),
            true);
}

TEST_F(CelProgramTest, ListsWithDifferentFirstElementAreNotEqual) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_==_", 2);
              SetStringList(args[0], {"a", "x"});
              SetStringList(args[1], {"b", "x"});
            }),
            false);
}

TEST_F(CelProgramTest, ListsWithDifferentLastElementAreNotEqual) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_==_", 2);
              SetStringList(args[0], {"a", "x"});
              SetStringList(args[1], {"a", "y"});
            }),
            false);
}

TEST_F(CelProgramTest, ListsWithDifferentSizesAreNotEqual) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_==_", 2);
              SetStringList(args[0], {"a"});
              SetStringList(args[1], {"a", "x"});
            }),
            false);
}

TEST_F(CelProgramTest, ListInequalityWithDifferentElements) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_!=_", 2);
              SetStringList(args[0], {"a", "x"});
              SetStringList(args[1], {"b", "x"});
            }),
            true);
}

TEST_F(CelProgramTest, ListInequalityWithSameElements) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_!=_", 2);
              SetStringList(args[0], {"a", "x"});
              SetStringList(args[1], {"a", "x"});
            }),
            false);
}

//
// Map equality
//

TEST_F(CelProgramTest, MapsWithSameEntryAreEqual) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_==_", 2);
              SetStringMap(args[0], {{"k", "v"}});
              SetStringMap(args[1], {{"k", "v"}});
            }),
            true);
}

TEST_F(CelProgramTest, MapsWithDifferentValueAreNotEqual) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_==_", 2);
              SetStringMap(args[0], {{"k", "v"}});
              SetStringMap(args[1], {{"k", "w"}});
            }),
            false);
}

TEST_F(CelProgramTest, MapInequalityWithDifferentValue) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_!=_", 2);
              SetStringMap(args[0], {{"k", "v"}});
              SetStringMap(args[1], {{"k", "w"}});
            }),
            true);
}

TEST_F(CelProgramTest, MapsWithDifferentFirstValueAreNotEqual) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_==_", 2);
              SetStringMap(args[0], {{"a", "1"}, {"b", "2"}});
              SetStringMap(args[1], {{"a", "9"}, {"b", "2"}});
            }),
            false);
}

TEST_F(CelProgramTest, MapsWithDifferentLastValueAreNotEqual) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_==_", 2);
              SetStringMap(args[0], {{"a", "1"}, {"b", "2"}});
              SetStringMap(args[1], {{"a", "1"}, {"b", "9"}});
            }),
            false);
}

TEST_F(CelProgramTest, MapInequalityWithDifferentFirstValue) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_!=_", 2);
              SetStringMap(args[0], {{"a", "1"}, {"b", "2"}});
              SetStringMap(args[1], {{"a", "9"}, {"b", "2"}});
            }),
            true);
}

TEST_F(CelProgramTest, MapsWithDifferentKeyAreNotEqual) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_==_", 2);
              SetStringMap(args[0], {{"a", "1"}, {"b", "2"}});
              SetStringMap(args[1], {{"a", "1"}, {"c", "2"}});
            }),
            false);
}

TEST_F(CelProgramTest, MapsWithDifferentSizesAreNotEqual) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_==_", 2);
              SetStringMap(args[0], {{"a", "1"}});
              SetStringMap(args[1], {{"a", "1"}, {"b", "2"}});
            }),
            false);
}

TEST_F(CelProgramTest, MapEqualityIgnoresEntryOrder) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_==_", 2);
              SetStringMap(args[0], {{"a", "1"}, {"b", "2"}});
              SetStringMap(args[1], {{"b", "2"}, {"a", "1"}});
            }),
            true);
}

TEST_F(CelProgramTest, MapInequalityIgnoresEntryOrder) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_!=_", 2);
              SetStringMap(args[0], {{"a", "1"}, {"b", "2"}});
              SetStringMap(args[1], {{"b", "2"}, {"a", "1"}});
            }),
            false);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
