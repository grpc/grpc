//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_SRC_CORE_SERVER_SERVER_CONFIG_SELECTOR_FILTER_H
#define GRPC_SRC_CORE_SERVER_SERVER_CONFIG_SELECTOR_FILTER_H

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"

namespace grpc_core {

extern const grpc_channel_filter kServerConfigSelectorFilter;

// This filter handles injection of dynamic filters for server connections.
//
// Normally (when dynamic filters are not used), the server creates a
// single filter stack of type GRPC_SERVER_CHANNEL that contains all
// filters needed for each connection.  However, when using dynamic
// filters, the filters are split into 3 stacks:
//
// - The top filter stack, of type GRPC_SERVER_TOP_CHANNEL.  This filter
//   is the final filter in that stack.
//
// - The dynamic filter stack, which is dynamically configured.
//
// - The bottom filter stack, of type GRPC_SERVER_CHANNEL, which will
//   always have the GRPC_ARG_BELOW_DYNAMIC_FILTERS channel arg set.
//
// Note that we use the same filter stack type for both the single stack
// when dynamic filters are not used and for the bottom stack when
// dynamic filters are used.  Filters that need to run above dynamic
// filters are registered twice: once in the GRPC_SERVER_CHANNEL stack
// with a condition that the GRPC_ARG_BELOW_DYNAMIC_FILTERS arg must not
// be set, and once in the GRPC_SERVER_TOP_CHANNEL stack with no such
// condition.  This ensures that they are in the right place in both modes.
//
// The job of this filter is to use the ServerConfigSelector to choose
// the right dynamic filter stack for each RPC and run the RPC through
// that filter stack before sending it on to the bottom filter stack.
//
// This filter will get the ServerConfigSelectorProvider from channel
// args and start a watch on it.  Whenever the watcher delivers a new
// ServerConfigSelector, the filter will ask the ServerConfigSelector to
// build a filter stack for each dynamic filter chain.  The final filter
// in each dynamic filter stack will forward to the bottom filter stack.
// Then it swaps the new ServerConfigSelector into place so that it will
// be used to choose which dynamic filter stack to use for each RPC.
class ServerConfigSelectorFilterV1 {
 public:
  static const grpc_channel_filter kFilterVtable;

  explicit ServerConfigSelectorFilterV1(const ChannelArgs& args);

  void StartTransportStreamOpBatch(grpc_transport_stream_op_batch* batch);

  void StartTransportOp(grpc_transport_op* op);

 private:
  // The final filter in each dynamic filter stack.  Used to propagate
  // RPCs to the bottom filter stack.
  class ServerDynamicTerminationFilter;

  // A call on the bottom filter stack.
  // Implements the same interface as RefCounted<>.
  class BottomCall {
   public:
    static RefCountedPtr<BottomCall> Create(Arena* arena);

    // Don't instantiate directly; use Create() instead.
    explicit BottomCall(RefCountedPtr<grpc_channel_stack> bottom_stack)
        : bottom_stack_(std::move(bottom_stack)) {}

    void StartTransportStreamOpBatch(grpc_transport_stream_op_batch* batch);

    // Interface of RefCounted<>.
    GRPC_MUST_USE_RESULT RefCountedPtr<BottomCall> Ref();
    GRPC_MUST_USE_RESULT RefCountedPtr<BottomCall> Ref(
        const DebugLocation& location, const char* reason);
    // When refcount drops to 0, destroys itself and the associated call stack,
    // but does NOT free the memory because it's in the call arena.
    void Unref();
    void Unref(const DebugLocation& location, const char* reason);

   private:
    // Allow RefCountedPtr<> to access IncrementRefCount().
    template <typename T>
    friend class RefCountedPtr;

    // Interface of RefCounted<>.
    void IncrementRefCount();
    void IncrementRefCount(const DebugLocation& location, const char* reason);

    grpc_call_stack* call_stack();

// FIXME: is this needed?
    RefCountedPtr<grpc_channel_stack> bottom_stack_;
  };

  // Watcher for ServerConfigSelector.
  class ServerConfigSelectorWatcher
      : public ServerConfigSelectorProvider::ServerConfigSelectorWatcher {
   public:
    explicit ServerConfigSelectorWatcher(ServerConfigSelectorFilterV1* filter)
        : filter_(filter) {
      GRPC_CHANNEL_STACK_REF(filter_->top_stack_);
    }

    ~ServerConfigSelectorWatcher() override {
      GRPC_CHANNEL_STACK_UNREF(filter_->top_stack_);
    }

    void OnServerConfigSelectorUpdate(
        absl::StatusOr<RefCountedPtr<ServerConfigSelector>> config_selector)
        override {
      if (config_selector.ok()) {
        filter_->BuildDynamicFilterChains(**config_selector);
      }
      MutexLock lock(&filter_->mu_);
      filter_->config_selector_ = std::move(config_selector);
    }

   private:
    ServerConfigSelectorFilterV1* filter_;
  };

  // Builds filter chains in a newly delivered ServerConfigSelector
  // before we start to use that ServerConfigSelector for RPCs.
  void BuildDynamicFilterChains(ServerConfigSelector& config_selector);

  // Gets the current ServerConfigSelector to use for an RPC.
  RefCountedPtr<ServerConfigSelector> GetConfigSelector();

  // Constructs a BottomCall object.  Called when each RPC hits the last
  // filter in the dynamic filter stack.
  OrphanablePtr<BottomCall> MakeBottomCall(const grpc_call_element_args& args);

  // Initializes the filter.
  static absl::Status Init(grpc_channel_element* elem,
                           grpc_channel_element_args* args);

  grpc_channel_stack* top_stack_;
  RefCountedPtr<grpc_channel_stack> bottom_stack_;
  const RefCountedPtr<ServerConfigSelectorProvider>
      server_config_selector_provider_;

  // FIXME: use per-CPU sharding here to avoid lock contention
  Mutex mu_;
  std::optional<absl::StatusOr<RefCountedPtr<ServerConfigSelector>>>
      config_selector_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_SERVER_SERVER_CONFIG_SELECTOR_FILTER_H
