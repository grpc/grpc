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

#include <stdint.h>

#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/call_filters.h"

/// This module provides a way for plugins (and the grpc core library itself)
/// to register mutators for channel stacks.
/// It also provides a universal entry path to run those mutators to build
/// a channel stack for various subsystems.

namespace grpc_core {

// HACK HACK HACK
// Right now grpc_channel_filter has a bunch of dependencies high in the stack,
// but this code needs to live as a dependency of CoreConfiguration so we need
// to be careful to ensure no dependency loops.
//
// We absolutely must be able to get the name from a filter - for stability and
// for debuggability.
//
// So we export this function, and have it filled in by the higher level code at
// static initialization time.
//
// TODO(ctiller): remove this. When we define a FilterFactory type, that type
// can be specified with the right constraints to be depended upon by this code,
// and that type can export a `string_view Name()` method.
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
  enum class PostProcessorSlot : uint8_t {
    kAuthSubstitution,
    kXdsChannelStackModifier,
    kCount
  };

  // Vtable-like data structure for channel data initialization
  struct ChannelFilterVtable {
    size_t size;
    size_t alignment;
    absl::Status (*init)(void* data, const ChannelArgs& args);
    void (*destroy)(void* data);
    void (*add_to_stack_builder)(void* data,
                                 CallFilters::StackBuilder& builder);
  };

  class FilterRegistration {
   public:
    // TODO(ctiller): Remove grpc_channel_filter* arg when that can be
    // deprecated (once filter stack is removed).
    explicit FilterRegistration(const grpc_channel_filter* filter,
                                const ChannelFilterVtable* vtable,
                                SourceLocation registration_source)
        : filter_(filter),
          vtable_(vtable),
          registration_source_(registration_source) {}
    FilterRegistration(const FilterRegistration&) = delete;
    FilterRegistration& operator=(const FilterRegistration&) = delete;

    // Ensure that this filter is placed *after* the filters listed here.
    // By Build() time all filters listed here must also be registered against
    // the same channel stack type as this registration.
    template <typename Filter>
    FilterRegistration& After() {
      return After({&Filter::kFilter});
    }
    // Ensure that this filter is placed *before* the filters listed here.
    // By Build() time all filters listed here must also be registered against
    // the same channel stack type as this registration.
    template <typename Filter>
    FilterRegistration& Before() {
      return Before({&Filter::kFilter});
    }

    // Ensure that this filter is placed *after* the filters listed here.
    // By Build() time all filters listed here must also be registered against
    // the same channel stack type as this registration.
    // TODO(ctiller): remove in favor of the version that does not mention
    // grpc_channel_filter
    FilterRegistration& After(
        std::initializer_list<const grpc_channel_filter*> filters);
    // Ensure that this filter is placed *before* the filters listed here.
    // By Build() time all filters listed here must also be registered against
    // the same channel stack type as this registration.
    // TODO(ctiller): remove in favor of the version that does not mention
    // grpc_channel_filter
    FilterRegistration& Before(
        std::initializer_list<const grpc_channel_filter*> filters);
    // Add a predicate for this filters inclusion.
    // If the predicate returns true the filter will be included in the stack.
    // Predicates do not affect the ordering of the filter stack: we first
    // topologically sort (once, globally) and only later apply predicates
    // per-channel creation.
    // Multiple predicates can be added to each registration.
    FilterRegistration& If(InclusionPredicate predicate);
    FilterRegistration& IfNot(InclusionPredicate predicate);
    // Add a predicate that only includes this filter if a channel arg is
    // present.
    FilterRegistration& IfHasChannelArg(const char* arg);
    // Add a predicate that only includes this filter if a boolean channel arg
    // is true (with default_value being used if the argument is not present).
    FilterRegistration& IfChannelArg(const char* arg, bool default_value);
    // Mark this filter as being terminal.
    // Exactly one terminal filter will be added at the end of each filter
    // stack.
    // If multiple are defined they are tried in registration order, and the
    // first terminal filter whos predicates succeed is selected.
    FilterRegistration& Terminal() {
      terminal_ = true;
      return *this;
    }
    // Ensure this filter appears at the top of the stack.
    // Effectively adds a 'Before' constraint on every other filter.
    // Adding this to more than one filter will cause a loop.
    FilterRegistration& BeforeAll() {
      before_all_ = true;
      return *this;
    }
    // Add a predicate that ensures this filter does not appear in the minimal
    // stack.
    FilterRegistration& ExcludeFromMinimalStack();

   private:
    friend class ChannelInit;
    const grpc_channel_filter* const filter_;
    const ChannelFilterVtable* const vtable_;
    std::vector<const grpc_channel_filter*> after_;
    std::vector<const grpc_channel_filter*> before_;
    std::vector<InclusionPredicate> predicates_;
    bool terminal_ = false;
    bool before_all_ = false;
    SourceLocation registration_source_;
  };

  class Builder {
   public:
    // Register a builder in the normal filter registration pass.
    // This occurs first during channel build time.
    // The FilterRegistration methods can be called to declaratively define
    // properties of the filter being registered.
    // TODO(ctiller): remove in favor of the version that does not mention
    // grpc_channel_filter
    FilterRegistration& RegisterFilter(
        grpc_channel_stack_type type, const grpc_channel_filter* filter,
        const ChannelFilterVtable* vtable = nullptr,
        SourceLocation registration_source = {});
    template <typename Filter>
    FilterRegistration& RegisterFilter(
        grpc_channel_stack_type type, SourceLocation registration_source = {}) {
      return RegisterFilter(type, &Filter::kFilter,
                            VtableForType<Filter>::vtable(),
                            registration_source);
    }

    // Register a post processor for the builder.
    // These run after the main graph has been placed into the builder.
    // At most one filter per slot per channel stack type can be added.
    // If at all possible, prefer to use the RegisterFilter() mechanism to add
    // filters to the system - this should be a last resort escape hatch.
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

  // A set of channel filters that can be added to a call stack.
  // TODO(ctiller): move this out so it can be used independently of
  // the global registration mechanisms.
  class StackSegment final {
   public:
    // Registration of one channel filter in the stack.
    struct ChannelFilter {
      size_t offset;
      const ChannelFilterVtable* vtable;
    };

    StackSegment() = default;
    explicit StackSegment(std::vector<ChannelFilter> filters,
                          uint8_t* channel_data);
    StackSegment(const StackSegment& other) = delete;
    StackSegment& operator=(const StackSegment& other) = delete;
    StackSegment(StackSegment&& other) noexcept = default;
    StackSegment& operator=(StackSegment&& other) = default;

    // Add this segment to a call filter stack builder
    void AddToCallFilterStack(CallFilters::StackBuilder& builder);

   private:
    // Combined channel data for the stack
    class ChannelData : public RefCounted<ChannelData> {
     public:
      explicit ChannelData(std::vector<ChannelFilter> filters,
                           uint8_t* channel_data);
      ~ChannelData() override;

      void AddToCallFilterStack(CallFilters::StackBuilder& builder);

     private:
      std::vector<ChannelFilter> filters_;
      uint8_t* channel_data_;
    };

    RefCountedPtr<ChannelData> data_;
  };

  /// Construct a channel stack of some sort: see channel_stack.h for details
  /// \a builder is the channel stack builder to build into.
  GRPC_MUST_USE_RESULT
  bool CreateStack(ChannelStackBuilder* builder) const;

  // Create a segment of a channel stack.
  // Terminators and post processors are not included in this construction:
  // terminators are a legacy filter-stack concept, and post processors
  // need to migrate to other mechanisms.
  // TODO(ctiller): figure out other mechanisms.
  absl::StatusOr<StackSegment> CreateStackSegment(
      grpc_channel_stack_type type, const ChannelArgs& args) const;

 private:
  struct Filter {
    Filter(const grpc_channel_filter* filter, const ChannelFilterVtable* vtable,
           std::vector<InclusionPredicate> predicates,
           SourceLocation registration_source)
        : filter(filter),
          vtable(vtable),
          predicates(std::move(predicates)),
          registration_source(registration_source) {}
    const grpc_channel_filter* filter;
    const ChannelFilterVtable* vtable;
    std::vector<InclusionPredicate> predicates;
    SourceLocation registration_source;
    bool CheckPredicates(const ChannelArgs& args) const;
  };
  struct StackConfig {
    std::vector<Filter> filters;
    std::vector<Filter> terminators;
    std::vector<PostProcessor> post_processors;
  };

  template <typename T, typename = void>
  struct VtableForType {
    static const ChannelFilterVtable* vtable() { return nullptr; }
  };

  template <typename T>
  struct VtableForType<T, absl::void_t<typename T::Call>> {
    static const ChannelFilterVtable kVtable;
    static const ChannelFilterVtable* vtable() { return &kVtable; }
  };

  StackConfig stack_configs_[GRPC_NUM_CHANNEL_STACK_TYPES];

  static StackConfig BuildStackConfig(
      const std::vector<std::unique_ptr<FilterRegistration>>& registrations,
      PostProcessor* post_processors, grpc_channel_stack_type type);
};

template <typename T>
const ChannelInit::ChannelFilterVtable
    ChannelInit::VtableForType<T, absl::void_t<typename T::Call>>::kVtable = {
        sizeof(T), alignof(T),
        [](void* data, const ChannelArgs& args) -> absl::Status {
          // TODO(ctiller): fill in ChannelFilter::Args (2nd arg)
          absl::StatusOr<T> r = T::Create(args, {});
          if (!r.ok()) return r.status();
          new (data) T(std::move(*r));
          return absl::OkStatus();
        },
        [](void* data) { static_cast<T*>(data)->~T(); },
        [](void* data, CallFilters::StackBuilder& builder) {
          builder.Add(static_cast<T*>(data));
        }};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_INIT_H
