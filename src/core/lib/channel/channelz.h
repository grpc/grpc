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
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"

// Channel arg key for channelz node.
#define GRPC_ARG_CHANNELZ_CHANNEL_NODE "grpc.channelz_channel_node"

// Channel arg key to encode the channelz uuid of the channel's parent.
#define GRPC_ARG_CHANNELZ_PARENT_UUID "grpc.channelz_parent_uuid"

/** This is the default value for whether or not to enable channelz. If
 * GRPC_ARG_ENABLE_CHANNELZ is set, it will override this default value. */
#define GRPC_ENABLE_CHANNELZ_DEFAULT true

/** This is the default value for the maximum amount of memory used by trace
 * events per channel trace node. If
 * GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE is set, it will override
 * this default value. */
#define GRPC_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE_DEFAULT 1024 * 4

namespace grpc_core {

namespace channelz {

// Helpers for getting and setting GRPC_ARG_CHANNELZ_PARENT_UUID.
grpc_arg MakeParentUuidArg(intptr_t parent_uuid);
intptr_t GetParentUuidFromArgs(const grpc_channel_args& args);

class SocketNode;
class ListenSocketNode;

namespace testing {
class CallCountingHelperPeer;
class ChannelNodePeer;
}  // namespace testing

// base class for all channelz entities
class BaseNode : public RefCounted<BaseNode> {
 public:
  // There are only four high level channelz entities. However, to support
  // GetTopChannelsRequest, we split the Channel entity into two different
  // types. All children of BaseNode must be one of these types.
  enum class EntityType {
    kTopLevelChannel,
    kInternalChannel,
    kSubchannel,
    kServer,
    kSocket,
  };

  BaseNode(EntityType type, UniquePtr<char> name);
  virtual ~BaseNode();

  // All children must implement this function.
  virtual grpc_json* RenderJson() GRPC_ABSTRACT;

  // Renders the json and returns allocated string that must be freed by the
  // caller.
  char* RenderJsonString();

  EntityType type() const { return type_; }
  intptr_t uuid() const { return uuid_; }
  const char* name() const { return name_.get(); }

 private:
  // to allow the ChannelzRegistry to set uuid_ under its lock.
  friend class ChannelzRegistry;
  const EntityType type_;
  intptr_t uuid_;
  UniquePtr<char> name_;
};

// This class is a helper class for channelz entities that deal with Channels,
// Subchannels, and Servers, since those have similar proto definitions.
// This class has the ability to:
//   - track calls_{started,succeeded,failed}
//   - track last_call_started_timestamp
//   - perform rendering of the above items
class CallCountingHelper {
 public:
  CallCountingHelper();
  ~CallCountingHelper();

  void RecordCallStarted();
  void RecordCallFailed();
  void RecordCallSucceeded();

  // Common rendering of the call count data and last_call_started_timestamp.
  void PopulateCallCounts(grpc_json* json);

 private:
  // testing peer friend.
  friend class testing::CallCountingHelperPeer;

  struct AtomicCounterData {
    gpr_atm calls_started = 0;
    gpr_atm calls_succeeded = 0;
    gpr_atm calls_failed = 0;
    gpr_atm last_call_started_millis = 0;
  };

  struct CounterData {
    intptr_t calls_started = 0;
    intptr_t calls_succeeded = 0;
    intptr_t calls_failed = 0;
    intptr_t last_call_started_millis = 0;
  };

  // collects the sharded data into one CounterData struct.
  void CollectData(CounterData* out);

  AtomicCounterData* per_cpu_counter_data_storage_ = nullptr;
  size_t num_cores_ = 0;
};

// Handles channelz bookkeeping for channels
class ChannelNode : public BaseNode {
 public:
  ChannelNode(UniquePtr<char> target, size_t channel_tracer_max_nodes,
              intptr_t parent_uuid);

  // Returns the string description of the given connectivity state.
  static const char* GetChannelConnectivityStateChangeString(
      grpc_connectivity_state state);

  intptr_t parent_uuid() const { return parent_uuid_; }

  grpc_json* RenderJson() override;

  // proxy methods to composed classes.
  void AddTraceEvent(ChannelTrace::Severity severity, const grpc_slice& data) {
    trace_.AddTraceEvent(severity, data);
  }
  void AddTraceEventWithReference(ChannelTrace::Severity severity,
                                  const grpc_slice& data,
                                  RefCountedPtr<BaseNode> referenced_channel) {
    trace_.AddTraceEventWithReference(severity, data,
                                      std::move(referenced_channel));
  }
  void RecordCallStarted() { call_counter_.RecordCallStarted(); }
  void RecordCallFailed() { call_counter_.RecordCallFailed(); }
  void RecordCallSucceeded() { call_counter_.RecordCallSucceeded(); }

  void SetConnectivityState(grpc_connectivity_state state);

  // TODO(roth): take in a RefCountedPtr to the child channel so we can retrieve
  // the human-readable name.
  void AddChildChannel(intptr_t child_uuid);
  void RemoveChildChannel(intptr_t child_uuid);

  // TODO(roth): take in a RefCountedPtr to the child subchannel so we can
  // retrieve the human-readable name.
  void AddChildSubchannel(intptr_t child_uuid);
  void RemoveChildSubchannel(intptr_t child_uuid);

 private:
  void PopulateChildRefs(grpc_json* json);

  // to allow the channel trace test to access trace_.
  friend class testing::ChannelNodePeer;

  UniquePtr<char> target_;
  CallCountingHelper call_counter_;
  ChannelTrace trace_;
  const intptr_t parent_uuid_;

  // Least significant bit indicates whether the value is set.  Remaining
  // bits are a grpc_connectivity_state value.
  Atomic<int> connectivity_state_{0};

  Mutex child_mu_;  // Guards child maps below.
  // TODO(roth): We don't actually use the values here, only the keys, so
  // these should be sets instead of maps, but we don't currently have a set
  // implementation.  Change this if/when we have one.
  Map<intptr_t, bool> child_channels_;
  Map<intptr_t, bool> child_subchannels_;
};

// Handles channelz bookkeeping for servers
class ServerNode : public BaseNode {
 public:
  ServerNode(grpc_server* server, size_t channel_tracer_max_nodes);

  ~ServerNode() override;

  grpc_json* RenderJson() override;

  char* RenderServerSockets(intptr_t start_socket_id, intptr_t max_results);

  void AddChildSocket(RefCountedPtr<SocketNode> node);

  void RemoveChildSocket(intptr_t child_uuid);

  void AddChildListenSocket(RefCountedPtr<ListenSocketNode> node);

  void RemoveChildListenSocket(intptr_t child_uuid);

  // proxy methods to composed classes.
  void AddTraceEvent(ChannelTrace::Severity severity, const grpc_slice& data) {
    trace_.AddTraceEvent(severity, data);
  }
  void AddTraceEventWithReference(ChannelTrace::Severity severity,
                                  const grpc_slice& data,
                                  RefCountedPtr<BaseNode> referenced_channel) {
    trace_.AddTraceEventWithReference(severity, data,
                                      std::move(referenced_channel));
  }
  void RecordCallStarted() { call_counter_.RecordCallStarted(); }
  void RecordCallFailed() { call_counter_.RecordCallFailed(); }
  void RecordCallSucceeded() { call_counter_.RecordCallSucceeded(); }

 private:
  CallCountingHelper call_counter_;
  ChannelTrace trace_;
  Mutex child_mu_;  // Guards child maps below.
  Map<intptr_t, RefCountedPtr<SocketNode>> child_sockets_;
  Map<intptr_t, RefCountedPtr<ListenSocketNode>> child_listen_sockets_;
};

// Handles channelz bookkeeping for sockets
class SocketNode : public BaseNode {
 public:
  SocketNode(UniquePtr<char> local, UniquePtr<char> remote,
             UniquePtr<char> name);
  ~SocketNode() override {}

  grpc_json* RenderJson() override;

  void RecordStreamStartedFromLocal();
  void RecordStreamStartedFromRemote();
  void RecordStreamSucceeded() {
    gpr_atm_no_barrier_fetch_add(&streams_succeeded_, static_cast<gpr_atm>(1));
  }
  void RecordStreamFailed() {
    gpr_atm_no_barrier_fetch_add(&streams_failed_, static_cast<gpr_atm>(1));
  }
  void RecordMessagesSent(uint32_t num_sent);
  void RecordMessageReceived();
  void RecordKeepaliveSent() {
    gpr_atm_no_barrier_fetch_add(&keepalives_sent_, static_cast<gpr_atm>(1));
  }

  const char* remote() { return remote_.get(); }

 private:
  gpr_atm streams_started_ = 0;
  gpr_atm streams_succeeded_ = 0;
  gpr_atm streams_failed_ = 0;
  gpr_atm messages_sent_ = 0;
  gpr_atm messages_received_ = 0;
  gpr_atm keepalives_sent_ = 0;
  gpr_atm last_local_stream_created_millis_ = 0;
  gpr_atm last_remote_stream_created_millis_ = 0;
  gpr_atm last_message_sent_millis_ = 0;
  gpr_atm last_message_received_millis_ = 0;
  UniquePtr<char> local_;
  UniquePtr<char> remote_;
};

// Handles channelz bookkeeping for listen sockets
class ListenSocketNode : public BaseNode {
 public:
  ListenSocketNode(UniquePtr<char> local_addr, UniquePtr<char> name);
  ~ListenSocketNode() override {}

  grpc_json* RenderJson() override;

 private:
  UniquePtr<char> local_addr_;
};

}  // namespace channelz
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNELZ_H */
