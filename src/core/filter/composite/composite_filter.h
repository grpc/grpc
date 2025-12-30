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

#ifndef GRPC_SRC_CORE_FILTER_COMPOSITE_COMPOSITE_FILTER_H
#define GRPC_SRC_CORE_FILTER_COMPOSITE_COMPOSITE_FILTER_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/call_destination.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

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
    struct Filter {
      const XdsHttpFilterImpl* filter_impl;
      RefCountedPtr<const FilterConfig> filter_config;

      bool operator==(const Filter& other) const {
        if (filter_impl != other.filter_impl) return false;
        if (filter_config == nullptr) return other.filter_config == nullptr;
        if (other.filter_config == nullptr) return false;
        return *filter_config == *other.filter_config;
      }
    };

    ExecuteFilterAction(std::vector<Filter> filter_chain,
                        uint32_t sample_per_million)
        : filter_chain_(std::move(filter_chain)),
          sample_per_million_(sample_per_million) {}

    bool Equals(const Action& other) const override {
      const auto& o = DownCast<const ExecuteFilterAction&>(other);
      return filter_chain_ == o.filter_chain_ &&
             sample_per_million_ == o.sample_per_million_;
    }

    std::string ToString() const override;

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
      return GRPC_UNIQUE_TYPE_NAME_HERE("composite_filter_config");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override {
      const auto& o = DownCast<const Config&>(other);
      if (matcher == nullptr) return o.matcher == nullptr;
      if (o.matcher == nullptr) return false;
      return matcher->Equals(*o.matcher);
    }
    std::string ToString() const override {
      if (matcher == nullptr) return "{}";
      return matcher->ToString();
    }

    std::unique_ptr<XdsMatcher> matcher;
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "composite"; }

  static absl::StatusOr<RefCountedPtr<CompositeFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  CompositeFilter(const ChannelArgs& args, RefCountedPtr<const Config> config,
                  ChannelFilter::Args filter_args);

 private:
  void Orphaned() override {}

  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override;

  RefCountedPtr<const Config> config_;

  // Map from action in the matcher tree to corresponding filter chain.
  //
  // Ideally, we'd prefer to avoid having a separate map here and instead
  // store the filter chain directly in the xDS matcher.  However, the xDS
  // matcher is constructed at xDS resource validation time, and we can't
  // construct the filter chain at that point, because we don't know the call
  // destination to use -- and we can't know it there, because each channel
  // that uses the same xDS resource will have its own call destination.
  absl::flat_hash_map<const XdsMatcher::Action*,
                      absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>>
      filter_chain_map_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_COMPOSITE_COMPOSITE_FILTER_H
