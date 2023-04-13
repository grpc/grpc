//
//
// Copyright 2016 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/channel_init.h"

#include <string.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <type_traits>

#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/surface/channel_stack_type.h"

namespace grpc_core {

const char* (*NameFromChannelFilter)(const grpc_channel_filter*);

namespace {
struct CompareChannelFiltersByName {
  bool operator()(const grpc_channel_filter* a,
                  const grpc_channel_filter* b) const {
    return strcmp(NameFromChannelFilter(a), NameFromChannelFilter(b)) < 0;
  }
};
}  // namespace

ChannelInit::FilterRegistration& ChannelInit::FilterRegistration::After(
    std::initializer_list<const grpc_channel_filter*> filters) {
  for (auto filter : filters) {
    after_.push_back(filter);
  }
  return *this;
}

ChannelInit::FilterRegistration& ChannelInit::FilterRegistration::Before(
    std::initializer_list<const grpc_channel_filter*> filters) {
  for (auto filter : filters) {
    before_.push_back(filter);
  }
  return *this;
}

ChannelInit::FilterRegistration& ChannelInit::FilterRegistration::If(
    InclusionPredicate predicate) {
  predicates_.emplace_back(std::move(predicate));
  return *this;
}

ChannelInit::FilterRegistration& ChannelInit::Builder::RegisterFilter(
    grpc_channel_stack_type type, const grpc_channel_filter* filter) {
  filters_[type].emplace_back(std::make_unique<FilterRegistration>(filter));
  return *filters_[type].back();
}

ChannelInit::StackConfig ChannelInit::BuildFilters(
    const std::vector<std::unique_ptr<ChannelInit::FilterRegistration>>&
        registrations,
    grpc_channel_stack_type type) {
  auto collapse_predicates =
      [](std::vector<InclusionPredicate> predicates) -> InclusionPredicate {
    switch (predicates.size()) {
      case 0:
        return [](const ChannelArgs&) { return true; };
      case 1:
        return std::move(predicates[0]);
      default:
        return [predicates =
                    std::move(predicates)](const ChannelArgs& args) mutable {
          for (auto& predicate : predicates) {
            if (!predicate(args)) return false;
          }
          return true;
        };
    }
  };
  // Phase 1: Build a map from filter to the set of filters that must be
  // initialized before it.
  // We order this map (and the set of dependent filters) by filter name to
  // ensure algorithm ordering stability is deterministic for a given build.
  // We should not require this, but at the time of writing it's expected that
  // this will help overall stability.
  using F = const grpc_channel_filter*;
  std::map<F, FilterRegistration*> filter_to_registration;
  using DependencyMap = std::map<F, std::set<F, CompareChannelFiltersByName>,
                                 CompareChannelFiltersByName>;
  DependencyMap dependencies;
  std::vector<Filter> terminal_filters;
  for (const auto& registration : registrations) {
    if (filter_to_registration.count(registration->filter_) > 0) {
      Crash(absl::StrCat("Duplicate registration of channel filter ",
                         NameFromChannelFilter(registration->filter_)));
    }
    filter_to_registration[registration->filter_] = registration.get();
    if (registration->terminal_) {
      GPR_ASSERT(registration->after_.empty());
      GPR_ASSERT(registration->before_.empty());
      GPR_ASSERT(!registration->before_all_);
      terminal_filters.emplace_back(
          registration->filter_,
          collapse_predicates(registration->predicates_));
    } else {
      dependencies[registration->filter_];  // Ensure it's in the map.
    }
  }
  for (const auto& registration : registrations) {
    if (registration->terminal_) continue;
    GPR_ASSERT(filter_to_registration.count(registration->filter_) > 0);
    for (F after : registration->after_) {
      if (filter_to_registration.count(after) == 0) {
        Crash(absl::StrCat(
            "Filter ", NameFromChannelFilter(after),
            " not registered, but is referenced in the after clause of ",
            NameFromChannelFilter(registration->filter_),
            " when building channel stack ",
            grpc_channel_stack_type_string(type)));
      }
      dependencies[registration->filter_].insert(after);
    }
    for (F before : registration->before_) {
      if (filter_to_registration.count(before) == 0) {
        Crash(absl::StrCat(
            "Filter ", NameFromChannelFilter(before),
            " not registered, but is referenced in the after clause of ",
            NameFromChannelFilter(registration->filter_),
            " when building channel stack ",
            grpc_channel_stack_type_string(type)));
      }
      dependencies[before].insert(registration->filter_);
    }
    if (registration->before_all_) {
      for (const auto& other : registrations) {
        if (other.get() == registration.get()) continue;
        dependencies[other->filter_].insert(registration->filter_);
      }
    }
  }
  // Phase 2: Build a list of filters in dependency order.
  // We can simply iterate through and add anything with no dependency.
  // We then remove that filter from the dependency list of all other filters.
  // We repeat until we have no more filters to add.
  auto build_remaining_dependency_graph =
      [](const DependencyMap& dependencies) {
        std::string result;
        for (const auto& p : dependencies) {
          absl::StrAppend(&result, NameFromChannelFilter(p.first), " ->");
          for (const auto& d : p.second) {
            absl::StrAppend(&result, " ", NameFromChannelFilter(d));
          }
          absl::StrAppend(&result, "\n");
        }
        return result;
      };
  auto take_ready_dependency = [&, original = dependencies]() {
    for (auto it = dependencies.begin(); it != dependencies.end(); ++it) {
      if (it->second.empty()) {
        auto r = it->first;
        dependencies.erase(it);
        return r;
      }
    }
    Crash(absl::StrCat(
        "Unresolvable graph of channel filters - remaining graph:\n",
        build_remaining_dependency_graph(dependencies), "original:\n",
        build_remaining_dependency_graph(original)));
  };
  std::vector<Filter> filters;
  while (!dependencies.empty()) {
    auto filter = take_ready_dependency();
    filters.emplace_back(
        filter, collapse_predicates(
                    std::move(filter_to_registration[filter]->predicates_)));
    for (auto& p : dependencies) {
      p.second.erase(filter);
    }
  }
  return StackConfig{std::move(filters), std::move(terminal_filters)};
};

ChannelInit ChannelInit::Builder::Build() {
  ChannelInit result;
  for (int i = 0; i < GRPC_NUM_CHANNEL_STACK_TYPES; i++) {
    result.filters_[i] =
        BuildFilters(filters_[i], static_cast<grpc_channel_stack_type>(i));
  }
  return result;
}

bool ChannelInit::CreateStack(ChannelStackBuilder* builder) const {
  for (const auto& filter : filters_[builder->channel_stack_type()].filters) {
    if (!filter.predicate(builder->channel_args())) continue;
    builder->AppendFilter(filter.filter);
  }
  for (const auto& terminator :
       filters_[builder->channel_stack_type()].terminators) {
    if (!terminator.predicate(builder->channel_args())) continue;
    builder->AppendFilter(terminator.filter);
    return true;
  }
  return false;
}

}  // namespace grpc_core
