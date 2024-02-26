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
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_stack_trace.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/sync.h"
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

ChannelInit::FilterRegistration& ChannelInit::FilterRegistration::IfNot(
    InclusionPredicate predicate) {
  predicates_.emplace_back(
      [predicate = std::move(predicate)](const ChannelArgs& args) {
        return !predicate(args);
      });
  return *this;
}

ChannelInit::FilterRegistration&
ChannelInit::FilterRegistration::IfHasChannelArg(const char* arg) {
  return If([arg](const ChannelArgs& args) { return args.Contains(arg); });
}

ChannelInit::FilterRegistration& ChannelInit::FilterRegistration::IfChannelArg(
    const char* arg, bool default_value) {
  return If([arg, default_value](const ChannelArgs& args) {
    return args.GetBool(arg).value_or(default_value);
  });
}

ChannelInit::FilterRegistration&
ChannelInit::FilterRegistration::ExcludeFromMinimalStack() {
  return If([](const ChannelArgs& args) { return !args.WantMinimalStack(); });
}

ChannelInit::FilterRegistration& ChannelInit::Builder::RegisterFilter(
    grpc_channel_stack_type type, const grpc_channel_filter* filter,
    const ChannelFilterVtable* vtable, SourceLocation registration_source) {
  filters_[type].emplace_back(std::make_unique<FilterRegistration>(
      filter, vtable, registration_source));
  return *filters_[type].back();
}

ChannelInit::StackConfig ChannelInit::BuildStackConfig(
    const std::vector<std::unique_ptr<ChannelInit::FilterRegistration>>&
        registrations,
    PostProcessor* post_processors, grpc_channel_stack_type type) {
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
      const auto first =
          filter_to_registration[registration->filter_]->registration_source_;
      const auto second = registration->registration_source_;
      Crash(absl::StrCat("Duplicate registration of channel filter ",
                         NameFromChannelFilter(registration->filter_),
                         "\nfirst: ", first.file(), ":", first.line(),
                         "\nsecond: ", second.file(), ":", second.line()));
    }
    filter_to_registration[registration->filter_] = registration.get();
    if (registration->terminal_) {
      GPR_ASSERT(registration->after_.empty());
      GPR_ASSERT(registration->before_.empty());
      GPR_ASSERT(!registration->before_all_);
      terminal_filters.emplace_back(registration->filter_, nullptr,
                                    std::move(registration->predicates_),
                                    registration->registration_source_);
    } else {
      dependencies[registration->filter_];  // Ensure it's in the map.
    }
  }
  for (const auto& registration : registrations) {
    if (registration->terminal_) continue;
    GPR_ASSERT(filter_to_registration.count(registration->filter_) > 0);
    for (F after : registration->after_) {
      if (filter_to_registration.count(after) == 0) {
        gpr_log(
            GPR_DEBUG, "%s",
            absl::StrCat(
                "Filter ", NameFromChannelFilter(after),
                " not registered, but is referenced in the after clause of ",
                NameFromChannelFilter(registration->filter_),
                " when building channel stack ",
                grpc_channel_stack_type_string(type))
                .c_str());
        continue;
      }
      dependencies[registration->filter_].insert(after);
    }
    for (F before : registration->before_) {
      if (filter_to_registration.count(before) == 0) {
        gpr_log(
            GPR_DEBUG, "%s",
            absl::StrCat(
                "Filter ", NameFromChannelFilter(before),
                " not registered, but is referenced in the before clause of ",
                NameFromChannelFilter(registration->filter_),
                " when building channel stack ",
                grpc_channel_stack_type_string(type))
                .c_str());
        continue;
      }
      dependencies[before].insert(registration->filter_);
    }
    if (registration->before_all_) {
      for (const auto& other : registrations) {
        if (other.get() == registration.get()) continue;
        if (other->terminal_) continue;
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
  const DependencyMap original = dependencies;
  auto take_ready_dependency = [&]() {
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
    auto* registration = filter_to_registration[filter];
    filters.emplace_back(filter, registration->vtable_,
                         std::move(registration->predicates_),
                         registration->registration_source_);
    for (auto& p : dependencies) {
      p.second.erase(filter);
    }
  }
  // Collect post processors that need to be applied.
  // We've already ensured the one-per-slot constraint, so now we can just
  // collect everything up into a vector and run it in order.
  std::vector<PostProcessor> post_processor_functions;
  for (int i = 0; i < static_cast<int>(PostProcessorSlot::kCount); i++) {
    if (post_processors[i] == nullptr) continue;
    post_processor_functions.emplace_back(std::move(post_processors[i]));
  }
  // Log out the graph we built if that's been requested.
  if (grpc_trace_channel_stack.enabled()) {
    // It can happen that multiple threads attempt to construct a core config at
    // once.
    // This is benign - the first one wins and others are discarded.
    // However, it messes up our logging and makes it harder to reason about the
    // graph, so we add some protection here.
    static Mutex* const m = new Mutex();
    MutexLock lock(m);
    // List the channel stack type (since we'll be repeatedly printing graphs in
    // this loop).
    gpr_log(GPR_INFO,
            "ORDERED CHANNEL STACK %s:", grpc_channel_stack_type_string(type));
    // First build up a map of filter -> file:line: strings, because it helps
    // the readability of this log to get later fields aligned vertically.
    std::map<const grpc_channel_filter*, std::string> loc_strs;
    size_t max_loc_str_len = 0;
    size_t max_filter_name_len = 0;
    auto add_loc_str = [&max_loc_str_len, &loc_strs, &filter_to_registration,
                        &max_filter_name_len](
                           const grpc_channel_filter* filter) {
      max_filter_name_len =
          std::max(strlen(NameFromChannelFilter(filter)), max_filter_name_len);
      const auto registration =
          filter_to_registration[filter]->registration_source_;
      absl::string_view file = registration.file();
      auto slash_pos = file.rfind('/');
      if (slash_pos != file.npos) {
        file = file.substr(slash_pos + 1);
      }
      auto loc_str = absl::StrCat(file, ":", registration.line(), ":");
      max_loc_str_len = std::max(max_loc_str_len, loc_str.length());
      loc_strs.emplace(filter, std::move(loc_str));
    };
    for (const auto& filter : filters) {
      add_loc_str(filter.filter);
    }
    for (const auto& terminal : terminal_filters) {
      add_loc_str(terminal.filter);
    }
    for (auto& loc_str : loc_strs) {
      loc_str.second = absl::StrCat(
          loc_str.second,
          std::string(max_loc_str_len + 2 - loc_str.second.length(), ' '));
    }
    // For each regular filter, print the location registered, the name of the
    // filter, and if it needed to occur after some other filters list those
    // filters too.
    // Note that we use the processed after list here - earlier we turned Before
    // registrations into After registrations and we used those converted
    // registrations to build the final ordering.
    // If you're trying to track down why 'A' is listed as after 'B', look at
    // the following:
    //  - If A is registered with .After({B}), then A will be 'after' B here.
    //  - If B is registered with .Before({A}), then A will be 'after' B here.
    //  - If B is registered as BeforeAll, then A will be 'after' B here.
    for (const auto& filter : filters) {
      auto dep_it = original.find(filter.filter);
      std::string after_str;
      if (dep_it != original.end() && !dep_it->second.empty()) {
        after_str = absl::StrCat(
            std::string(max_filter_name_len + 1 -
                            strlen(NameFromChannelFilter(filter.filter)),
                        ' '),
            "after ",
            absl::StrJoin(
                dep_it->second, ", ",
                [](std::string* out, const grpc_channel_filter* filter) {
                  out->append(NameFromChannelFilter(filter));
                }));
      }
      const auto filter_str =
          absl::StrCat("  ", loc_strs[filter.filter],
                       NameFromChannelFilter(filter.filter), after_str);
      gpr_log(GPR_INFO, "%s", filter_str.c_str());
    }
    // Finally list out the terminal filters and where they were registered
    // from.
    for (const auto& terminal : terminal_filters) {
      const auto filter_str = absl::StrCat(
          "  ", loc_strs[terminal.filter],
          NameFromChannelFilter(terminal.filter),
          std::string(max_filter_name_len + 1 -
                          strlen(NameFromChannelFilter(terminal.filter)),
                      ' '),
          "[terminal]");
      gpr_log(GPR_INFO, "%s", filter_str.c_str());
    }
  }
  // Check if there are no terminal filters: this would be an error.
  // GRPC_CLIENT_DYNAMIC stacks don't use this mechanism, so we don't check that
  // condition here.
  // Right now we only log: many tests end up with a core configuration that
  // is invalid.
  // TODO(ctiller): evaluate if we can turn this into a crash one day.
  // Right now it forces too many tests to know about channel initialization,
  // either by supplying a valid configuration or by including an opt-out flag.
  if (terminal_filters.empty() && type != GRPC_CLIENT_DYNAMIC) {
    gpr_log(
        GPR_ERROR,
        "No terminal filters registered for channel stack type %s; this is "
        "common for unit tests messing with CoreConfiguration, but will result "
        "in a ChannelInit::CreateStack that never completes successfully.",
        grpc_channel_stack_type_string(type));
  }
  return StackConfig{std::move(filters), std::move(terminal_filters),
                     std::move(post_processor_functions)};
};

ChannelInit ChannelInit::Builder::Build() {
  ChannelInit result;
  for (int i = 0; i < GRPC_NUM_CHANNEL_STACK_TYPES; i++) {
    result.stack_configs_[i] =
        BuildStackConfig(filters_[i], post_processors_[i],
                         static_cast<grpc_channel_stack_type>(i));
  }
  return result;
}

bool ChannelInit::Filter::CheckPredicates(const ChannelArgs& args) const {
  for (const auto& predicate : predicates) {
    if (!predicate(args)) return false;
  }
  return true;
}

bool ChannelInit::CreateStack(ChannelStackBuilder* builder) const {
  const auto& stack_config = stack_configs_[builder->channel_stack_type()];
  for (const auto& filter : stack_config.filters) {
    if (!filter.CheckPredicates(builder->channel_args())) continue;
    builder->AppendFilter(filter.filter);
  }
  int found_terminators = 0;
  for (const auto& terminator : stack_config.terminators) {
    if (!terminator.CheckPredicates(builder->channel_args())) continue;
    builder->AppendFilter(terminator.filter);
    ++found_terminators;
  }
  if (found_terminators != 1) {
    std::string error = absl::StrCat(
        found_terminators,
        " terminating filters found creating a channel of type ",
        grpc_channel_stack_type_string(builder->channel_stack_type()),
        " with arguments ", builder->channel_args().ToString(),
        " (we insist upon one and only one terminating "
        "filter)\n");
    if (stack_config.terminators.empty()) {
      absl::StrAppend(&error, "  No terminal filters were registered");
    } else {
      for (const auto& terminator : stack_config.terminators) {
        absl::StrAppend(
            &error, "  ", NameFromChannelFilter(terminator.filter),
            " registered @ ", terminator.registration_source.file(), ":",
            terminator.registration_source.line(), ": enabled = ",
            terminator.CheckPredicates(builder->channel_args()) ? "true"
                                                                : "false",
            "\n");
      }
    }
    gpr_log(GPR_ERROR, "%s", error.c_str());
    return false;
  }
  for (const auto& post_processor : stack_config.post_processors) {
    post_processor(*builder);
  }
  return true;
}

absl::StatusOr<ChannelInit::StackSegment> ChannelInit::CreateStackSegment(
    grpc_channel_stack_type type, const ChannelArgs& args) const {
  const auto& stack_config = stack_configs_[type];
  std::vector<StackSegment::ChannelFilter> filters;
  size_t channel_data_size = 0;
  size_t channel_data_alignment = 0;
  // Based on predicates build a list of filters to include in this segment.
  for (const auto& filter : stack_config.filters) {
    if (!filter.CheckPredicates(args)) continue;
    if (filter.vtable == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Filter ", NameFromChannelFilter(filter.filter),
                       " has no v3-callstack vtable"));
    }
    channel_data_alignment =
        std::max(channel_data_alignment, filter.vtable->alignment);
    if (channel_data_size % filter.vtable->alignment != 0) {
      channel_data_size += filter.vtable->alignment -
                           (channel_data_size % filter.vtable->alignment);
    }
    filters.push_back({channel_data_size, filter.vtable});
    channel_data_size += filter.vtable->size;
  }
  // Shortcut for empty segments.
  if (filters.empty()) return StackSegment();
  // Allocate memory for the channel data, initialize channel filters into it.
  uint8_t* p = static_cast<uint8_t*>(
      gpr_malloc_aligned(channel_data_size, channel_data_alignment));
  for (size_t i = 0; i < filters.size(); i++) {
    auto r = filters[i].vtable->init(p + filters[i].offset, args);
    if (!r.ok()) {
      for (size_t j = 0; j < i; j++) {
        filters[j].vtable->destroy(p + filters[j].offset);
      }
      gpr_free_aligned(p);
      return r;
    }
  }
  return StackSegment(std::move(filters), p);
}

///////////////////////////////////////////////////////////////////////////////
// ChannelInit::StackSegment

ChannelInit::StackSegment::StackSegment(std::vector<ChannelFilter> filters,
                                        uint8_t* channel_data)
    : data_(MakeRefCounted<ChannelData>(std::move(filters), channel_data)) {}

void ChannelInit::StackSegment::AddToCallFilterStack(
    CallFilters::StackBuilder& builder) {
  if (data_ == nullptr) return;
  data_->AddToCallFilterStack(builder);
  builder.AddOwnedObject(data_);
};

ChannelInit::StackSegment::ChannelData::ChannelData(
    std::vector<ChannelFilter> filters, uint8_t* channel_data)
    : filters_(std::move(filters)), channel_data_(channel_data) {}

void ChannelInit::StackSegment::ChannelData::AddToCallFilterStack(
    CallFilters::StackBuilder& builder) {
  for (const auto& filter : filters_) {
    filter.vtable->add_to_stack_builder(channel_data_ + filter.offset, builder);
  }
}

ChannelInit::StackSegment::ChannelData::~ChannelData() {
  for (const auto& filter : filters_) {
    filter.vtable->destroy(channel_data_ + filter.offset);
  }
  gpr_free_aligned(channel_data_);
}

}  // namespace grpc_core
