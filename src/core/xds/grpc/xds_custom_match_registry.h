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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_CUSTOM_MATCH_REGISTRY_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_CUSTOM_MATCH_REGISTRY_H

#include <map>
#include <memory>

#include "src/core/util/unique_type_name.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Factory for one type of SinglePredicate.custom_match extension in
// xds.type.matcher.v3.Matcher (e.g., xds.type.matcher.v3.CelMatcher).
//
// A custom matcher defines the type of value that it consumes, so it is
// also responsible for parsing the predicate's input.  This allows the
// matcher implementation (and its dependencies) to live outside of the
// generic xDS matcher code.
class XdsCustomMatchFactory {
 public:
  // A parsed custom_match config.
  class CustomMatch {
   public:
    virtual ~CustomMatch() = default;

    // Parses the predicate's input and returns a predicate that applies
    // this matcher to it.  Returns null and adds to errors on failure.
    // Called at most once.
    virtual std::unique_ptr<XdsMatcherList::Predicate> CreatePredicate(
        const XdsResourceType::DecodeContext& context,
        const XdsExtension& input, const UniqueTypeName& matcher_context,
        ValidationErrors* errors) = 0;
  };

  virtual ~XdsCustomMatchFactory() = default;

  // The proto message type of the custom_match config.
  virtual absl::string_view type() const = 0;

  // Parses the serialized custom_match config.  Returns null and adds to
  // errors on failure.
  virtual std::unique_ptr<CustomMatch> ParseCustomMatch(
      const XdsResourceType::DecodeContext& context,
      absl::string_view serialized_value, ValidationErrors* errors) const = 0;
};

// Registry of custom_match factories, keyed by config proto type.
class XdsCustomMatchRegistry final {
 public:
  void RegisterFactory(std::unique_ptr<XdsCustomMatchFactory> factory);

  // Parses custom_match using the factory registered for its type.
  // Returns null and adds to errors on failure, including if no factory
  // is registered for the type.
  std::unique_ptr<XdsCustomMatchFactory::CustomMatch> ParseCustomMatch(
      const XdsResourceType::DecodeContext& context,
      const XdsExtension& custom_match, ValidationErrors* errors) const;

 private:
  std::map<absl::string_view /* Owned by factory */,
           std::unique_ptr<XdsCustomMatchFactory>>
      factories_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_CUSTOM_MATCH_REGISTRY_H
