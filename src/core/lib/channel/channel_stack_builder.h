/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_BUILDER_H
#define GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_BUILDER_H

#include <grpc/support/port_platform.h>

#include <stdbool.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"

namespace grpc_core {
class ChannelStackBuilder {
 public:
  using PostInitFunc = std::function<void(grpc_channel_stack* channel_stack,
                                          grpc_channel_element* elem)>;

  explicit ChannelStackBuilder(std::string name) : name_(std::move(name)) {}

  ~ChannelStackBuilder();

  ChannelStackBuilder& SetTarget(const char* target);
  absl::string_view target() const { return target_; }

  ChannelStackBuilder& SetTransport(grpc_transport* transport) {
    GPR_ASSERT(transport_ == nullptr);
    transport_ = transport;
    return *this;
  }

  grpc_transport* transport() const { return transport_; }

  ChannelStackBuilder& SetChannelArgs(const grpc_channel_args* args);
  const grpc_channel_args* channel_args() const { return args_; }

  struct StackEntry {
    const grpc_channel_filter* filter;
    PostInitFunc post_init;
  };

  std::vector<StackEntry>* mutable_stack() { return &stack_; }

  void PrependFilter(const grpc_channel_filter* filter,
                     PostInitFunc post_init) {
    stack_.insert(stack_.begin(), {filter, std::move(post_init)});
  }

  void AppendFilter(const grpc_channel_filter* filter, PostInitFunc post_init) {
    stack_.push_back({filter, std::move(post_init)});
  }

  grpc_error_handle Build(size_t prefix_bytes, int initial_refs,
                          grpc_iomgr_cb_func destroy, void* destroy_arg,
                          void** result);

 private:
  const std::string name_;
  std::string target_;
  grpc_transport* transport_ = nullptr;
  const grpc_channel_args* args_ = nullptr;
  std::vector<StackEntry> stack_;
};
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_STACK_BUILDER_H */
