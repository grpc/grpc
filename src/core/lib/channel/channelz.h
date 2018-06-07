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

namespace grpc_core {
namespace channelz {

namespace testing {
  class ChannelPeer;
}

class Channel : public RefCounted<Channel> {
 public:
  Channel(grpc_channel* channel, size_t channel_tracer_max_nodes);
  ~Channel();

  void RecordCallStarted();
  void RecordCallFailed() {
    gpr_atm_no_barrier_fetch_add(&calls_failed_, (gpr_atm(1)));
  }
  void RecordCallSucceeded() {
    gpr_atm_no_barrier_fetch_add(&calls_succeeded_, (gpr_atm(1)));
  }

  char* RenderJSON();

  ChannelTrace* trace() { return trace_.get(); }

  void set_channel_destroyed() {
    GPR_ASSERT(!channel_destroyed_);
    channel_destroyed_ = true;
  }

  intptr_t channel_uuid() { return channel_uuid_; }

 private:
  // testing peer friend.
  friend class testing::ChannelPeer;

  bool channel_destroyed_ = false;
  grpc_channel* channel_;
  const char* target_;
  gpr_atm calls_started_ = 0;
  gpr_atm calls_succeeded_ = 0;
  gpr_atm calls_failed_ = 0;
  gpr_atm last_call_started_millis_;
  intptr_t channel_uuid_;
  ManualConstructor<ChannelTrace> trace_;

  grpc_connectivity_state GetConnectivityState();
};

}  // namespace channelz
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNELZ_H */
