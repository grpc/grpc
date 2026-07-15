// Copyright 2025 The gRPC Authors
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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_CFSOCKET_LISTENER_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_CFSOCKET_LISTENER_H
#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE
#include <AvailabilityMacros.h>
#ifdef AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER

#include <CoreFoundation/CoreFoundation.h>
#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/cf_engine/cf_engine.h"
#include "src/core/lib/event_engine/cf_engine/cftype_unique_ref.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"

namespace grpc_event_engine::experimental {

class CFSocketListenerImpl
    : public grpc_core::RefCounted<CFSocketListenerImpl> {
 public:
  CFSocketListenerImpl(
      std::shared_ptr<CFEventEngine> engine,
      EventEngine::Listener::AcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory);
  ~CFSocketListenerImpl();

  absl::StatusOr<int> Bind(const EventEngine::ResolvedAddress& addr);
  absl::Status Start();

  void Shutdown();

 private:
  static const void* Retain(const void* info) {
    auto that = static_cast<const CFSocketListenerImpl*>(info);
    return that->Ref().release();
  }

  static void Release(const void* info) {
    auto that = static_cast<const CFSocketListenerImpl*>(info);
    that->Unref();
  }

  static void handleConnect(CFSocketRef s, CFSocketCallBackType type,
                            CFDataRef address, const void* data, void* info);

 private:
  std::shared_ptr<CFEventEngine> engine_;
  dispatch_queue_t queue_ = dispatch_queue_create("cfsocket_listener", nullptr);

  grpc_core::Mutex mu_;
  bool started_ ABSL_GUARDED_BY(mu_) = false;
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  std::vector<std::tuple<CFTypeUniqueRef<CFSocketRef>>> ipv6cfsocks_
      ABSL_GUARDED_BY(mu_);
  CFRunLoopRef runloop_ ABSL_GUARDED_BY(mu_);

  EventEngine::Listener::AcceptCallback on_accept_;
  absl::AnyInvocable<void(absl::Status)> on_shutdown_;
  std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory_;
};

class CFSocketListener : public EventEngine::Listener {
 public:
  CFSocketListener(
      std::shared_ptr<CFEventEngine> engine, Listener::AcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
      : impl_(grpc_core::MakeRefCounted<CFSocketListenerImpl>(
            std::move(engine), std::move(on_accept), std::move(on_shutdown),
            config, std::move(memory_allocator_factory))) {}

  ~CFSocketListener() override { impl_->Shutdown(); }

  absl::StatusOr<int> Bind(const EventEngine::ResolvedAddress& addr) override {
    return impl_->Bind(addr);
  };

  absl::Status Start() override { return impl_->Start(); };

 private:
  grpc_core::RefCountedPtr<CFSocketListenerImpl> impl_;
};

}  // namespace grpc_event_engine::experimental

#endif  // AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER
#endif  // GPR_APPLE

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_CFSOCKET_LISTENER_H
