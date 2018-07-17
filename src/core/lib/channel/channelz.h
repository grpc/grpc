/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNELZ_H
#define GRPC_CORE_LIB_CHANNEL_CHANNELZ_H

#include <grpc/impl/codegen/port_platform.h>

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"

// Channel arg key for client channel factory.
#define GRPC_ARG_CHANNELZ_CHANNEL_NODE_CREATION_FUNC \
  "grpc.channelz_channel_node_creation_func"

namespace grpc_core {
namespace channelz {

namespace testing {
class ChannelNodePeer;
}

class ChannelNode : public RefCounted<ChannelNode> {
 public:
  static RefCountedPtr<ChannelNode> MakeChannelNode(
      grpc_channel* channel, size_t channel_tracer_max_nodes);

  void RecordCallStarted();
  void RecordCallFailed() {
    gpr_atm_no_barrier_fetch_add(&calls_failed_, (gpr_atm(1)));
  }
  void RecordCallSucceeded() {
    gpr_atm_no_barrier_fetch_add(&calls_succeeded_, (gpr_atm(1)));
  }

  char* RenderJSON();

  // helper for getting and populating connectivity state. It is virtual
  // because it allows the client_channel specific code to live in ext/
  // instead of lib/
  virtual void PopulateConnectivityState(grpc_json* json);

  virtual void PopulateChildRefs(grpc_json* json);

  ChannelTrace* trace() { return trace_.get(); }

  void MarkChannelDestroyed() {
    GPR_ASSERT(channel_ != nullptr);
    channel_ = nullptr;
  }

  bool ChannelIsDestroyed() { return channel_ == nullptr; }

  intptr_t channel_uuid() { return channel_uuid_; }

 protected:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW
  ChannelNode(grpc_channel* channel, size_t channel_tracer_max_nodes);
  virtual ~ChannelNode();

 private:
  // testing peer friend.
  friend class testing::ChannelNodePeer;

  grpc_channel* channel_ = nullptr;
  UniquePtr<char> target_;
  gpr_atm calls_started_ = 0;
  gpr_atm calls_succeeded_ = 0;
  gpr_atm calls_failed_ = 0;
  gpr_atm last_call_started_millis_ = 0;
  intptr_t channel_uuid_;
  ManualConstructor<ChannelTrace> trace_;
};

// Placeholds channelz class for subchannels. All this can do now is track its
// uuid (this information is needed by the parent channelz class).
// TODO(ncteisen): build this out to support the GetSubchannel channelz request.
class SubchannelNode : public RefCounted<SubchannelNode> {
 public:
  SubchannelNode();
  virtual ~SubchannelNode();

  intptr_t subchannel_uuid() { return subchannel_uuid_; }

 protected:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW

 private:
  intptr_t subchannel_uuid_;
};

// Creation functions

typedef RefCountedPtr<ChannelNode> (*ChannelNodeCreationFunc)(grpc_channel*,
                                                              size_t);

}  // namespace channelz
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNELZ_H */
