//
//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_CLIENT_CHANNEL_CHANNELZ_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_CLIENT_CHANNEL_CHANNELZ_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <atomic>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"

#include <grpc/impl/connectivity_state.h>
#include <grpc/slice.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {
namespace channelz {

class SubchannelNode : public BaseNode {
 public:
  SubchannelNode(std::string target_address, size_t channel_tracer_max_nodes);
  ~SubchannelNode() override;

  // Sets the subchannel's connectivity state without health checking.
  void UpdateConnectivityState(grpc_connectivity_state state);

  // Used when the subchannel's child socket changes. This should be set when
  // the subchannel's transport is created and set to nullptr when the
  // subchannel unrefs the transport.
  void SetChildSocket(RefCountedPtr<SocketNode> socket);

  Json RenderJson() override;

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
  std::atomic<grpc_connectivity_state> connectivity_state_{GRPC_CHANNEL_IDLE};
  Mutex socket_mu_;
  RefCountedPtr<SocketNode> child_socket_ ABSL_GUARDED_BY(socket_mu_);
  std::string target_;
  CallCountingHelper call_counter_;
  ChannelTrace trace_;
};

}  // namespace channelz
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_CLIENT_CHANNEL_CHANNELZ_H
