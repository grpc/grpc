//
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
//

#ifndef GRPC_SRC_CORE_SERVER_SERVER_CONFIG_SELECTOR_FILTER_H
#define GRPC_SRC_CORE_SERVER_SERVER_CONFIG_SELECTOR_FILTER_H

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/observable.h"
#include "src/core/server/server_config_selector.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

extern const grpc_channel_filter kServerConfigSelectorFilter;

// This filter handles injection of dynamic filters for server connections.
//
// This filter will get the ServerConfigSelectorProvider from channel
// args and start a watch on it.  Whenever the watcher delivers a new
// ServerConfigSelector, the filter will ask the ServerConfigSelector to
// build a filter stack for each dynamic filter chain.  Then it swaps the
// new ServerConfigSelector into place so that it will be used to choose
// which dynamic filter stack to use for each RPC.
//
// This is implemented as a v3 interceptor using V3InterceptorToV2Bridge
// to run in v1 stacks.  However, it runs the dynamic filters as native
// v3 filters (i.e., it creates a v3 interceptor chain for them), so they
// cannot be implemented as v1 filters.  The call destination at the end
// of each v3 interceptor chain is the wrapped call destination (i.e.,
// the next filter in the server stack).
class ServerConfigSelectorInterceptor final
    : public V3InterceptorToV2Bridge<ServerConfigSelectorInterceptor> {
 public:
  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "server_config_selector"; }

  static absl::StatusOr<RefCountedPtr<ServerConfigSelectorInterceptor>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  ServerConfigSelectorInterceptor(
      const ChannelArgs& args, ChannelFilter::Args filter_args,
      RefCountedPtr<ServerConfigSelectorProvider>
      server_config_selector_provider);

 private:
  class Watcher;

  void Orphaned() override {}

  // Builds filter chains in a newly delivered ServerConfigSelector
  // before we start to use that ServerConfigSelector for RPCs.
  void BuildDynamicFilterChains(ServerConfigSelector& config_selector);

  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override;

  const ChannelArgs args_;
  const RefCountedPtr<ServerConfigSelectorProvider>
      server_config_selector_provider_;

  Observable<absl::StatusOr<RefCountedPtr<ServerConfigSelector>>>
      config_selector_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_SERVER_SERVER_CONFIG_SELECTOR_FILTER_H
