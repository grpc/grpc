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

#include "src/core/xds/grpc/xds_cel_parser.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cel-c/activation.h"
#include "cel-c/string_view_absl.h"
#include "cel-c/value.h"
#include "cel/expr/checked.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_cel.h"
#include "test/core/xds/cel_expr_builder.h"
#include "upb/mem/arena.hpp"
#include "xds/type/v3/cel.upb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

// Variable resolver backed by a map of names to values.
class MapResolver final : public cel_VariableResolver {
 public:
  MapResolver() { vtable = &kVTable; }

  void SetString(absl::string_view name, absl::string_view value) {
    strings_.push_back(std::make_unique<std::string>(value));
    cel_Value v;
    cel_Value_SetString(&v, cel_StringView_FromAbsl(*strings_.back()));
    bindings_[std::string(name)] = v;
  }

  void SetInt(absl::string_view name, int64_t value) {
    cel_Value v;
    cel_Value_SetInt(&v, value);
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

class CelParserTest : public ::testing::Test {
 protected:
  // Returns a CelExpression whose cel_expr_checked is checked.
  xds_type_v3_CelExpression* MakeCelExpression(cel_expr_CheckedExpr* checked) {
    auto* cel_expression = xds_type_v3_CelExpression_new(arena_.ptr());
    xds_type_v3_CelExpression_set_cel_expr_checked(cel_expression, checked);
    return cel_expression;
  }

  // Returns `request == value`, with a bool result type.
  cel_expr_CheckedExpr* RequestEquals(absl::string_view value) {
    cel_expr_Expr* root;
    cel_expr_CheckedExpr* checked =
        builder_.NewCheckedExpr(&root, CelProgram::ResultType::kBool);
    std::vector<cel_expr_Expr*> args = builder_.SetCall(root, "_==_", 2);
    builder_.SetIdent(args[0], "request");
    builder_.SetStringConst(args[1], value);
    return checked;
  }

  // Returns `request`, with a string result type.
  cel_expr_CheckedExpr* RequestString() {
    cel_expr_Expr* root;
    cel_expr_CheckedExpr* checked =
        builder_.NewCheckedExpr(&root, CelProgram::ResultType::kString);
    builder_.SetIdent(root, "request");
    return checked;
  }

  // Returns `request`, with a dyn result type.
  cel_expr_CheckedExpr* RequestDyn() {
    cel_expr_Expr* root;
    cel_expr_CheckedExpr* checked = builder_.NewDynCheckedExpr(&root);
    builder_.SetIdent(root, "request");
    return checked;
  }

  xds_type_v3_CelExtractString* MakeCelExtractString(
      cel_expr_CheckedExpr* checked,
      std::optional<absl::string_view> default_value = std::nullopt) {
    auto* extract = xds_type_v3_CelExtractString_new(arena_.ptr());
    xds_type_v3_CelExtractString_set_expr_extract(extract,
                                                  MakeCelExpression(checked));
    if (default_value.has_value()) {
      auto* value = xds_type_v3_CelExtractString_mutable_default_value(
          extract, arena_.ptr());
      google_protobuf_StringValue_set_value(
          value, StdStringToUpbString(*default_value));
    }
    return extract;
  }

  static std::string ErrorMessage(const ValidationErrors& errors) {
    return std::string(
        errors.status(absl::StatusCode::kInvalidArgument, "").message());
  }

  upb::Arena arena_;
  CelExprBuilder builder_{arena_.ptr()};
};

//
// ParseCelExpression
//

TEST_F(CelParserTest, CelExpression) {
  ValidationErrors errors;
  auto program =
      ParseCelExpression(MakeCelExpression(RequestEquals("bar")),
                         CelProgram::ResultType::kBool, &errors);
  EXPECT_TRUE(errors.ok()) << ErrorMessage(errors);
  EXPECT_TRUE(program.has_value());
}

TEST_F(CelParserTest, CelExpressionNotPresent) {
  ValidationErrors errors;
  EXPECT_EQ(ParseCelExpression(nullptr, CelProgram::ResultType::kBool, &errors),
            std::nullopt);
  EXPECT_EQ(ErrorMessage(errors), ": [field: error:field not present]");
}

TEST_F(CelParserTest, CelExpressionStringNotSupported) {
  auto* cel_expression = MakeCelExpression(RequestEquals("bar"));
  xds_type_v3_CelExpression_set_cel_expr_string(
      cel_expression, StdStringToUpbString(absl::string_view("true")));
  ValidationErrors errors;
  EXPECT_EQ(ParseCelExpression(cel_expression, CelProgram::ResultType::kBool,
                               &errors),
            std::nullopt);
  EXPECT_EQ(ErrorMessage(errors),
            ": [field:cel_expr_string error:not supported]");
}

TEST_F(CelParserTest, CelExpressionCheckedNotPresent) {
  ValidationErrors errors;
  EXPECT_EQ(ParseCelExpression(xds_type_v3_CelExpression_new(arena_.ptr()),
                               CelProgram::ResultType::kBool, &errors),
            std::nullopt);
  EXPECT_EQ(ErrorMessage(errors),
            ": [field:cel_expr_checked error:field not present]");
}

TEST_F(CelParserTest, CelExpressionWrongResultType) {
  ValidationErrors errors;
  EXPECT_EQ(ParseCelExpression(MakeCelExpression(RequestEquals("bar")),
                               CelProgram::ResultType::kString, &errors),
            std::nullopt);
  EXPECT_THAT(ErrorMessage(errors),
              ::testing::StartsWith(": [field:cel_expr_checked error:"));
}

//
// ParseCelExtractString
//

TEST_F(CelParserTest, CelExtractString) {
  ValidationErrors errors;
  auto extractor =
      ParseCelExtractString(MakeCelExtractString(RequestString()), &errors);
  ASSERT_TRUE(errors.ok()) << ErrorMessage(errors);
  ASSERT_TRUE(extractor.has_value());
  MapResolver resolver;
  resolver.SetString("request", "/foo/bar");
  EXPECT_EQ(extractor->Extract(resolver), "/foo/bar");
}

TEST_F(CelParserTest, CelExtractStringNoDefault) {
  ValidationErrors errors;
  auto extractor =
      ParseCelExtractString(MakeCelExtractString(RequestString()), &errors);
  ASSERT_TRUE(errors.ok()) << ErrorMessage(errors);
  ASSERT_TRUE(extractor.has_value());
  // Missing variable makes evaluation fail.
  MapResolver resolver;
  EXPECT_EQ(extractor->Extract(resolver), std::nullopt);
  EXPECT_THAT(extractor->ToString(),
              ::testing::StartsWith("CelStringExtractor{CelProgram{"));
}

TEST_F(CelParserTest, CelExtractStringDefault) {
  ValidationErrors errors;
  auto extractor = ParseCelExtractString(
      MakeCelExtractString(RequestString(), "default"), &errors);
  ASSERT_TRUE(errors.ok()) << ErrorMessage(errors);
  ASSERT_TRUE(extractor.has_value());
  MapResolver resolver;
  EXPECT_EQ(extractor->Extract(resolver), "default");
  resolver.SetString("request", "bar");
  EXPECT_EQ(extractor->Extract(resolver), "bar");
  EXPECT_THAT(extractor->ToString(),
              ::testing::EndsWith(", default_value=default}"));
}

TEST_F(CelParserTest, CelExtractStringEmptyDefault) {
  ValidationErrors errors;
  auto extractor =
      ParseCelExtractString(MakeCelExtractString(RequestString(), ""), &errors);
  ASSERT_TRUE(errors.ok()) << ErrorMessage(errors);
  ASSERT_TRUE(extractor.has_value());
  MapResolver resolver;
  EXPECT_EQ(extractor->Extract(resolver), "");
}

TEST_F(CelParserTest, CelExtractStringNotPresent) {
  ValidationErrors errors;
  EXPECT_EQ(ParseCelExtractString(nullptr, &errors), std::nullopt);
  EXPECT_EQ(ErrorMessage(errors), ": [field: error:field not present]");
}

TEST_F(CelParserTest, CelExtractStringExprNotPresent) {
  ValidationErrors errors;
  EXPECT_EQ(ParseCelExtractString(
                xds_type_v3_CelExtractString_new(arena_.ptr()), &errors),
            std::nullopt);
  EXPECT_EQ(ErrorMessage(errors),
            ": [field:expr_extract error:field not present]");
}

TEST_F(CelParserTest, CelExtractStringBoolResultType) {
  ValidationErrors errors;
  EXPECT_EQ(ParseCelExtractString(MakeCelExtractString(RequestEquals("bar")),
                                  &errors),
            std::nullopt);
  EXPECT_THAT(
      ErrorMessage(errors),
      ::testing::StartsWith(": [field:expr_extract.cel_expr_checked error:"));
}

// A control plane that declares `request` as dyn produces dyn-typed
// expressions.
TEST_F(CelParserTest, CelExtractStringDynResultType) {
  ValidationErrors errors;
  auto extractor = ParseCelExtractString(
      MakeCelExtractString(RequestDyn(), "default"), &errors);
  ASSERT_TRUE(errors.ok()) << ErrorMessage(errors);
  ASSERT_TRUE(extractor.has_value());
  MapResolver resolver;
  // Missing binding: evaluation error, so the default is used.
  EXPECT_EQ(extractor->Extract(resolver), "default");
  resolver.SetString("request", "/foo/bar");
  EXPECT_EQ(extractor->Extract(resolver), "/foo/bar");
}

TEST_F(CelParserTest, CelExtractStringDynNonStringResultUsesDefault) {
  ValidationErrors errors;
  auto extractor = ParseCelExtractString(
      MakeCelExtractString(RequestDyn(), "default"), &errors);
  ASSERT_TRUE(errors.ok()) << ErrorMessage(errors);
  ASSERT_TRUE(extractor.has_value());
  MapResolver resolver;
  resolver.SetInt("request", 42);
  EXPECT_EQ(extractor->Extract(resolver), "default");
}

TEST_F(CelParserTest, CelExpressionDynResultTypeRejectedForBool) {
  ValidationErrors errors;
  EXPECT_EQ(ParseCelExpression(MakeCelExpression(RequestDyn()),
                               CelProgram::ResultType::kBool, &errors),
            std::nullopt);
  EXPECT_THAT(ErrorMessage(errors),
              ::testing::HasSubstr("must evaluate to bool"));
}

//
// CelStringExtractor
//

TEST_F(CelParserTest, CelStringExtractorEquals) {
  auto make = [&](absl::string_view value,
                  std::optional<absl::string_view> default_value) {
    CelExprBuilder builder(arena_.ptr());
    cel_expr_Expr* root;
    cel_expr_CheckedExpr* checked =
        builder.NewCheckedExpr(&root, CelProgram::ResultType::kString);
    builder.SetStringConst(root, value);
    ValidationErrors errors;
    auto extractor = ParseCelExtractString(
        MakeCelExtractString(checked, default_value), &errors);
    EXPECT_TRUE(errors.ok()) << ErrorMessage(errors);
    return extractor;
  };
  auto extractor = make("foo", "a");
  auto extractor_copy = make("foo", "a");
  auto different_expr = make("bar", "a");
  auto different_default = make("foo", "b");
  auto no_default = make("foo", std::nullopt);
  ASSERT_TRUE(extractor.has_value());
  ASSERT_TRUE(extractor_copy.has_value());
  ASSERT_TRUE(different_expr.has_value());
  ASSERT_TRUE(different_default.has_value());
  ASSERT_TRUE(no_default.has_value());
  EXPECT_EQ(*extractor, *extractor_copy);
  EXPECT_NE(*extractor, *different_expr);
  EXPECT_NE(*extractor, *different_default);
  EXPECT_NE(*extractor, *no_default);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
