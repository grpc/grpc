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

#ifndef GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_INIT_H
#define GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_INIT_H

#include <grpc/support/port_platform.h>

#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/surface/channel_stack_type.h"

#define GRPC_CHANNEL_INIT_BUILTIN_PRIORITY 10000

/// This module provides a way for plugins (and the grpc core library itself)
/// to register mutators for channel stacks.
/// It also provides a universal entry path to run those mutators to build
/// a channel stack for various subsystems.

namespace grpc_core {

extern const char* (*NameFromChannelFilter)(const grpc_channel_filter*);

class ChannelInit {
 public:
  // Predicate for if a filter registration applies
  using InclusionPredicate = absl::AnyInvocable<bool(const ChannelArgs&) const>;
  // Post processor for the channel stack - applied in PostProcessorSlot order
  using PostProcessor = absl::AnyInvocable<void(ChannelStackBuilder&) const>;
  // Post processing slots - up to one PostProcessor per slot can be registered
  // They run after filters registered are added to the channel stack builder,
  // but before Build is called - allowing ad-hoc mutation to the channel stack.
  enum class PostProcessorSlot { kXdsChannelStackModifier, kCount };

  class FilterRegistration {
   public:
    explicit FilterRegistration(const grpc_channel_filter* filter)
        : filter_(filter) {}
    FilterRegistration(const FilterRegistration&) = delete;
    FilterRegistration& operator=(const FilterRegistration&) = delete;

    FilterRegistration& After(
        std::initializer_list<const grpc_channel_filter*> filters);
    FilterRegistration& Before(
        std::initializer_list<const grpc_channel_filter*> filters);
    FilterRegistration& If(InclusionPredicate predicate);
    FilterRegistration& IfHasChannelArg(const char* arg) {
      return If([arg](const ChannelArgs& args) { return args.Contains(arg); });
    }
    FilterRegistration& IfChannelArg(const char* arg, bool default_value) {
      return If([arg, default_value](const ChannelArgs& args) {
        return args.GetBool(arg).value_or(default_value);
      });
    }
    FilterRegistration& Terminal() {
      terminal_ = true;
      return *this;
    }
    FilterRegistration& BeforeAll() {
      before_all_ = true;
      return *this;
    }
    FilterRegistration& ExcludeFromMinimalStack() {
      return If(
          [](const ChannelArgs& args) { return !args.WantMinimalStack(); });
    }

   private:
    friend class ChannelInit;
    const grpc_channel_filter* const filter_;
    std::vector<const grpc_channel_filter*> after_;
    std::vector<const grpc_channel_filter*> before_;
    std::vector<InclusionPredicate> predicates_;
    bool terminal_ = false;
    bool before_all_ = false;
  };

  class Builder {
   public:
    FilterRegistration& RegisterFilter(grpc_channel_stack_type type,
                                       const grpc_channel_filter* filter);

    void RegisterPostProcessor(grpc_channel_stack_type type,
                               PostProcessorSlot slot,
                               PostProcessor post_processor) {
      auto& slot_value = post_processors_[type][static_cast<int>(slot)];
      GPR_ASSERT(slot_value == nullptr);
      slot_value = std::move(post_processor);
    }

    /// Finalize registration.
    ChannelInit Build();

   private:
    std::vector<std::unique_ptr<FilterRegistration>>
        filters_[GRPC_NUM_CHANNEL_STACK_TYPES];
    PostProcessor post_processors_[GRPC_NUM_CHANNEL_STACK_TYPES]
                                  [static_cast<int>(PostProcessorSlot::kCount)];
  };

  /// Construct a channel stack of some sort: see channel_stack.h for details
  /// \a builder is the channel stack builder to build into.
  bool CreateStack(ChannelStackBuilder* builder) const;

 private:
  struct Filter {
    Filter(const grpc_channel_filter* filter, InclusionPredicate predicate)
        : filter(filter), predicate(std::move(predicate)) {}
    const grpc_channel_filter* filter;
    InclusionPredicate predicate;
  };
  struct StackConfig {
    std::vector<Filter> filters;
    std::vector<Filter> terminators;
    std::vector<PostProcessor> post_processors_;
  };
  StackConfig stack_configs_[GRPC_NUM_CHANNEL_STACK_TYPES];

  static StackConfig BuildStackConfig(
      const std::vector<std::unique_ptr<FilterRegistration>>& registrations,
      PostProcessor* post_processors, grpc_channel_stack_type type);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_INIT_H
