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

// Channel arg key to signal that the channel is an internal channel.
#define GRPC_ARG_CHANNELZ_CHANNEL_IS_INTERNAL_CHANNEL \
  "grpc.channelz_channel_is_internal_channel"

namespace grpc_core {
namespace channelz {

namespace testing {
class ChannelNodePeer;
}

// base class for all channelz entities
class BaseNode : public RefCounted<BaseNode> {
 public:
  BaseNode() {}
  virtual ~BaseNode() {}

 private:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW
};

// Handles channelz bookkeeping for sockets
// TODO(ncteisen): implement in subsequent PR.
class SocketNode : public BaseNode {
 public:
  SocketNode() : BaseNode() {}
  ~SocketNode() override {}

 private:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW
};

// This class is the parent for the channelz entities that deal with Channels
// Subchannels, and Servers, since those have similar proto definitions.
// This class has the ability to:
//   - track calls_{started,succeeded,failed}
//   - track last_call_started_timestamp
//   - hold the channel trace.
//   - perform common rendering.
//
// This class also defines some fat interfaces so that its children can
// implement the functionality differently. For example, querying the
// connectivity state looks different for channels than for subchannels, and
// does not make sense for servers. So servers will not override, and channels
// and subchannels will override with their own way to query connectivity state.
class CallCountingBase : public BaseNode {
 public:
  CallCountingBase(size_t channel_tracer_max_nodes);
  ~CallCountingBase() override;

  void RecordCallStarted();
  void RecordCallFailed() {
    gpr_atm_no_barrier_fetch_add(&calls_failed_, (gpr_atm(1)));
  }
  void RecordCallSucceeded() {
    gpr_atm_no_barrier_fetch_add(&calls_succeeded_, (gpr_atm(1)));
  }
  ChannelTrace* trace() { return trace_.get(); }

  // Fat interface for ConnectivityState. Default is to leave it out, however,
  // things like Channel and Subchannel will override with their mechanism
  // for querying connectivity state.
  virtual void PopulateConnectivityState(grpc_json* json) {}

  // Fat interface for Targets.
  virtual void PopulateTarget(grpc_json* json) {}

  // Fat interface for ChildRefs. Allows children to populate with whatever
  // combination of child_refs, subchannel_refs, and socket_refs is correct.
  virtual void PopulateChildRefs(grpc_json* json) {}

  // All children must implement their custom JSON rendering.
  virtual grpc_json* RenderJson() GRPC_ABSTRACT;

  // Common rendering of the channel trace.
  void PopulateTrace(grpc_json* json);

  // Common rendering of the call count data and last_call_started_timestamp.
  void PopulateCallData(grpc_json* json);

  // Common rendering of grpc_json from RenderJson() to allocated string.
  char* RenderJsonString();

 private:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW

  gpr_atm calls_started_ = 0;
  gpr_atm calls_succeeded_ = 0;
  gpr_atm calls_failed_ = 0;
  gpr_atm last_call_started_millis_ = 0;
  ManualConstructor<ChannelTrace> trace_;
};

// Handles channelz bookkeeping for servers
// TODO(ncteisen): implement in subsequent PR.
class ServerNode : public CallCountingBase {
 public:
  ServerNode(size_t channel_tracer_max_nodes)
      : CallCountingBase(channel_tracer_max_nodes) {}
  ~ServerNode() override {}

 private:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW
};

// Overrides Channel specific functionality.
class ChannelNode : public CallCountingBase {
 public:
  static RefCountedPtr<ChannelNode> MakeChannelNode(
      grpc_channel* channel, size_t channel_tracer_max_nodes,
      bool is_top_level_channel);

  void MarkChannelDestroyed() {
    GPR_ASSERT(channel_ != nullptr);
    channel_ = nullptr;
  }

  grpc_json* RenderJson() override;

  void PopulateTarget(grpc_json* json) override;

  bool ChannelIsDestroyed() { return channel_ == nullptr; }

  intptr_t channel_uuid() { return channel_uuid_; }
  bool is_top_level_channel() { return is_top_level_channel_; }

 protected:
  ChannelNode(grpc_channel* channel, size_t channel_tracer_max_nodes,
              bool is_top_level_channel);
  ~ChannelNode() override;

 private:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW

  // testing peer friend.
  friend class testing::ChannelNodePeer;

  grpc_channel* channel_ = nullptr;
  UniquePtr<char> target_;
  intptr_t channel_uuid_;
  bool is_top_level_channel_ = true;
};

// Overrides Subchannel specific functionality.
class SubchannelNode : public CallCountingBase {
 public:
  SubchannelNode(size_t channel_tracer_max_nodes);
  ~SubchannelNode() override;
  grpc_json* RenderJson() override;
  intptr_t subchannel_uuid() { return subchannel_uuid_; }

 private:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW

  intptr_t subchannel_uuid_;
};

// Creation functions

typedef RefCountedPtr<ChannelNode> (*ChannelNodeCreationFunc)(grpc_channel*,
                                                              size_t, bool);

}  // namespace channelz
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNELZ_H */
