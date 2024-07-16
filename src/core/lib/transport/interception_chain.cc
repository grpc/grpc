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

#include "src/core/lib/transport/interception_chain.h"

#include <cstddef>

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/transport/call_destination.h"
#include "src/core/lib/transport/call_filters.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

std::atomic<size_t> InterceptionChainBuilder::next_filter_id_{0};

///////////////////////////////////////////////////////////////////////////////
// HijackedCall

CallInitiator HijackedCall::MakeCall() {
  auto metadata = Arena::MakePooledForOverwrite<ClientMetadata>();
  *metadata = metadata_->Copy();
  return MakeCallWithMetadata(std::move(metadata));
}

CallInitiator HijackedCall::MakeCallWithMetadata(
    ClientMetadataHandle metadata) {
  auto call = MakeCallPair(std::move(metadata), call_handler_.arena()->Ref());
  destination_->StartCall(std::move(call.handler));
  return std::move(call.initiator);
}

namespace {
class CallStarter final : public UnstartedCallDestination {
 public:
  CallStarter(RefCountedPtr<CallFilters::Stack> stack,
              RefCountedPtr<CallDestination> destination)
      : stack_(std::move(stack)), destination_(std::move(destination)) {}

  void Orphaned() override {
    stack_.reset();
    destination_.reset();
  }

  void StartCall(UnstartedCallHandler unstarted_call_handler) override {
    unstarted_call_handler.AddCallStack(stack_);
    destination_->HandleCall(unstarted_call_handler.StartCall());
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

  void Orphaned() override {
    stack_.reset();
    destination_.reset();
  }

  void StartCall(UnstartedCallHandler unstarted_call_handler) override {
    unstarted_call_handler.AddCallStack(stack_);
    destination_->StartCall(unstarted_call_handler);
  }

 private:
  RefCountedPtr<CallFilters::Stack> stack_;
  RefCountedPtr<UnstartedCallDestination> destination_;
};
}  // namespace

///////////////////////////////////////////////////////////////////////////////
// InterceptionChain::Builder

void InterceptionChainBuilder::AddInterceptor(
    absl::StatusOr<RefCountedPtr<Interceptor>> interceptor) {
  if (!status_.ok()) return;
  if (!interceptor.ok()) {
    status_ = interceptor.status();
    return;
  }
  (*interceptor)->filter_stack_ = MakeFilterStack();
  if (top_interceptor_ == nullptr) {
    top_interceptor_ = std::move(*interceptor);
  } else {
    Interceptor* previous = top_interceptor_.get();
    while (previous->wrapped_destination_ != nullptr) {
      previous = DownCast<Interceptor*>(previous->wrapped_destination_.get());
    }
    previous->wrapped_destination_ = std::move(*interceptor);
  }
}

absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>
InterceptionChainBuilder::Build(FinalDestination final_destination) {
  if (!status_.ok()) return status_;
  // Build the final UnstartedCallDestination in the chain - what we do here
  // depends on both the type of the final destination and the filters we have
  // that haven't been captured into an Interceptor yet.
  RefCountedPtr<UnstartedCallDestination> terminator = Match(
      final_destination,
      [this](RefCountedPtr<UnstartedCallDestination> final_destination)
          -> RefCountedPtr<UnstartedCallDestination> {
        if (stack_builder_.has_value()) {
          return MakeRefCounted<TerminalInterceptor>(MakeFilterStack(),
                                                     final_destination);
        }
        return final_destination;
      },
      [this](RefCountedPtr<CallDestination> final_destination)
          -> RefCountedPtr<UnstartedCallDestination> {
        return MakeRefCounted<CallStarter>(MakeFilterStack(),
                                           std::move(final_destination));
      });
  // Now append the terminator to the interceptor chain.
  if (top_interceptor_ == nullptr) {
    return std::move(terminator);
  }
  Interceptor* previous = top_interceptor_.get();
  while (previous->wrapped_destination_ != nullptr) {
    previous = DownCast<Interceptor*>(previous->wrapped_destination_.get());
  }
  previous->wrapped_destination_ = std::move(terminator);
  return std::move(top_interceptor_);
}

}  // namespace grpc_core
