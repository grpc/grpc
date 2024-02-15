//
// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_H
#define GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_H

#include <grpc/support/port_platform.h>

#include <map>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/compression_types.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/gprpp/cpp_impl_of.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/channel_stack_type.h"

namespace grpc_core {

class Channel : public RefCounted<Channel>,
                public CppImplOf<Channel, grpc_channel> {
 public:
  struct RegisteredCall {
    Slice path;
    absl::optional<Slice> authority;

    explicit RegisteredCall(const char* method_arg, const char* host_arg);
    RegisteredCall(const RegisteredCall& other);
    RegisteredCall& operator=(const RegisteredCall&) = delete;

    ~RegisteredCall();
  };

  static absl::StatusOr<RefCountedPtr<Channel>> Create(
      std::string target, ChannelArgs args,
      grpc_channel_stack_type channel_stack_type,
      Transport* optional_transport);

  virtual ~Channel() override;

  virtual void Orphan() = 0;

  virtual Arena* CreateArena() = 0;
  virtual void DestroyArena(Arena* arena) = 0;

  // TODO(roth): This should return a C++ type.
  virtual grpc_call* CreateCall(
      grpc_call* parent_call, uint32_t propagation_mask,
      grpc_completion_queue* cq, grpc_pollset_set* pollset_set_alternative,
      Slice path, absl::optional<Slice> authority, Timestamp deadline,
      bool registered_method) = 0;

  virtual grpc_event_engine::experimental::EventEngine* event_engine() const =
      0;

  virtual bool SupportsConnectivityWatcher() const = 0;

  virtual grpc_connectivity_state CheckConnectivityState(
      bool try_to_connect) = 0;

  // For external watched via the C-core API.
  virtual void WatchConnectivityState(
      grpc_connectivity_state last_observed_state,
      Timestamp deadline, grpc_completion_queue* cq, void* tag) = 0;

  // For internal watches.
  virtual void AddConnectivityWatcher(
      grpc_connectivity_state initial_state,
      OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher) = 0;
  virtual void RemoveConnectivityWatcher(
      AsyncConnectivityStateWatcherInterface* watcher) = 0;

  virtual void GetInfo(const grpc_channel_info* channel_info) = 0;

  virtual void ResetConnectionBackoff() = 0;

  absl::string_view target() const { return target_; }
  channelz::ChannelNode* channelz_node() const { return channelz_node_.get(); }
  grpc_compression_options compression_options() const {
    return compression_options_;
  }

  RegisteredCall* RegisterCall(const char* method, const char* host);

  int TestOnlyRegisteredCalls() {
    MutexLock lock(&mu_);
    return registration_table_.size();
  }

  // For tests only.
  // Pings the channel's peer.  Load-balanced channels will select one
  // subchannel to ping.  If the channel is not connected, posts a
  // failure to the CQ.
  virtual void Ping(grpc_completion_queue* cq, void* tag) = 0;

 protected:
  Channel(std::string target, const ChannelArgs& channel_args,
          grpc_compression_options compression_options);

 private:
  const std::string target_;
  const RefCountedPtr<channelz::ChannelNode> channelz_node_;
  const grpc_compression_options compression_options_;

  Mutex mu_;
  // The map key needs to be owned strings rather than unowned char*'s to
  // guarantee that it outlives calls on the core channel (which may outlast
  // the C++ or other wrapped language Channel that registered these calls).
  std::map<std::pair<std::string, std::string>, RegisteredCall>
      registration_table_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

/// The same as grpc_channel_destroy, but doesn't create an ExecCtx, and so
/// is safe to use from within core.
inline void grpc_channel_destroy_internal(grpc_channel* channel) {
  grpc_core::Channel::FromC(channel)->Orphan();
}

/// Create a call given a grpc_channel, in order to call \a method.
/// Progress is tied to activity on \a pollset_set. The returned call object is
/// meant to be used with \a grpc_call_start_batch_and_execute, which relies on
/// callbacks to signal completions. \a method and \a host need
/// only live through the invocation of this function. If \a parent_call is
/// non-NULL, it must be a server-side call. It will be used to propagate
/// properties from the server call to this new client call, depending on the
/// value of \a propagation_mask (see propagation_bits.h for possible values)
grpc_call* grpc_channel_create_pollset_set_call(
    grpc_channel* channel, grpc_call* parent_call, uint32_t propagation_mask,
    grpc_pollset_set* pollset_set, const grpc_slice& method,
    const grpc_slice* host, grpc_core::Timestamp deadline, void* reserved);

// Return the channel's compression options.
inline grpc_compression_options grpc_channel_compression_options(
    const grpc_channel* channel) {
  return grpc_core::Channel::FromC(channel)->compression_options();
}

inline grpc_core::channelz::ChannelNode* grpc_channel_get_channelz_node(
    grpc_channel* channel) {
  return grpc_core::Channel::FromC(channel)->channelz_node();
}

// Ping the channels peer (load balanced channels will select one sub-channel to
// ping); if the channel is not connected, posts a failed.
void grpc_channel_ping(grpc_channel* channel, grpc_completion_queue* cq,
                       void* tag, void* reserved);

#endif  // GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_H
