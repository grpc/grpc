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

#ifndef GRPC_CORE_EXT_XDS_XDS_CHANNEL_STACK_MODIFIER_H
#define GRPC_CORE_EXT_XDS_XDS_CHANNEL_STACK_MODIFIER_H

#include <grpc/support/port_platform.h>

#include <vector>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/gprpp/ref_counted.h"

namespace grpc_core {

// XdsChannelStackModifier allows for inserting xDS HTTP filters into the
// channel stack. It is registered to mutate the `grpc_channel_stack_builder`
// object via ChannelInit::Builder::RegisterStage.
class XdsChannelStackModifier : public RefCounted<XdsChannelStackModifier> {
 public:
  explicit XdsChannelStackModifier(
      std::vector<const grpc_channel_filter*> filters)
      : filters_(std::move(filters)) {}
  // Returns true on success, false otherwise.
  bool ModifyChannelStack(grpc_channel_stack_builder* builder);
  grpc_arg MakeChannelArg() const;
  static RefCountedPtr<XdsChannelStackModifier> GetFromChannelArgs(
      const grpc_channel_args& args);

 private:
  std::vector<const grpc_channel_filter*> filters_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_XDS_XDS_CHANNEL_STACK_MODIFIER_H */
