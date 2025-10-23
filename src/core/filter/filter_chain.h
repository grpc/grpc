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

#ifndef GRPC_SRC_CORE_FILTER_FILTER_CHAIN_H
#define GRPC_SRC_CORE_FILTER_FILTER_CHAIN_H

#include <memory>
#include <utility>
#include <vector>

#include "src/core/call/interception_chain.h"
#include "src/core/filter/filter_args.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"

// This code provides an abstraction that allows the config selector to
// build a filter chain without knowing the details of how things work
// in the client channel code.
// TODO(roth): A lot of these interfaces are designed to abstract away
// the differences between the v1 and v3 stacks, and a lot of that
// complexity can go away when the v3 migration is done.

namespace grpc_core {

namespace filter_chain_detail {

// A helper class to accumulate a list of v1 filters.
class FilterChainBuilderV1 {
 public:
  void AddFilter(const grpc_channel_filter* vtable,
                 RefCountedPtr<const FilterConfig> config) {
    filters_.push_back({vtable, std::move(config)});
  }

  std::vector<FilterAndConfig> TakeFilters() { return std::move(filters_); }

 private:
  std::vector<FilterAndConfig> filters_;
};

}  // namespace filter_chain_detail

// Base class for filter chains.
// TODO(roth): Once the v3 migration is done, this can probably go away in
// favor of just directly using UnstartedCallDestination.
class FilterChain : public RefCounted<FilterChain> {};

// Abstract filter chain builder interface.
class FilterChainBuilder {
 public:
  virtual ~FilterChainBuilder() = default;

  // Add a filter using a convenience template method.
  template <typename FilterType>
  void AddFilter(RefCountedPtr<const FilterConfig> config) {
    // TODO(roth): Once the v3 migration is done, this can directly call
    // InterceptionChainBuilder.
    AddFilter(FilterHandleImpl<FilterType>(), std::move(config));
  }

  // Builds the filter chain.  Resets the builder to an empty state, so
  // that it can be used to build another filter chain.
  virtual absl::StatusOr<RefCountedPtr<FilterChain>> Build() = 0;

 protected:
  // Abstract handle for a filter.
  class FilterHandle {
   public:
    virtual ~FilterHandle() = default;
    virtual void AddToBuilder(
        filter_chain_detail::FilterChainBuilderV1* builder,
        RefCountedPtr<const FilterConfig> config) const = 0;
    virtual void AddToBuilder(
        InterceptionChainBuilder* builder,
        RefCountedPtr<const FilterConfig> config) const = 0;
  };

 private:
  // Concrete handle for a specific filter type.
  template <typename FilterType>
  class FilterHandleImpl : public FilterHandle {
   public:
    void AddToBuilder(filter_chain_detail::FilterChainBuilderV1* builder,
                      RefCountedPtr<const FilterConfig> config) const override {
      builder->AddFilter(&FilterType::kFilterVtable, std::move(config));
    }
    void AddToBuilder(InterceptionChainBuilder* builder,
                      RefCountedPtr<const FilterConfig> config) const override {
      builder->Add<FilterType>(std::move(config));
    }
  };

  // Pure virtual method to be implemented by concrete wrappers.
  virtual void AddFilter(const FilterHandle& filter_handle,
                         RefCountedPtr<const FilterConfig> config) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_FILTER_CHAIN_H
