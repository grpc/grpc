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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_stack_builder.h"

#include <algorithm>

#include "src/core/lib/channel/channel_args.h"

namespace grpc_core {

ChannelStackBuilder::ChannelStackBuilder(const char* name,
                                         grpc_channel_stack_type type,
                                         const ChannelArgs& channel_args)
    : name_(name), type_(type), args_(channel_args) {}

ChannelStackBuilder& ChannelStackBuilder::SetTarget(const char* target) {
  if (target == nullptr) {
    target_ = unknown_target();
  } else {
    target_ = target;
  }
  return *this;
}

void ChannelStackBuilder::PrependFilter(const grpc_channel_filter* filter) {
  stack_.insert(stack_.begin(), filter);
}

void ChannelStackBuilder::AppendFilter(const grpc_channel_filter* filter) {
  stack_.push_back(filter);
}

}  // namespace grpc_core
