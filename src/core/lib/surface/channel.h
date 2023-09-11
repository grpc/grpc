//
//
// Copyright 2015 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_H
#define GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <map>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>
#include <grpc/impl/compression_types.h>
#include <grpc/slice.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"  // IWYU pragma: keep
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/gprpp/cpp_impl_of.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport_fwd.h"

/// The same as grpc_channel_destroy, but doesn't create an ExecCtx, and so
/// is safe to use from within core.
void grpc_channel_destroy_internal(grpc_channel* channel);

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

/// Get a (borrowed) pointer to this channels underlying channel stack
grpc_channel_stack* grpc_channel_get_channel_stack(grpc_channel* channel);

grpc_core::channelz::ChannelNode* grpc_channel_get_channelz_node(
    grpc_channel* channel);

size_t grpc_channel_get_call_size_estimate(grpc_channel* channel);
void grpc_channel_update_call_size_estimate(grpc_channel* channel, size_t size);

namespace grpc_core {

struct RegisteredCall {
  Slice path;
  absl::optional<Slice> authority;

  explicit RegisteredCall(const char* method_arg, const char* host_arg);
  RegisteredCall(const RegisteredCall& other);
  RegisteredCall& operator=(const RegisteredCall&) = delete;

  ~RegisteredCall();
};

struct CallRegistrationTable {
  Mutex mu;
  // The map key should be owned strings rather than unowned char*'s to
  // guarantee that it outlives calls on the core channel (which may outlast the
  // C++ or other wrapped language Channel that registered these calls).
  std::map<std::pair<std::string, std::string>, RegisteredCall> map
      ABSL_GUARDED_BY(mu);
  int method_registration_attempts ABSL_GUARDED_BY(mu) = 0;
};

class Channel : public RefCounted<Channel>,
                public CppImplOf<Channel, grpc_channel> {
 public:
  static absl::StatusOr<RefCountedPtr<Channel>> Create(
      const char* target, ChannelArgs args,
      grpc_channel_stack_type channel_stack_type,
      grpc_transport* optional_transport);

  static absl::StatusOr<RefCountedPtr<Channel>> CreateWithBuilder(
      ChannelStackBuilder* builder);

  grpc_channel_stack* channel_stack() const { return channel_stack_.get(); }

  grpc_compression_options compression_options() const {
    return compression_options_;
  }

  channelz::ChannelNode* channelz_node() const { return channelz_node_.get(); }

  size_t CallSizeEstimate() {
    // We round up our current estimate to the NEXT value of kRoundUpSize.
    // This ensures:
    //  1. a consistent size allocation when our estimate is drifting slowly
    //     (which is common) - which tends to help most allocators reuse memory
    //  2. a small amount of allowed growth over the estimate without hitting
    //     the arena size doubling case, reducing overall memory usage
    static constexpr size_t kRoundUpSize = 256;
    return (call_size_estimate_.load(std::memory_order_relaxed) +
            2 * kRoundUpSize) &
           ~(kRoundUpSize - 1);
  }

  void UpdateCallSizeEstimate(size_t size);
  absl::string_view target() const { return target_; }
  MemoryAllocator* allocator() { return &allocator_; }
  bool is_client() const { return is_client_; }
  bool is_promising() const { return is_promising_; }
  RegisteredCall* RegisterCall(const char* method, const char* host);

  int TestOnlyRegisteredCalls() {
    MutexLock lock(&registration_table_.mu);
    return registration_table_.map.size();
  }

  int TestOnlyRegistrationAttempts() {
    MutexLock lock(&registration_table_.mu);
    return registration_table_.method_registration_attempts;
  }

  grpc_event_engine::experimental::EventEngine* event_engine() const {
    return channel_stack_->EventEngine();
  }

  const ChannelArgs& channel_args() const { return channel_args_; }

 private:
  Channel(bool is_client, bool is_promising, std::string target,
          const ChannelArgs& channel_args,
          grpc_compression_options compression_options,
          RefCountedPtr<grpc_channel_stack> channel_stack);

  const bool is_client_;
  const bool is_promising_;
  const grpc_compression_options compression_options_;
  std::atomic<size_t> call_size_estimate_;
  CallRegistrationTable registration_table_;
  RefCountedPtr<channelz::ChannelNode> channelz_node_;
  MemoryAllocator allocator_;
  std::string target_;
  const RefCountedPtr<grpc_channel_stack> channel_stack_;
  const ChannelArgs channel_args_;
};

}  // namespace grpc_core

inline grpc_compression_options grpc_channel_compression_options(
    const grpc_channel* channel) {
  return grpc_core::Channel::FromC(channel)->compression_options();
}

inline grpc_channel_stack* grpc_channel_get_channel_stack(
    grpc_channel* channel) {
  return grpc_core::Channel::FromC(channel)->channel_stack();
}

inline grpc_core::channelz::ChannelNode* grpc_channel_get_channelz_node(
    grpc_channel* channel) {
  return grpc_core::Channel::FromC(channel)->channelz_node();
}

inline void grpc_channel_internal_ref(grpc_channel* channel,
                                      const char* reason) {
  grpc_core::Channel::FromC(channel)->Ref(DEBUG_LOCATION, reason).release();
}
inline void grpc_channel_internal_unref(grpc_channel* channel,
                                        const char* reason) {
  grpc_core::Channel::FromC(channel)->Unref(DEBUG_LOCATION, reason);
}

// Return the channel's compression options.
grpc_compression_options grpc_channel_compression_options(
    const grpc_channel* channel);

// Ping the channels peer (load balanced channels will select one sub-channel to
// ping); if the channel is not connected, posts a failed.
void grpc_channel_ping(grpc_channel* channel, grpc_completion_queue* cq,
                       void* tag, void* reserved);

#endif  // GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_H
