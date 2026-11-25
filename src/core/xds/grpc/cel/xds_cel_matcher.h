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

#ifndef GRPC_SRC_CORE_XDS_GRPC_CEL_XDS_CEL_MATCHER_H
#define GRPC_SRC_CORE_XDS_GRPC_CEL_XDS_CEL_MATCHER_H

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/core/util/down_cast.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/cel/xds_cel.h"
#include "src/core/xds/grpc/cel/xds_cel_request_resolver.h"
#include "src/core/xds/grpc/xds_custom_match_registry.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Registers the xDS CEL matcher (xds.type.matcher.v3.CelMatcher, gRFC
// A106) for use in SinglePredicate.custom_match.  Without this, xDS
// resources that use CelMatcher are rejected.  Must be called before the
// XdsClient is created.  Safe to call more than once.
void RegisterXdsCelMatcher();

// Matches by evaluating a CEL expression against the RPC's attributes
// (xds.type.matcher.v3.CelMatcher, gRFC A106).  The match fails if the
// expression evaluates to an error or to anything other than true.
class CelInputMatcher
    : public XdsMatcherList::InputMatcher<CelRequestResolver> {
 public:
  // program must have been created with CelProgram::ResultType::kBool.
  explicit CelInputMatcher(CelProgram program) : program_(std::move(program)) {}

  bool Match(const std::optional<CelRequestResolver>& input) const override {
    if (!input.has_value()) return false;
    return program_.EvaluateBool(*input).value_or(false);
  }
  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("CelInputMatcher");
  }
  UniqueTypeName type() const override { return Type(); }
  bool Equals(const XdsMatcherList::InputMatcher<CelRequestResolver>& other)
      const override {
    if (type() != other.type()) return false;
    return program_ == DownCast<const CelInputMatcher&>(other).program_;
  }
  std::string ToString() const override {
    return absl::StrCat("CelMatcher{", program_.ToString(), "}");
  }

 private:
  CelProgram program_;
};

// Exposes the RPC's attributes to a CEL matcher via the `request` variable
// (xds.type.matcher.v3.HttpAttributesCelMatchInput, gRFC A106).
class HttpAttributesCelMatchInput
    : public XdsMatcher::InputValue<CelRequestResolver> {
 public:
  std::optional<CelRequestResolver> GetValue(
      const XdsMatcher::MatchContext& context) const override;
  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("HttpAttributesCelMatchInput");
  }
  UniqueTypeName type() const override { return Type(); }
  bool Equals(
      const XdsMatcher::InputValue<CelRequestResolver>& other) const override {
    return type() == other.type();
  }
  std::string ToString() const override {
    return "HttpAttributesCelMatchInput";
  }
};

// custom_match factory for xds.type.matcher.v3.CelMatcher.  The only
// supported input is xds.type.matcher.v3.HttpAttributesCelMatchInput.
class XdsCelMatcherFactory final : public XdsCustomMatchFactory {
 public:
  static absl::string_view Type() { return "xds.type.matcher.v3.CelMatcher"; }
  absl::string_view type() const override { return Type(); }

  std::unique_ptr<CustomMatch> ParseCustomMatch(
      const XdsResourceType::DecodeContext& context,
      absl::string_view serialized_value,
      ValidationErrors* errors) const override;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_CEL_XDS_CEL_MATCHER_H
