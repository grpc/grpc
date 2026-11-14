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

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cel-c/activation.h"
#include "cel-c/status.h"
#include "cel-c/string_view_absl.h"
#include "cel-c/value.h"
#include "cel/expr/checked.upb.h"
#include "cel/expr/syntax.upb.h"
#include "test/core/xds/cel_expr_builder.h"
#include "upb/mem/arena.hpp"
#include "gtest/gtest.h"
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

  // Makes resolving name fail.
  void SetError(absl::string_view name) { errors_.insert(std::string(name)); }

 private:
  static cel_Trilean Find(const cel_VariableResolver* resolver,
                          cel_StringView name, cel_Value* value,
                          cel_Arena* /*arena*/, cel_Status* status) {
    const auto* self = static_cast<const MapResolver*>(resolver);
    std::string key(cel_StringView_ToAbsl(name));
    if (self->errors_.count(key) > 0) {
      cel_InternalStatus(status, cel_StringView_FromAbsl("resolver error"));
      return cel_Trilean_kError;
    }
    auto it = self->bindings_.find(key);
    if (it == self->bindings_.end()) return cel_Trilean_kFalse;
    *value = it->second;
    return cel_Trilean_kTrue;
  }

  static constexpr cel_VariableResolverVTable kVTable = {&Find};

  std::map<std::string, cel_Value> bindings_;
  std::vector<std::unique_ptr<std::string>> strings_;
  std::set<std::string> errors_;
};

//
// CelProgramTest fixture
//

class CelProgramTest : public ::testing::Test {
 protected:
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

  // Builds `request in <container>`, returning the container expr.
  cel_expr_CheckedExpr* NewInExpr(cel_expr_Expr** container) {
    cel_expr_Expr* root;
    auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
    auto args = builder_.SetCall(root, "@in", 2);
    builder_.SetIdent(args[0], "request");
    *container = args[1];
    return checked;
  }

  // Makes expr a map literal with a single entry.
  void SetSingleEntryMap(cel_expr_Expr* expr, cel_expr_Expr** key,
                         cel_expr_Expr** value) {
    cel_expr_Expr_CreateStruct_Entry* entry =
        cel_expr_Expr_CreateStruct_add_entries(
            cel_expr_Expr_mutable_struct_expr(expr, arena_.ptr()),
            arena_.ptr());
    *key = builder_.NewExpr(
        cel_expr_Expr_CreateStruct_Entry_mutable_map_key(entry, arena_.ptr()));
    *value = builder_.NewExpr(
        cel_expr_Expr_CreateStruct_Entry_mutable_value(entry, arena_.ptr()));
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
};

//
// Evaluation
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

// The result is a CEL error value.
TEST_F(CelProgramTest, UnresolvedIdentFailsEvaluation) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetIdent(root, "request");
  auto program = Compile(checked, ResultType::kBool);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->EvaluateBool(resolver_), std::nullopt);
}

// Execution itself fails.
TEST_F(CelProgramTest, ResolverErrorFailsEvaluation) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetIdent(root, "request");
  auto program = Compile(checked, ResultType::kBool);
  ASSERT_NE(program, nullptr);
  resolver_.SetError("request");
  EXPECT_EQ(program->EvaluateBool(resolver_), std::nullopt);
}

TEST_F(CelProgramTest, IntAddition) {
  auto program = Compile(NewAddExpr("add_int64"), ResultType::kBool);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->EvaluateBool(resolver_), true);
}

//
// Equality and ToString
//

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

TEST_F(CelProgramTest, ExpressionsDifferingOnlyInSourceInfoCompareEqual) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
  builder_.SetStringConst(root, "foo");
  auto program = Compile(checked, ResultType::kString);
  cel_expr_SourceInfo_positions_set(
      cel_expr_CheckedExpr_mutable_source_info(checked, arena_.ptr()),
      cel_expr_Expr_id(root), /*val=*/5, arena_.ptr());
  auto program_with_source_info = Compile(checked, ResultType::kString);
  ASSERT_NE(program, nullptr);
  ASSERT_NE(program_with_source_info, nullptr);
  EXPECT_EQ(*program, *program_with_source_info);
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
  builder_.SetIdent(root, "response");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("CEL variable \"response\" is not supported"));
}

// The following tests check that each kind of sub-expression is validated.

TEST_F(CelProgramTest, UnsupportedVariableInSelectOperand) {
  // response.foo
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  cel_expr_Expr_Select* select =
      cel_expr_Expr_mutable_select_expr(root, arena_.ptr());
  cel_expr_Expr_Select_set_field(select, cel_StringView_FromAbsl("foo"));
  builder_.SetIdent(builder_.NewExpr(cel_expr_Expr_Select_mutable_operand(
                        select, arena_.ptr())),
                    "response");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("CEL variable \"response\" is not supported"));
}

TEST_F(CelProgramTest, UnsupportedVariableInCallTarget) {
  // response.startsWith('a')
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  auto exprs = builder_.SetReceiverCall(root, "startsWith", 1);
  builder_.SetIdent(exprs[0], "response");
  builder_.SetStringConst(exprs[1], "a");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("CEL variable \"response\" is not supported"));
}

TEST_F(CelProgramTest, UnsupportedVariableInCallArg) {
  // response == 'a'
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  auto args = builder_.SetCall(root, "_==_", 2);
  builder_.SetIdent(args[0], "response");
  builder_.SetStringConst(args[1], "a");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("CEL variable \"response\" is not supported"));
}

TEST_F(CelProgramTest, UnsupportedVariableInListElement) {
  // request in [response]
  cel_expr_Expr* list;
  auto* checked = NewInExpr(&list);
  builder_.SetIdent(
      builder_.NewExpr(cel_expr_Expr_CreateList_add_elements(
          cel_expr_Expr_mutable_list_expr(list, arena_.ptr()), arena_.ptr())),
      "response");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("CEL variable \"response\" is not supported"));
}

TEST_F(CelProgramTest, UnsupportedVariableInMapKey) {
  // request in {response: 'v'}
  cel_expr_Expr* map;
  auto* checked = NewInExpr(&map);
  cel_expr_Expr* key;
  cel_expr_Expr* value;
  SetSingleEntryMap(map, &key, &value);
  builder_.SetIdent(key, "response");
  builder_.SetStringConst(value, "v");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("CEL variable \"response\" is not supported"));
}

TEST_F(CelProgramTest, UnsupportedVariableInMapValue) {
  // request in {'k': response}
  cel_expr_Expr* map;
  auto* checked = NewInExpr(&map);
  cel_expr_Expr* key;
  cel_expr_Expr* value;
  SetSingleEntryMap(map, &key, &value);
  builder_.SetStringConst(key, "k");
  builder_.SetIdent(value, "response");
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
          "CEL string, bytes and list concatenation are not supported"));
}

TEST_F(CelProgramTest, BytesConcatenation) {
  EXPECT_EQ(
      CelProgram::Create(NewAddExpr("add_bytes"), ResultType::kBool).status(),
      absl::InternalError(
          "CEL string, bytes and list concatenation are not supported"));
}

TEST_F(CelProgramTest, ListConcatenation) {
  EXPECT_EQ(
      CelProgram::Create(NewAddExpr("add_list"), ResultType::kBool).status(),
      absl::InternalError(
          "CEL string, bytes and list concatenation are not supported"));
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

// matches() with no pattern isn't validated (so that there is no
// out-of-bounds access), but fails to compile.
TEST_F(CelProgramTest, MatchesWithoutPattern) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetIdent(builder_.SetReceiverCall(root, "matches", 0)[0], "request");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError(
                "failed to compile CEL expression: cel: function with name "
                "matches taking 1 args not found"));
}

TEST_F(CelProgramTest, ExpressionTooDeepToSerialize) {
  // !!...!true, nested deeper than upb's encoding depth limit.
  cel_expr_Expr* expr;
  auto* checked = builder_.NewCheckedExpr(&expr, ResultType::kBool);
  for (int i = 0; i < 100; ++i) expr = builder_.SetCall(expr, "!_", 1)[0];
  builder_.SetBoolConst(expr, true);
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError("failed to serialize CEL expression"));
}

TEST_F(CelProgramTest, InvalidExprIdFailsConversion) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  auto args = builder_.SetCall(root, "_==_", 2);
  builder_.SetIntConst(args[0], 1);
  builder_.SetIntConst(args[1], 1);
  cel_expr_Expr_set_id(args[1], -1);
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError(
                "failed to convert CEL expression: cel: expected "
                "google.api.expr.Expr.id to be greater than or equal to 0: "
                "-1"));
}

TEST_F(CelProgramTest, UnknownFunctionFailsCompilation) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetIdent(builder_.SetCall(root, "my_custom_func", 1)[0], "request");
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError(
                "failed to compile CEL expression: cel: function with name "
                "my_custom_func taking 1 args not found"));
}

// The following tests check that validation skips missing sub-expressions,
// leaving them to be rejected by cel-c.

TEST_F(CelProgramTest, SelectWithoutOperand) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  cel_expr_Expr_Select_set_field(
      cel_expr_Expr_mutable_select_expr(root, arena_.ptr()),
      cel_StringView_FromAbsl("foo"));
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError(
                "failed to compile CEL expression: cel: select expr AST node "
                "is missing operand: id=1"));
}

TEST_F(CelProgramTest, MapEntryWithoutKeyOrValue) {
  cel_expr_Expr* map;
  auto* checked = NewInExpr(&map);
  cel_expr_Expr_CreateStruct_add_entries(
      cel_expr_Expr_mutable_struct_expr(map, arena_.ptr()), arena_.ptr());
  EXPECT_EQ(CelProgram::Create(checked, ResultType::kBool).status(),
            absl::InternalError(
                "failed to convert CEL expression: cel: expected "
                "google.api.expr.Expr.CreateStruct.Entry.key_kind to be "
                "map_key: 0"));
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

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
