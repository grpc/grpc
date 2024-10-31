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

#include "src/core/lib/surface/channel_init.h"

#include <grpc/support/port_platform.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <type_traits>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/util/crash.h"
#include "src/core/util/sync.h"
#include "src/core/util/unique_type_name.h"

namespace grpc_core {

UniqueTypeName (*NameFromChannelFilter)(const grpc_channel_filter*);

namespace {
struct CompareChannelFiltersByName {
  bool operator()(UniqueTypeName a, UniqueTypeName b) const {
    // Compare lexicographically instead of by pointer value so that different
    // builds make the same choices.
    return a.name() < b.name();
  }
};
}  // namespace

ChannelInit::FilterRegistration& ChannelInit::FilterRegistration::After(
    std::initializer_list<UniqueTypeName> filters) {
  for (auto filter : filters) {
    after_.push_back(filter);
  }
  return *this;
}

ChannelInit::FilterRegistration& ChannelInit::FilterRegistration::Before(
    std::initializer_list<UniqueTypeName> filters) {
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
    grpc_channel_stack_type type, UniqueTypeName name,
    const grpc_channel_filter* filter, FilterAdder filter_adder,
    SourceLocation registration_source) {
  filters_[type].emplace_back(std::make_unique<FilterRegistration>(
      name, filter, filter_adder, registration_source));
  return *filters_[type].back();
}

class ChannelInit::DependencyTracker {
 public:
  // Declare that a filter exists.
  void Declare(FilterRegistration* registration) {
    nodes_.emplace(registration->name_, registration);
  }
  // Insert an edge from a to b
  // Both nodes must be declared.
  void InsertEdge(UniqueTypeName a, UniqueTypeName b) {
    auto it_a = nodes_.find(a);
    auto it_b = nodes_.find(b);
    if (it_a == nodes_.end()) {
      GRPC_TRACE_LOG(channel_stack, INFO)
          << "gRPC Filter " << a.name()
          << " was not declared before adding an edge to " << b.name();
      return;
    }
    if (it_b == nodes_.end()) {
      GRPC_TRACE_LOG(channel_stack, INFO)
          << "gRPC Filter " << b.name()
          << " was not declared before adding an edge from " << a.name();
      return;
    }
    auto& node_a = it_a->second;
    auto& node_b = it_b->second;
    node_a.dependents.push_back(&node_b);
    node_b.all_dependencies.push_back(a);
    ++node_b.waiting_dependencies;
  }

  // Finish the dependency graph and begin iteration.
  void FinishDependencyMap() {
    for (auto& p : nodes_) {
      if (p.second.waiting_dependencies == 0) {
        ready_dependencies_.emplace(&p.second);
      }
    }
  }

  FilterRegistration* Next() {
    if (ready_dependencies_.empty()) {
      CHECK_EQ(nodes_taken_, nodes_.size()) << "Unresolvable graph of channel "
                                               "filters:\n"
                                            << GraphString();
      return nullptr;
    }
    auto next = ready_dependencies_.top();
    ready_dependencies_.pop();
    if (!ready_dependencies_.empty() &&
        next.node->ordering() != Ordering::kDefault) {
      // Constraint: if we use ordering other than default, then we must have an
      // unambiguous pick. If there is ambiguity, we must fix it by adding
      // explicit ordering constraints.
      CHECK_NE(next.node->ordering(),
               ready_dependencies_.top().node->ordering())
          << "Ambiguous ordering between " << next.node->name() << " and "
          << ready_dependencies_.top().node->name();
    }
    for (Node* dependent : next.node->dependents) {
      CHECK_GT(dependent->waiting_dependencies, 0u);
      --dependent->waiting_dependencies;
      if (dependent->waiting_dependencies == 0) {
        ready_dependencies_.emplace(dependent);
      }
    }
    ++nodes_taken_;
    return next.node->registration;
  }

  // Debug helper to dump the graph
  std::string GraphString() const {
    std::string result;
    for (const auto& p : nodes_) {
      absl::StrAppend(&result, p.first, " ->");
      for (const auto& d : p.second.all_dependencies) {
        absl::StrAppend(&result, " ", d);
      }
      absl::StrAppend(&result, "\n");
    }
    return result;
  }

  absl::Span<const UniqueTypeName> DependenciesFor(UniqueTypeName name) const {
    auto it = nodes_.find(name);
    CHECK(it != nodes_.end()) << "Filter " << name.name() << " not found";
    return it->second.all_dependencies;
  }

 private:
  struct Node {
    explicit Node(FilterRegistration* registration)
        : registration(registration) {}
    // Nodes that depend on this node
    std::vector<Node*> dependents;
    // Nodes that this node depends on - for debugging purposes only
    std::vector<UniqueTypeName> all_dependencies;
    // The registration for this node
    FilterRegistration* registration;
    // Number of nodes this node is waiting on
    size_t waiting_dependencies = 0;

    Ordering ordering() const { return registration->ordering_; }
    absl::string_view name() const { return registration->name_.name(); }
  };
  struct ReadyDependency {
    explicit ReadyDependency(Node* node) : node(node) {}
    Node* node;
    bool operator<(const ReadyDependency& other) const {
      // Sort first on ordering, and then lexically on name.
      // The lexical sort means that the ordering is stable between builds
      // (UniqueTypeName ordering is not stable between builds).
      return node->ordering() > other.node->ordering() ||
             (node->ordering() == other.node->ordering() &&
              node->name() > other.node->name());
    }
  };
  absl::flat_hash_map<UniqueTypeName, Node> nodes_;
  std::priority_queue<ReadyDependency> ready_dependencies_;
  size_t nodes_taken_ = 0;
};

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
  DependencyTracker dependencies;
  std::vector<Filter> terminal_filters;
  for (const auto& registration : registrations) {
    if (registration->terminal_) {
      CHECK(registration->after_.empty());
      CHECK(registration->before_.empty());
      CHECK(!registration->before_all_);
      CHECK_EQ(registration->ordering_, Ordering::kDefault);
      terminal_filters.emplace_back(
          registration->name_, registration->filter_, nullptr,
          std::move(registration->predicates_), registration->version_,
          registration->ordering_, registration->registration_source_);
    } else {
      dependencies.Declare(registration.get());
    }
  }
  for (const auto& registration : registrations) {
    if (registration->terminal_) continue;
    for (UniqueTypeName after : registration->after_) {
      dependencies.InsertEdge(after, registration->name_);
    }
    for (UniqueTypeName before : registration->before_) {
      dependencies.InsertEdge(registration->name_, before);
    }
    if (registration->before_all_) {
      for (const auto& other : registrations) {
        if (other.get() == registration.get()) continue;
        if (other->terminal_) continue;
        dependencies.InsertEdge(registration->name_, other->name_);
      }
    }
  }
  // Phase 2: Build a list of filters in dependency order.
  // We can simply iterate through and add anything with no dependency.
  // We then remove that filter from the dependency list of all other filters.
  // We repeat until we have no more filters to add.
  dependencies.FinishDependencyMap();
  std::vector<Filter> filters;
  while (auto registration = dependencies.Next()) {
    filters.emplace_back(
        registration->name_, registration->filter_, registration->filter_adder_,
        std::move(registration->predicates_), registration->version_,
        registration->ordering_, registration->registration_source_);
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
  if (GRPC_TRACE_FLAG_ENABLED(channel_stack)) {
    PrintChannelStackTrace(type, registrations, dependencies, filters,
                           terminal_filters);
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
    LOG(ERROR) << "No terminal filters registered for channel stack type "
               << grpc_channel_stack_type_string(type)
               << "; this is common for unit tests messing with "
                  "CoreConfiguration, but will result in a "
                  "ChannelInit::CreateStack that never completes successfully.";
  }
  return StackConfig{std::move(filters), std::move(terminal_filters),
                     std::move(post_processor_functions)};
};

void ChannelInit::PrintChannelStackTrace(
    grpc_channel_stack_type type,
    const std::vector<std::unique_ptr<ChannelInit::FilterRegistration>>&
        registrations,
    const DependencyTracker& dependencies, const std::vector<Filter>& filters,
    const std::vector<Filter>& terminal_filters) {
  // It can happen that multiple threads attempt to construct a core config at
  // once.
  // This is benign - the first one wins and others are discarded.
  // However, it messes up our logging and makes it harder to reason about the
  // graph, so we add some protection here.
  static Mutex* const m = new Mutex();
  MutexLock lock(m);
  // List the channel stack type (since we'll be repeatedly printing graphs in
  // this loop).
  LOG(INFO) << "ORDERED CHANNEL STACK " << grpc_channel_stack_type_string(type)
            << ":";
  // First build up a map of filter -> file:line: strings, because it helps
  // the readability of this log to get later fields aligned vertically.
  absl::flat_hash_map<UniqueTypeName, std::string> loc_strs;
  size_t max_loc_str_len = 0;
  size_t max_filter_name_len = 0;
  auto add_loc_str = [&max_loc_str_len, &loc_strs, &registrations,
                      &max_filter_name_len](UniqueTypeName name) {
    max_filter_name_len = std::max(name.name().length(), max_filter_name_len);
    for (const auto& registration : registrations) {
      if (registration->name_ == name) {
        auto source = registration->registration_source_;
        absl::string_view file = source.file();
        auto slash_pos = file.rfind('/');
        if (slash_pos != file.npos) {
          file = file.substr(slash_pos + 1);
        }
        auto loc_str = absl::StrCat(file, ":", source.line(), ":");
        max_loc_str_len = std::max(max_loc_str_len, loc_str.length());
        loc_strs.emplace(name, std::move(loc_str));
        break;
      }
    }
  };
  for (const auto& filter : filters) {
    add_loc_str(filter.name);
  }
  for (const auto& terminal : terminal_filters) {
    add_loc_str(terminal.name);
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
    auto after = dependencies.DependenciesFor(filter.name);
    std::string after_str;
    if (!after.empty()) {
      after_str = absl::StrCat(
          std::string(max_filter_name_len + 1 - filter.name.name().length(),
                      ' '),
          "after ", absl::StrJoin(after, ", "));
    } else {
      after_str =
          std::string(max_filter_name_len - filter.name.name().length(), ' ');
    }
    LOG(INFO) << "  " << loc_strs[filter.name] << filter.name << after_str
              << " [" << filter.ordering << "/" << filter.version << "]";
  }
  // Finally list out the terminal filters and where they were registered
  // from.
  for (const auto& terminal : terminal_filters) {
    const auto filter_str = absl::StrCat(
        "  ", loc_strs[terminal.name], terminal.name,
        std::string(max_filter_name_len + 1 - terminal.name.name().length(),
                    ' '),
        "[terminal]");
    LOG(INFO) << filter_str;
  }
}

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
    if (SkipV2(filter.version)) continue;
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
        absl::StrAppend(&error, "  ", terminator.name, " registered @ ",
                        terminator.registration_source.file(), ":",
                        terminator.registration_source.line(), ": enabled = ",
                        terminator.CheckPredicates(builder->channel_args())
                            ? "true"
                            : "false",
                        "\n");
      }
    }
    LOG(ERROR) << error;
    return false;
  }
  for (const auto& post_processor : stack_config.post_processors) {
    post_processor(*builder);
  }
  return true;
}

void ChannelInit::AddToInterceptionChainBuilder(
    grpc_channel_stack_type type, InterceptionChainBuilder& builder) const {
  const auto& stack_config = stack_configs_[type];
  // Based on predicates build a list of filters to include in this segment.
  for (const auto& filter : stack_config.filters) {
    if (SkipV3(filter.version)) continue;
    if (!filter.CheckPredicates(builder.channel_args())) continue;
    if (filter.filter_adder == nullptr) {
      builder.Fail(absl::InvalidArgumentError(
          absl::StrCat("Filter ", filter.name, " has no v3-callstack vtable")));
      return;
    }
    filter.filter_adder(builder);
  }
}

}  // namespace grpc_core
