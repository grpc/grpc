// Copyright 2024 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/interception_chain.h"

#include "call_destination.h"

#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/transport/call_filters.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// HijackedCall

namespace interception_chain_detail {

CallInitiator HijackedCall::MakeCall() {
  auto metadata = Arena::MakePooled<ClientMetadata>();
  *metadata = metadata_->Copy();
  return MakeCallWithMetadata(std::move(metadata));
}

CallInitiator HijackedCall::MakeCallWithMetadata(
    ClientMetadataHandle metadata) {
  auto call =
      grpc_core::MakeCallPair(std::move(metadata), call_handler_.event_engine(),
                              call_handler_.arena(), false);
  destination_->StartCall(std::move(call.unstarted_handler));
  return std::move(call.initiator);
}

class CallStarter final : public UnstartedCallDestination {
 public:
  CallStarter(RefCountedPtr<CallFilters::Stack> stack,
              RefCountedPtr<CallDestination> destination)
      : stack_(std::move(stack)), destination_(std::move(destination)) {}

  void Orphan() override {
    stack_.reset();
    destination_.reset();
  }

  void StartCall(UnstartedCallHandler unstarted_call_handler) override {
    destination_->HandleCall(unstarted_call_handler.StartCall(stack_));
  }

 private:
  RefCountedPtr<CallFilters::Stack> stack_;
  RefCountedPtr<CallDestination> destination_;
};

class TerminalInterceptor final : public UnstartedCallDestination {
 public:
  explicit TerminalInterceptor(
      RefCountedPtr<CallFilters::Stack> stack,
      RefCountedPtr<UnstartedCallDestination> destination)
      : stack_(std::move(stack)), destination_(std::move(destination)) {}

  void Orphan() override {
    stack_.reset();
    destination_.reset();
  }

  void StartCall(UnstartedCallHandler unstarted_call_handler) override {
    unstarted_call_handler.SpawnGuarded(
        "start_call",
        Map(HijackCall(std::move(unstarted_call_handler), destination_, stack_),
            [](ValueOrFailure<HijackedCall> hijacked_call) -> StatusFlag {
              if (!hijacked_call.ok()) return Failure{};
              ForwardCall(hijacked_call.value().original_call_handler(),
                          hijacked_call.value().MakeLastCall());
              return Success{};
            }));
  }

 private:
  RefCountedPtr<CallFilters::Stack> stack_;
  RefCountedPtr<UnstartedCallDestination> destination_;
};

}  // namespace interception_chain_detail

///////////////////////////////////////////////////////////////////////////////
// ChainAllocator

namespace interception_chain_detail {

ChainAllocator::~ChainAllocator() {
  if (chain_data_ == nullptr) return;

  for (size_t i = 0; i < instantiated_filters_; i++) {
    filter_defs_[i]->footprint.destroy(
        filter_defs_[i]->footprint.OffsetPtr(chain_data_));
  }

  if (chain_data_ != nullptr) {
    gpr_free_aligned(chain_data_);
  }
}

void ChainAllocator::Append(InterceptorDef& interceptor) {
  Append(interceptor.footprint);
  interceptor_defs_.push_back(&interceptor);
}

void ChainAllocator::Append(FilterDef& filter) {
  Append(filter.footprint);
  filter_defs_.push_back(&filter);
}

void ChainAllocator::Append(Footprint& footprint) {
  if (chain_size_ % footprint.alignment != 0) {
    chain_size_ += footprint.alignment - (chain_size_ % footprint.alignment);
  }
  footprint.offset = chain_size_;
  chain_size_ += footprint.size;
  chain_alignment_ = std::max(chain_alignment_, footprint.alignment);
  if (footprint.destroy != nullptr) {
    destructors_.push_back(Destructor{footprint.destroy, footprint.offset});
  }
}

absl::Status ChainAllocator::Instantiate(const ChannelArgs& args) {
  GPR_ASSERT(chain_data_ == nullptr);

  chain_data_ = gpr_malloc_aligned(chain_size_, chain_alignment_);

  for (instantiated_filters_ = 0; instantiated_filters_ < filter_defs_.size();
       ++instantiated_filters_) {
    auto& filter = *filter_defs_[instantiated_filters_];
    auto status = filter.init(filter.footprint.OffsetPtr(chain_data_), args);
    if (!status.ok()) return status;
  }

  interceptors_.reserve(interceptor_defs_.size());
  for (instantiated_interceptors_ = 0;
       instantiated_interceptors_ < interceptor_defs_.size();
       ++instantiated_interceptors_) {
    auto interceptor = interceptor_defs_[instantiated_interceptors_];
    auto* interceptor_instance = interceptor->footprint.OffsetPtr(chain_data_);
    auto status = interceptor->init(interceptor_instance, args);
    if (!status.ok()) return status.status();
    interceptors_.push_back(status.value());
  }

  return absl::OkStatus();
}

RefCountedPtr<CallFilters::Stack> ChainAllocator::MakeFilterStack(
    absl::Span<const FilterDef> filters) {
  CallFilters::StackBuilder stack_builder;
  for (auto& filter : filters) {
    filter.add_to_stack_builder(stack_builder,
                                filter.footprint.OffsetPtr(chain_data_));
  }
  return stack_builder.Build();
}

RefCountedPtr<UnstartedCallDestination> ChainAllocator::LinkDestinations(
    RefCountedPtr<UnstartedCallDestination> final_destination) {
  for (size_t building = 0; building < interceptors_.size(); ++building) {
    if (building + 1 == interceptors_.size()) {
      interceptors_[building]->wrapped_destination_ = final_destination;
    } else {
      interceptors_[building]->wrapped_destination_ =
          interceptors_[building + 1];
    }
  }
  return interceptors_.empty() ? final_destination : interceptors_.front();
}
}  // namespace interception_chain_detail

///////////////////////////////////////////////////////////////////////////////
// InterceptionChain

InterceptionChain::~InterceptionChain() {
  uint8_t* p = static_cast<uint8_t*>(chain_data_);
  for (auto& destructor : destructors_) {
    destructor.destroy(p + destructor.offset);
  }
  gpr_free_aligned(chain_data_);
}

absl::StatusOr<RefCountedPtr<InterceptionChain>>
InterceptionChain::Builder::Build(const ChannelArgs& args) {
  // Build the chain memory layout.
  // We allocate a single block, and place each interceptor along with its
  // filters in the block.
  interception_chain_detail::ChainAllocator chain_allocator;
  for (auto& interceptor : interceptors_) {
    chain_allocator.Append(interceptor);
    for (auto& filter : interceptor.filters) {
      chain_allocator.Append(filter);
    }
  }
  for (auto& filter : building_filters_) {
    chain_allocator.Append(filter);
  }

  // Instantiate the interceptors.
  auto instantiation_result = chain_allocator.Instantiate(args);
  if (!instantiation_result.ok()) return instantiation_result;

  // Create filter stacks for each interceptor.
  for (size_t building = 0; building < interceptors_.size(); ++building) {
    chain_allocator.interceptor(building)->filter_stack_ =
        chain_allocator.MakeFilterStack(interceptors_[building].filters);
  }

  // Build the final UnstartedCallDestination in the chain - what we do here
  // depends on both the type of the final destination and the filters we have
  // that haven't been captured into an Interceptor yet.
  absl::StatusOr<RefCountedPtr<UnstartedCallDestination>> terminator = Match(
      final_destination_,
      [this, &chain_allocator](
          RefCountedPtr<UnstartedCallDestination> final_destination)
          -> absl::StatusOr<RefCountedPtr<UnstartedCallDestination>> {
        if (!building_filters_.empty()) {
          // TODO(ctiller): consider interjecting a hijacker here
          return MakeRefCounted<interception_chain_detail::TerminalInterceptor>(
              chain_allocator.MakeFilterStack(building_filters_),
              final_destination);
        }
        return final_destination;
      },
      [this, &chain_allocator](RefCountedPtr<CallDestination> final_destination)
          -> absl::StatusOr<RefCountedPtr<UnstartedCallDestination>> {
        return MakeRefCounted<interception_chain_detail::CallStarter>(
            chain_allocator.MakeFilterStack(building_filters_),
            std::move(final_destination));
      });
  if (!terminator.ok()) return terminator.status();

  return MakeRefCounted<InterceptionChain>(
      chain_allocator.LinkDestinations(std::move(terminator.value())),
      chain_allocator.TakeDestructors(), chain_allocator.TakeChainData());
}

InterceptionChain::InterceptionChain(
    RefCountedPtr<UnstartedCallDestination> first_destination,
    std::vector<interception_chain_detail::Destructor> destructors,
    void* chain_data)
    : first_destination_(std::move(first_destination)),
      destructors_(std::move(destructors)),
      chain_data_(chain_data) {}

void InterceptionChain::Orphan() { first_destination_.reset(); }

}  // namespace grpc_core
