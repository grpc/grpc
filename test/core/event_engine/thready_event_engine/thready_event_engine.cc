// Copyright 2023 gRPC authors.
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

#include "test/core/event_engine/thready_event_engine/thready_event_engine.h"

#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "src/core/lib/gprpp/crash.h"

namespace grpc_event_engine {
namespace experimental {

void ThreadyEventEngine::Asynchronously(absl::AnyInvocable<void()> fn) {
  std::thread([fn = std::move(fn)]() mutable { fn(); }).detach();
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
ThreadyEventEngine::CreateListener(
    Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  return impl_->CreateListener(
      [this, on_accept = std::make_shared<Listener::AcceptCallback>(
                 std::move(on_accept))](std::unique_ptr<Endpoint> endpoint,
                                        MemoryAllocator memory_allocator) {
        Asynchronously(
            [on_accept, endpoint = std::move(endpoint),
             memory_allocator = std::move(memory_allocator)]() mutable {
              (*on_accept)(std::move(endpoint), std::move(memory_allocator));
            });
      },
      [this,
       on_shutdown = std::move(on_shutdown)](absl::Status status) mutable {
        Asynchronously([on_shutdown = std::move(on_shutdown),
                        status = std::move(status)]() mutable {
          on_shutdown(std::move(status));
        });
      },
      config, std::move(memory_allocator_factory));
}

EventEngine::ConnectionHandle ThreadyEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator memory_allocator,
    Duration timeout) {
  return impl_->Connect(
      [this, on_connect = std::move(on_connect)](
          absl::StatusOr<std::unique_ptr<Endpoint>> c) mutable {
        Asynchronously(
            [on_connect = std::move(on_connect), c = std::move(c)]() mutable {
              on_connect(std::move(c));
            });
      },
      addr, args, std::move(memory_allocator), timeout);
}

bool ThreadyEventEngine::CancelConnect(ConnectionHandle handle) {
  return impl_->CancelConnect(handle);
}

bool ThreadyEventEngine::IsWorkerThread() {
  grpc_core::Crash("we should remove this");
}

std::unique_ptr<EventEngine::DNSResolver> ThreadyEventEngine::GetDNSResolver(
    const DNSResolver::ResolverOptions& options) {
  return std::make_unique<ThreadyDNSResolver>(impl_->GetDNSResolver(options));
}

void ThreadyEventEngine::Run(Closure* closure) {
  Run([closure]() { closure->Run(); });
}

void ThreadyEventEngine::Run(absl::AnyInvocable<void()> closure) {
  Asynchronously(std::move(closure));
}

EventEngine::TaskHandle ThreadyEventEngine::RunAfter(Duration when,
                                                     Closure* closure) {
  return RunAfter(when, [closure]() { closure->Run(); });
}

EventEngine::TaskHandle ThreadyEventEngine::RunAfter(
    Duration when, absl::AnyInvocable<void()> closure) {
  return impl_->RunAfter(when, [this, closure = std::move(closure)]() mutable {
    Asynchronously(std::move(closure));
  });
}

bool ThreadyEventEngine::Cancel(TaskHandle handle) {
  return impl_->Cancel(handle);
}

EventEngine::DNSResolver::LookupTaskHandle
ThreadyEventEngine::ThreadyDNSResolver::LookupHostname(
    LookupHostnameCallback on_resolve, absl::string_view name,
    absl::string_view default_port, Duration timeout) {
  return impl_->LookupHostname(
      [this, on_resolve = std::move(on_resolve)](
          absl::StatusOr<std::vector<ResolvedAddress>> addresses) mutable {
        engine_->Asynchronously([on_resolve = std::move(on_resolve),
                                 addresses = std::move(addresses)]() mutable {
          on_resolve(std::move(addresses));
        });
      },
      name, default_port, timeout);
}

EventEngine::DNSResolver::LookupTaskHandle
ThreadyEventEngine::ThreadyDNSResolver::LookupSRV(LookupSRVCallback on_resolve,
                                                  absl::string_view name,
                                                  Duration timeout) {
  return impl_->LookupSRV(
      [this, on_resolve = std::move(on_resolve)](
          absl::StatusOr<std::vector<SRVRecord>> records) mutable {
        return engine_->Asynchronously(
            [on_resolve = std::move(on_resolve),
             records = std::move(records)]() mutable {
              on_resolve(std::move(records));
            });
      },
      name, timeout);
}

EventEngine::DNSResolver::LookupTaskHandle
ThreadyEventEngine::ThreadyDNSResolver::LookupTXT(LookupTXTCallback on_resolve,
                                                  absl::string_view name,
                                                  Duration timeout) {
  return impl_->LookupTXT(
      [this, on_resolve = std::move(on_resolve)](
          absl::StatusOr<std::vector<std::string>> record) mutable {
        return engine_->Asynchronously([on_resolve = std::move(on_resolve),
                                        record = std::move(record)]() mutable {
          on_resolve(std::move(record));
        });
      },
      name, timeout);
}

bool ThreadyEventEngine::ThreadyDNSResolver::CancelLookup(
    LookupTaskHandle handle) {
  return impl_->CancelLookup(handle);
}

}  // namespace experimental
}  // namespace grpc_event_engine
