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

#include "src/core/lib/transport/call_filters.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

CallInitiator Interceptor::HijackedCall::MakeCall() {
  auto metadata = Arena::MakePooled<ClientMetadata>(call_handler_.arena());
  *metadata = metadata_->Copy();
  return MakeCallWithMetadata(std::move(metadata));
}

CallInitiator Interceptor::HijackedCall::MakeCallWithMetadata(
    ClientMetadataHandle metadata) {
  auto call = MakeCall(std::move(metadata), call_handler_.event_engine(),
                       call_handler_.arena());
  destination_->StartCall(std::move(call.unstarted_handler));
  return std::move(call.initiator);
}

absl::StatusOr<RefCountedPtr<InterceptionChain>>
InterceptionChain::Builder::Build(const ChannelArgs& args) {
  if (!building_filters_.empty()) {
    return absl::InternalError("Last filter must be a hijacker");
  }

  // Build the chain memory layout.
  // We allocate a single block, and place each interceptor along with its
  // filters in the block.
  std::vector<Destructor> destructors;
  std::vector<FilterDef*> filters;
  size_t chain_size = 0;
  size_t chain_alignment = 1;
  auto append_to_chain = [&](Footprint& footprint) {
    if (chain_size % footprint.alignment != 0) {
      chain_size += footprint.alignment - (chain_size % footprint.alignment);
    }
    footprint.offset = chain_size;
    chain_size += footprint.size;
    chain_alignment = std::max(chain_alignment, footprint.alignment);
    if (footprint.destroy != nullptr) {
      destructors.push_back(Destructor{footprint.destroy, footprint.offset});
    }
  };
  for (auto& interceptor : interceptors_) {
    append_to_chain(interceptor.footprint);
    for (auto& filter : interceptor.filters) {
      filters.push_back(&filter);
      append_to_chain(filter.footprint);
    }
  }

  // Allocate the chain.
  void* chain_memory = gpr_malloc_aligned(chain_size, chain_alignment);

  // Instantiate the filters.
  for (size_t i = 0; i < filters.size(); ++i) {
    auto& filter = *filters[i];
    auto status = filter.init(filter.footprint.OffsetPtr(chain_memory), args);
    if (!status.ok()) {
      for (size_t j = 0; j < i; ++j) {
        filters[j]->footprint.destroy(
            filters[j]->footprint.OffsetPtr(chain_memory));
      }
      gpr_free_aligned(chain_memory);
      return status;
    }
  }

  // Instantiate the interceptors.
  std::vector<Interceptor*> interceptors;
  interceptors.reserve(interceptors_.size());
  for (size_t i = 0; i < interceptors_.size(); ++i) {
    auto& interceptor = interceptors_[i];
    auto* interceptor_instance = interceptor.footprint.OffsetPtr(chain_memory);
    auto status = interceptor.init(interceptor_instance, args);
    if (!status.ok()) {
      for (size_t j = 0; j < i; ++j) {
        interceptors[j]->~Interceptor();
      }
      for (size_t j = 0; j < filters.size(); ++j) {
        filters[j]->footprint.destroy(
            filters[j]->footprint.OffsetPtr(chain_memory));
      }
      gpr_free_aligned(chain_memory);
      return status.status();
    }
    interceptors.push_back(status.value());
  }

  // Create filter stacks for each interceptor.
  for (size_t building = 0; building < interceptors_.size(); ++building) {
    CallFilters::StackBuilder stack_builder;
    for (auto& filter : interceptors_[building].filters) {
      filter.add_to_stack_builder(stack_builder,
                                  filter.footprint.OffsetPtr(chain_memory));
    }
    interceptors[building]->filter_stack_ = stack_builder.Build();
  }

  // Create the chain.
  auto chain = MakeRefCounted<Chain>(
      interceptors.empty() ? nullptr : interceptors.back(),
      std::move(destructors), chain_memory, std::move(final_destination_));

  // Fill in the rest of the interceptor data.
  for (size_t building = 0; building < interceptors_.size(); ++building) {
    if (building + 1 == interceptors_.size()) {
      interceptors[building]->wrapped_destination_ =
          chain->final_destination.get();
    } else {
      interceptors[building]->wrapped_destination_ = interceptors[building + 1];
    }
  }

  return MakeRefCounted<InterceptionChain>(std::move(chain));
}

InterceptionChain::Chain::~Chain() {
  uint8_t* p = static_cast<uint8_t*>(chain_data);
  for (auto& destructor : destructors) {
    destructor.destroy(p + destructor.offset);
  }
  gpr_free_aligned(chain_data);
}

}  // namespace grpc_core
