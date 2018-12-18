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

// TODO(ncteisen), this only contains the uuids of the children for now,
// since that is all that is strictly needed. In a future enhancement we will
// add human readable names as in the channelz.proto
typedef InlinedVector<intptr_t, 10> ChildRefsList;

class SocketNode;
typedef InlinedVector<SocketNode*, 10> ChildSocketsList;

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

  explicit BaseNode(EntityType type);
  virtual ~BaseNode();

  // All children must implement this function.
  virtual grpc_json* RenderJson() GRPC_ABSTRACT;

  // Renders the json and returns allocated string that must be freed by the
  // caller.
  char* RenderJsonString();

  EntityType type() const { return type_; }
  intptr_t uuid() const { return uuid_; }

 private:
  // to allow the ChannelzRegistry to set uuid_ under its lock.
  friend class ChannelzRegistry;
  const EntityType type_;
  intptr_t uuid_;
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
  static RefCountedPtr<ChannelNode> MakeChannelNode(
      grpc_channel* channel, size_t channel_tracer_max_nodes,
      bool is_top_level_channel);

  ChannelNode(grpc_channel* channel, size_t channel_tracer_max_nodes,
              bool is_top_level_channel);
  ~ChannelNode() override;

  grpc_json* RenderJson() override;

  // template methods. RenderJSON uses these methods to render its JSON
  // representation. These are virtual so that children classes may provide
  // their specific mechanism for populating these parts of the channelz
  // object.
  //
  // ChannelNode does not have a notion of connectivity state or child refs,
  // so it leaves these implementations blank.
  //
  // This is utilizing the template method design pattern.
  //
  // TODO(ncteisen): remove these template methods in favor of manual traversal
  // and mutation of the grpc_json object.
  virtual void PopulateConnectivityState(grpc_json* json) {}
  virtual void PopulateChildRefs(grpc_json* json) {}

  void MarkChannelDestroyed() {
    GPR_ASSERT(channel_ != nullptr);
    channel_ = nullptr;
  }

  bool ChannelIsDestroyed() { return channel_ == nullptr; }

  // proxy methods to composed classes.
  void AddTraceEvent(ChannelTrace::Severity severity, grpc_slice data) {
    trace_.AddTraceEvent(severity, data);
  }
  void AddTraceEventWithReference(ChannelTrace::Severity severity,
                                  grpc_slice data,
                                  RefCountedPtr<BaseNode> referenced_channel) {
    trace_.AddTraceEventWithReference(severity, data,
                                      std::move(referenced_channel));
  }
  void RecordCallStarted() { call_counter_.RecordCallStarted(); }
  void RecordCallFailed() { call_counter_.RecordCallFailed(); }
  void RecordCallSucceeded() { call_counter_.RecordCallSucceeded(); }

 private:
  // to allow the channel trace test to access trace_.
  friend class testing::ChannelNodePeer;
  grpc_channel* channel_ = nullptr;
  UniquePtr<char> target_;
  CallCountingHelper call_counter_;
  ChannelTrace trace_;
};

// Handles channelz bookkeeping for servers
class ServerNode : public BaseNode {
 public:
  ServerNode(grpc_server* server, size_t channel_tracer_max_nodes);
  ~ServerNode() override;

  grpc_json* RenderJson() override;

  char* RenderServerSockets(intptr_t start_socket_id,
                            intptr_t pagination_limit);

  // proxy methods to composed classes.
  void AddTraceEvent(ChannelTrace::Severity severity, grpc_slice data) {
    trace_.AddTraceEvent(severity, data);
  }
  void AddTraceEventWithReference(ChannelTrace::Severity severity,
                                  grpc_slice data,
                                  RefCountedPtr<BaseNode> referenced_channel) {
    trace_.AddTraceEventWithReference(severity, data,
                                      std::move(referenced_channel));
  }
  void RecordCallStarted() { call_counter_.RecordCallStarted(); }
  void RecordCallFailed() { call_counter_.RecordCallFailed(); }
  void RecordCallSucceeded() { call_counter_.RecordCallSucceeded(); }

 private:
  grpc_server* server_;
  CallCountingHelper call_counter_;
  ChannelTrace trace_;
};

// Handles channelz bookkeeping for sockets
class SocketNode : public BaseNode {
 public:
  SocketNode(UniquePtr<char> local, UniquePtr<char> remote);
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
  // ListenSocketNode takes ownership of host.
  explicit ListenSocketNode(UniquePtr<char> local_addr);
  ~ListenSocketNode() override {}

  grpc_json* RenderJson() override;

 private:
  UniquePtr<char> local_addr_;
};

// Creation functions

typedef RefCountedPtr<ChannelNode> (*ChannelNodeCreationFunc)(grpc_channel*,
                                                              size_t, bool);

}  // namespace channelz
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNELZ_H */
