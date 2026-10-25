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
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace grpc_core {
namespace testing {
namespace {

using ResultType = CelProgram::ResultType;

// Variable resolver backed by a map of names to values.
class MapResolver : public cel_VariableResolver {
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

class CelProgramTest : public ::testing::Test {
 protected:
  // Builds `request.matches(pattern)`.
  cel_expr_CheckedExpr* NewMatchesExpr(absl::string_view pattern) {
    cel_expr_Expr* root;
    auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
    auto [target, arg] = builder_.SetReceiverCall(root, "matches");
    builder_.SetIdent(target, "request");
    builder_.SetStringConst(arg, pattern);
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

  // Makes expr a receiver-style call target.function(args...), returning
  // the target followed by the args.
  std::vector<cel_expr_Expr*> SetReceiverCall(cel_expr_Expr* expr,
                                              absl::string_view function,
                                              int num_args) {
    cel_expr_Expr_Call* call =
        cel_expr_Expr_mutable_call_expr(expr, arena_.ptr());
    cel_expr_Expr_Call_set_function(call, cel_StringView_FromAbsl(function));
    std::vector<cel_expr_Expr*> exprs = {builder_.NewExpr(
        cel_expr_Expr_Call_mutable_target(call, arena_.ptr()))};
    for (int i = 0; i < num_args; ++i) {
      exprs.push_back(
          builder_.NewExpr(cel_expr_Expr_Call_add_args(call, arena_.ptr())));
    }
    return exprs;
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

  struct MapEntry {
    absl::string_view key;
    absl::string_view value;
  };

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

  // Makes expr `timestamp(value)`.
  void SetTimestamp(cel_expr_Expr* expr, absl::string_view value) {
    builder_.SetStringConst(builder_.SetCall(expr, "timestamp", 1)[0], value);
  }

  std::unique_ptr<CelProgram> Compile(const cel_expr_CheckedExpr* checked,
                                      ResultType result_type) {
    auto program = CelProgram::Create(checked, result_type);
    EXPECT_TRUE(program.ok()) << program.status();
    return program.ok() ? std::move(*program) : nullptr;
  }

  void ExpectInvalid(const cel_expr_CheckedExpr* checked,
                     ResultType result_type, absl::string_view message) {
    auto program = CelProgram::Create(checked, result_type);
    EXPECT_EQ(program.status(), absl::InvalidArgumentError(message));
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
  // Wrong result type.
  EXPECT_EQ(program->EvaluateString(resolver_), std::nullopt);
}

TEST_F(CelProgramTest, StringConstant) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
  builder_.SetStringConst(root, "foo");
  auto program = Compile(checked, ResultType::kString);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->EvaluateString(resolver_), "foo");
  // Wrong result type.
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
  auto [target, arg] = builder_.SetReceiverCall(root, "startsWith");
  builder_.SetIdent(target, "request");
  builder_.SetStringConst(arg, "ba");
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

TEST_F(CelProgramTest, Equality) {
  cel_expr_Expr* root1;
  auto* checked1 = builder_.NewCheckedExpr(&root1, ResultType::kString);
  builder_.SetStringConst(root1, "foo");
  cel_expr_Expr* root2;
  auto* checked2 = builder_.NewCheckedExpr(&root2, ResultType::kString);
  builder_.SetStringConst(root2, "bar");
  auto program1 = Compile(checked1, ResultType::kString);
  auto program1_copy = Compile(checked1, ResultType::kString);
  auto program2 = Compile(checked2, ResultType::kString);
  ASSERT_NE(program1, nullptr);
  ASSERT_NE(program1_copy, nullptr);
  ASSERT_NE(program2, nullptr);
  EXPECT_EQ(*program1, *program1_copy);
  EXPECT_NE(*program1, *program2);
}

//
// Validation
//

TEST_F(CelProgramTest, NullExpression) {
  ExpectInvalid(nullptr, ResultType::kBool, "CEL expression not set");
}

TEST_F(CelProgramTest, EmptyExpression) {
  ExpectInvalid(cel_expr_CheckedExpr_new(arena_.ptr()), ResultType::kBool,
                "CEL expression not set");
}

TEST_F(CelProgramTest, MissingResultType) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root);
  builder_.SetBoolConst(root, true);
  ExpectInvalid(checked, ResultType::kBool,
                "CEL expression has no type for its result");
}

TEST_F(CelProgramTest, WrongResultType) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
  builder_.SetStringConst(root, "foo");
  ExpectInvalid(checked, ResultType::kBool,
                "CEL expression must evaluate to bool");
  cel_expr_Expr* root2;
  auto* checked2 = builder_.NewCheckedExpr(&root2, ResultType::kBool);
  builder_.SetBoolConst(root2, true);
  ExpectInvalid(checked2, ResultType::kString,
                "CEL expression must evaluate to string");
}

TEST_F(CelProgramTest, UnsupportedVariable) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  auto args = builder_.SetCall(root, "_==_", 2);
  builder_.SetIdent(args[0], "response");
  builder_.SetStringConst(args[1], "foo");
  ExpectInvalid(checked, ResultType::kBool,
                "CEL variable \"response\" is not supported");
}

TEST_F(CelProgramTest, Comprehension) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  cel_expr_Expr_mutable_comprehension_expr(root, arena_.ptr());
  ExpectInvalid(checked, ResultType::kBool,
                "CEL comprehensions are not supported");
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
  ExpectInvalid(checked, ResultType::kBool,
                "CEL comprehensions are not supported");
}

TEST_F(CelProgramTest, StringConversion) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kString);
  builder_.SetIdent(builder_.SetCall(root, "string", 1)[0], "request");
  ExpectInvalid(checked, ResultType::kString,
                "CEL string() conversions are not supported");
}

TEST_F(CelProgramTest, StringConcatenation) {
  ExpectInvalid(NewAddExpr("add_string"), ResultType::kBool,
                "CEL string and list concatenation are not supported");
}

TEST_F(CelProgramTest, ListConcatenation) {
  ExpectInvalid(NewAddExpr("add_list"), ResultType::kBool,
                "CEL string and list concatenation are not supported");
}

TEST_F(CelProgramTest, AdditionWithoutOverload) {
  ExpectInvalid(NewAddExpr(""), ResultType::kBool,
                "CEL expression has no overload for +");
}

TEST_F(CelProgramTest, NonConstantRegex) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  // Global-style matches(request, request).
  auto args = builder_.SetCall(root, "matches", 2);
  builder_.SetIdent(args[0], "request");
  builder_.SetIdent(args[1], "request");
  ExpectInvalid(checked, ResultType::kBool,
                "CEL matches() pattern must be a constant string");
}

TEST_F(CelProgramTest, InvalidRegex) {
  ExpectInvalid(NewMatchesExpr("(unclosed"), ResultType::kBool,
                "CEL matches() pattern is invalid: missing ): (unclosed");
}

TEST_F(CelProgramTest, RegexTooLarge) {
  ExpectInvalid(NewMatchesExpr("a{200}"), ResultType::kBool,
                "CEL matches() pattern program size 204 exceeds the limit of "
                "100");
}

//
// Result type dyn
//

TEST_F(CelProgramTest, DynResultAcceptedForString) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewDynCheckedExpr(&root);
  builder_.SetIdent(root, "request");
  auto program = Compile(checked, ResultType::kString);
  ASSERT_NE(program, nullptr);
  resolver_.SetString("request", "foo");
  EXPECT_EQ(program->EvaluateString(resolver_), "foo");
  // A non-string value at runtime is not a result.
  resolver_.SetBool("request", true);
  EXPECT_EQ(program->EvaluateString(resolver_), std::nullopt);
  // ...so CelStringExtractor falls back to its default.
  CelStringExtractor extractor(std::move(program), "default");
  EXPECT_EQ(extractor.Extract(resolver_), "default");
}

TEST_F(CelProgramTest, DynResultRejectedForBool) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewDynCheckedExpr(&root);
  builder_.SetIdent(root, "request");
  ExpectInvalid(checked, ResultType::kBool,
                "CEL expression must evaluate to bool");
}

//
// Functions
//

TEST_F(CelProgramTest, UnknownFunction) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  builder_.SetIdent(builder_.SetCall(root, "my_custom_func", 1)[0], "request");
  ExpectInvalid(checked, ResultType::kBool,
                "failed to compile CEL expression: cel: function with name "
                "my_custom_func taking 1 args not found");
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

TEST_F(CelProgramTest, SizeOfList) {
  // Global style size(['a', 'b']).
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  SetStringList(builder_.SetCall(call, "size", 1)[0],
                                {"a", "b"});
                },
                2),
            true);
  // Receiver style ['a', 'b', 'c'].size().
  EXPECT_EQ(EvaluateIntEquals(
                [&](cel_expr_Expr* call) {
                  SetStringList(SetReceiverCall(call, "size", 0)[0],
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
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_<_", 2);
              SetTimestamp(args[0], "2025-01-01T00:00:00Z");
              SetTimestamp(args[1], "2026-01-01T00:00:00Z");
            }),
            true);
}

TEST_F(CelProgramTest, InvalidTimestampFailsEvaluation) {
  EXPECT_EQ(EvaluateBoolExpr([&](cel_expr_Expr* root) {
              auto args = builder_.SetCall(root, "_<_", 2);
              SetTimestamp(args[0], "not a timestamp");
              SetTimestamp(args[1], "2026-01-01T00:00:00Z");
            }),
            std::nullopt);
}

TEST_F(CelProgramTest, TimestampGetters) {
  auto getter = [&](absl::string_view function,
                    std::optional<absl::string_view> zone) {
    return [this, function, zone](cel_expr_Expr* call) {
      auto exprs = SetReceiverCall(call, function, zone.has_value() ? 1 : 0);
      SetTimestamp(exprs[0], "2025-12-01T10:20:30.456Z");
      if (zone.has_value()) builder_.SetStringConst(exprs[1], *zone);
    };
  };
  EXPECT_EQ(EvaluateIntEquals(getter("getHours", std::nullopt), 10), true);
  EXPECT_EQ(EvaluateIntEquals(getter("getMinutes", std::nullopt), 20), true);
  EXPECT_EQ(EvaluateIntEquals(getter("getSeconds", std::nullopt), 30), true);
  EXPECT_EQ(EvaluateIntEquals(getter("getMilliseconds", std::nullopt), 456),
            true);
  // UTC offset form.
  EXPECT_EQ(EvaluateIntEquals(getter("getHours", "+05:30"), 15), true);
  EXPECT_EQ(EvaluateIntEquals(getter("getMinutes", "+05:30"), 50), true);
  EXPECT_EQ(EvaluateIntEquals(getter("getHours", "-05:30"), 4), true);
  // IANA time zone form.  "UTC" is always available.
  EXPECT_EQ(EvaluateIntEquals(getter("getHours", "UTC"), 10), true);
  // Other zones need a zoneinfo database, which some platforms (e.g.
  // Windows) do not have.
  absl::TimeZone kolkata;
  if (absl::LoadTimeZone("Asia/Kolkata", &kolkata)) {
    EXPECT_EQ(EvaluateIntEquals(getter("getHours", "Asia/Kolkata"), 15), true);
  }
  // A zone that is neither an IANA name nor an offset is an evaluation
  // error.
  EXPECT_EQ(EvaluateIntEquals(getter("getHours", "Not/AZone"), 10),
            std::nullopt);
}

TEST_F(CelProgramTest, DurationGetters) {
  auto getter = [&](absl::string_view function) {
    return [this, function](cel_expr_Expr* call) {
      builder_.SetStringConst(
          builder_.SetCall(SetReceiverCall(call, function, 0)[0], "duration",
                           1)[0],
          "3h25m");
    };
  };
  EXPECT_EQ(EvaluateIntEquals(getter("getHours"), 3), true);
  // Duration getters return the total, not the component.
  EXPECT_EQ(EvaluateIntEquals(getter("getMinutes"), 205), true);
}

// The date getters are not implemented by cel-c, so expressions using them
// are rejected rather than failing at runtime.
TEST_F(CelProgramTest, UnsupportedTimestampGetter) {
  cel_expr_Expr* root;
  auto* checked = builder_.NewCheckedExpr(&root, ResultType::kBool);
  auto eq_args = builder_.SetCall(root, "_==_", 2);
  SetTimestamp(SetReceiverCall(eq_args[0], "getFullYear", 0)[0],
               "2025-12-01T10:20:30Z");
  builder_.SetIntConst(eq_args[1], 2025);
  ExpectInvalid(checked, ResultType::kBool,
                "failed to compile CEL expression: cel: function with name "
                "getFullYear taking 1 args not found");
}

//
// List and map equality
//

TEST_F(CelProgramTest, ListEquality) {
  auto list_op = [&](absl::string_view op, std::vector<absl::string_view> lhs,
                     std::vector<absl::string_view> rhs) {
    return EvaluateBoolExpr([&](cel_expr_Expr* root) {
      auto args = builder_.SetCall(root, op, 2);
      SetStringList(args[0], lhs);
      SetStringList(args[1], rhs);
    });
  };
  EXPECT_EQ(list_op("_==_", {"a", "x"}, {"a", "x"}), true);
  EXPECT_EQ(list_op("_==_", {"a", "x"}, {"b", "x"}), false);
  EXPECT_EQ(list_op("_==_", {"a", "x"}, {"a", "y"}), false);
  EXPECT_EQ(list_op("_==_", {"a"}, {"a", "x"}), false);
  EXPECT_EQ(list_op("_!=_", {"a", "x"}, {"b", "x"}), true);
  EXPECT_EQ(list_op("_!=_", {"a", "x"}, {"a", "x"}), false);
}

TEST_F(CelProgramTest, MapEquality) {
  auto map_op = [&](absl::string_view op, const std::vector<MapEntry>& lhs,
                    const std::vector<MapEntry>& rhs) {
    return EvaluateBoolExpr([&](cel_expr_Expr* root) {
      auto args = builder_.SetCall(root, op, 2);
      SetStringMap(args[0], lhs);
      SetStringMap(args[1], rhs);
    });
  };
  EXPECT_EQ(map_op("_==_", {{"k", "v"}}, {{"k", "v"}}), true);
  EXPECT_EQ(map_op("_==_", {{"k", "v"}}, {{"k", "w"}}), false);
  EXPECT_EQ(map_op("_!=_", {{"k", "v"}}, {{"k", "w"}}), true);
  // Multiple entries: only the first, or only the last, value differs.
  EXPECT_EQ(map_op("_==_", {{"a", "1"}, {"b", "2"}}, {{"a", "9"}, {"b", "2"}}),
            false);
  EXPECT_EQ(map_op("_==_", {{"a", "1"}, {"b", "2"}}, {{"a", "1"}, {"b", "9"}}),
            false);
  EXPECT_EQ(map_op("_!=_", {{"a", "1"}, {"b", "2"}}, {{"a", "9"}, {"b", "2"}}),
            true);
  // A key differs, or the sizes differ.
  EXPECT_EQ(map_op("_==_", {{"a", "1"}, {"b", "2"}}, {{"a", "1"}, {"c", "2"}}),
            false);
  EXPECT_EQ(map_op("_==_", {{"a", "1"}}, {{"a", "1"}, {"b", "2"}}), false);
  // Entry order does not matter.
  EXPECT_EQ(map_op("_==_", {{"a", "1"}, {"b", "2"}}, {{"b", "2"}, {"a", "1"}}),
            true);
  EXPECT_EQ(map_op("_!=_", {{"a", "1"}, {"b", "2"}}, {{"b", "2"}, {"a", "1"}}),
            false);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
