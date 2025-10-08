//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_FILTER_COMPOSITE_COMPOSITE_FILTERS_H
#define GRPC_SRC_CORE_FILTER_COMPOSITE_COMPOSITE_FILTERS_H

#include "src/core/filter/filter_chain.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/down_cast.h"
#include "src/core/xds/grpc/xds_matcher.h"

namespace grpc_core {

class CompositeFilter final : public V3InterceptorToV2Bridge<CompositeFilter> {
 public:
  // A matcher action indicating that no filter chain should be used.
  class SkipFilterAction final : public XdsMatcher::Action {
   public:
    bool Equals(const Action& other) const override { return true; }

    std::string ToString() const override { return "SkipFilter"; }

    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE(
          "envoy.extensions.filters.common.matcher.action.v3.SkipFilter");
    }

    UniqueTypeName type() const override { return Type(); }
  };

  // A matcher action indicating a filter chain to use.
  class ExecuteFilterAction final : public XdsMatcher::Action {
   public:
    // TODO(roth): Once we're done with the promise migration, see if
    // there's a way to construct the v3 filter chain during xDS
    // resource validation and store it here directly, so we don't need
    // a layer of indirection in the composite filter itself.
    struct Filter {
      const XdsHttpFilterImpl* filter_impl;
      RefCountedPtr<const FilterConfig> filter_config;
    };

    ExecuteFilterAction(std::vector<Filter> filter_chain,
                        uint32_t sample_per_million)
        : filter_chain_(std::move(filter_chain)),
          sample_per_million_(sample_per_million) {}

    bool Equals(const Action& other) const override {
      const auto& o = DownCast<const ExecuteFilterAction&>(other);
      if (filter_chain_ == nullptr) return o.filter_chain_ == nullptr;
      if (o.filter_chain_ == nullptr) return false;
      return *filter_chain_ == *o.filter_chain_;
    }

    std::string ToString() const override {
      if (filter_chain_ == nullptr) return "{}";
      return filter_chain_->ToString();
    }

    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE(
          "envoy.extensions.filters.http.composite.v3.ExecuteFilterAction");
    }

    UniqueTypeName type() const override { return Type(); }

    const std::vector<Filter>& filter_chain() const { return filter_chain_; }

    uint32_t sample_per_million() const { return sample_per_million_; }

   private:
    std::vector<Filter> filter_chain_;
    uint32_t sample_per_million_;
  };

  // Top-level filter config.
  struct Config final : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE(
          "envoy.extensions.common.matching.v3.ExtensionWithMatcher");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override {
      const auto& o = DownCast<const Config&>(other);
      if (matcher == nullptr) return other.matcher == nullptr;
      if (other.matcher == nullptr) return false;
      return matcher->Equals(*other.matcher);
    }
    std::string ToString() const override {
      if (matcher == nullptr) return "{}";
      return matcher->ToString();
    }

    std::unique_ptr<XdsMatcher> matcher;
  };

  // Override filter config.
  struct ConfigOverride final : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE(
          "envoy.extensions.common.matching.v3.ExtensionWithMatcherPerRoute");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override {
      const auto& o = DownCast<const ConfigOverride&>(other);
      if (matcher == nullptr) return other.matcher == nullptr;
      if (other.matcher == nullptr) return false;
      return matcher->Equals(*other.matcher);
    }
    std::string ToString() const override {
      if (matcher == nullptr) return "{}";
      return matcher->ToString();
    }

    std::unique_ptr<XdsMatcher> matcher;
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "composite"; }

  static absl::StatusOr<std::unique_ptr<CompositeFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  explicit CompositeFilter(std::unique_ptr<const Config> config);

 private:
  std::unique_ptr<const Config> config_;
  absl::flat_hash_map<const XdsMatcher::Action*,
                      absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>>
      filter_chain_map_;
};

#endif  // GRPC_SRC_CORE_FILTER_COMPOSITE_COMPOSITE_FILTERS_H
