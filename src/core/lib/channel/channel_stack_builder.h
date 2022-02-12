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

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_BUILDER_H
#define GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_BUILDER_H

#include <grpc/support/port_platform.h>

#include <stdbool.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"

namespace grpc_core {

// Build a channel stack.
// Allows interested parties to add filters to the stack, and to query an
// in-progress build.
// Carries some useful context for the channel stack, such as a target string
// and a transport.
class ChannelStackBuilder {
 public:
  // A function that will be called after the channel stack is successfully
  // built.
  using PostInitFunc = std::function<void(grpc_channel_stack* channel_stack,
                                          grpc_channel_element* elem)>;

  // One filter in the currently building stack.
  struct StackEntry {
    const grpc_channel_filter* filter;
    PostInitFunc post_init;
  };

  // Initialize with a name.
  explicit ChannelStackBuilder(const char* name) : name_(name) {}

  ~ChannelStackBuilder();

  // Set the target string.
  ChannelStackBuilder& SetTarget(const char* target);

  // Query the target.
  absl::string_view target() const { return target_; }

  // Set the transport.
  ChannelStackBuilder& SetTransport(grpc_transport* transport) {
    GPR_ASSERT(transport_ == nullptr);
    transport_ = transport;
    return *this;
  }

  // Query the transport.
  grpc_transport* transport() const { return transport_; }

  // Set channel args (takes a copy of them).
  ChannelStackBuilder& SetChannelArgs(const grpc_channel_args* args);

  // Query the channel args.
  const grpc_channel_args* channel_args() const { return args_; }

  // Mutable vector of proposed stack entries.
  std::vector<StackEntry>* mutable_stack() { return &stack_; }

  // Helper to add a filter to the front of the stack.
  void PrependFilter(const grpc_channel_filter* filter, PostInitFunc post_init);

  // Helper to add a filter to the end of the stack.
  void AppendFilter(const grpc_channel_filter* filter, PostInitFunc post_init);

  // Build the channel stack.
  // After success, *result holds the new channel stack,
  // prefix_bytes are allocated before the channel stack,
  // initial_refs, destroy, destroy_arg are as per grpc_channel_stack_init
  // On failure, *result is nullptr.
  grpc_error_handle Build(size_t prefix_bytes, int initial_refs,
                          grpc_iomgr_cb_func destroy, void* destroy_arg,
                          void** result);

 private:
  static std::string unknown_target() { return "unknown"; }

  // The name of the stack
  const char* const name_;
  // The target
  std::string target_{unknown_target()};
  // The transport
  grpc_transport* transport_ = nullptr;
  // Channel args
  const grpc_channel_args* args_ = nullptr;
  // The in-progress stack
  std::vector<StackEntry> stack_;
};
}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_BUILDER_H
