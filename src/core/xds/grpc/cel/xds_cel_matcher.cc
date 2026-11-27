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
#include <variant>

#include "src/core/util/down_cast.h"
#include "src/core/util/grpc_check.h"
#include "src/core/xds/grpc/cel/xds_cel.h"
#include "src/core/xds/grpc/cel/xds_cel_parser.h"
#include "src/core/xds/grpc/cel/xds_cel_request_resolver.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc_builder.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "xds/type/matcher/v3/cel.upb.h"
#include "xds/type/matcher/v3/http_inputs.upb.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kHttpAttributesCelMatchInputType =
    "xds.type.matcher.v3.HttpAttributesCelMatchInput";

class CelCustomMatch final : public XdsCustomMatchFactory::CustomMatch {
 public:
  explicit CelCustomMatch(CelProgram program)
      : matcher_(std::make_unique<CelInputMatcher>(std::move(program))) {}

  std::unique_ptr<XdsMatcherList::Predicate> CreatePredicate(
      const XdsResourceType::DecodeContext& context, const XdsExtension& input,
      const UniqueTypeName& matcher_context,
      ValidationErrors* errors) override {
    if (input.type != kHttpAttributesCelMatchInputType) {
      errors->AddError(absl::StrCat("Unsupported Input type:", input.type));
      return nullptr;
    }
    if (matcher_context != RpcMatchContext::Type()) {
      errors->AddError(
          absl::StrCat("Unsupported context:", RpcMatchContext::Type(),
                       ". Parser supported context:", matcher_context));
      return nullptr;
    }
    const absl::string_view* serialized_value =
        std::get_if<absl::string_view>(&input.value);
    // The message has no fields, but make sure it is well-formed.
    if (serialized_value == nullptr ||
        xds_type_matcher_v3_HttpAttributesCelMatchInput_parse(
            serialized_value->data(), serialized_value->size(),
            context.arena) == nullptr) {
      errors->AddError("Failed to parse HttpAttributesCelMatchInput");
      return nullptr;
    }
    return XdsMatcherList::CreateSinglePredicate(
        std::make_unique<HttpAttributesCelMatchInput>(), std::move(matcher_));
  }

 private:
  std::unique_ptr<CelInputMatcher> matcher_;
};

}  // namespace

//
// HttpAttributesCelMatchInput
//

std::optional<CelRequestResolver> HttpAttributesCelMatchInput::GetValue(
    const XdsMatcher::MatchContext& context) const {
  GRPC_CHECK_EQ(context.type(), RpcMatchContext::Type());
  return CelRequestResolver(DownCast<const RpcMatchContext&>(context));
}

//
// XdsCelMatcherFactory
//

std::unique_ptr<XdsCustomMatchFactory::CustomMatch>
XdsCelMatcherFactory::ParseCustomMatch(
    const XdsResourceType::DecodeContext& context,
    absl::string_view serialized_value, ValidationErrors* errors) const {
  const auto* cel_matcher = xds_type_matcher_v3_CelMatcher_parse(
      serialized_value.data(), serialized_value.size(), context.arena);
  if (cel_matcher == nullptr) {
    errors->AddError("could not parse CelMatcher");
    return nullptr;
  }
  ValidationErrors::ScopedField field(errors, ".expr_match");
  auto program =
      ParseCelExpression(xds_type_matcher_v3_CelMatcher_expr_match(cel_matcher),
                         CelProgram::ResultType::kBool, errors);
  if (!program.has_value()) return nullptr;
  return std::make_unique<CelCustomMatch>(std::move(*program));
}

//
// RegisterXdsCelMatcher
//

void RegisterXdsCelMatcher() {
  static const bool registered = [] {
    GrpcXdsBootstrapBuilder::RegisterXdsCustomMatchFactories(
        [](XdsCustomMatchRegistry& registry) {
          registry.RegisterFactory(std::make_unique<XdsCelMatcherFactory>());
        });
    return true;
  }();
  (void)registered;
}

}  // namespace grpc_core
