//
// Copyright 2026 gRPC authors.
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

#include "test/core/xds/fake_xds_client.h"

#include <grpc/support/port_platform.h>

#include <utility>

#include "src/core/util/grpc_check.h"

namespace grpc_core {

FakeXdsClient::FakeXdsClient(std::shared_ptr<XdsBootstrap> bootstrap)
    : bootstrap_(bootstrap != nullptr ? std::move(bootstrap)
                                      : std::make_shared<FakeXdsBootstrap>()) {}

void FakeXdsClient::WatchResource(
    const XdsResourceType* type, absl::string_view name,
    RefCountedPtr<ResourceWatcherInterface> watcher) {
  GRPC_CHECK_NE(type, nullptr);
  GRPC_CHECK_NE(watcher.get(), nullptr);
  MutexLock lock(mu_);
  WatchKey key{type, std::string(name)};
  ResourceWatcherInterface* raw = watcher.get();
  auto [_, inserted] = watchers_[key].emplace(raw, std::move(watcher));
  GRPC_CHECK(inserted) << "Duplicate watch for " << type->type_url() << " "
                       << name;
}

void FakeXdsClient::CancelResourceWatch(const XdsResourceType* type,
                                        absl::string_view name,
                                        ResourceWatcherInterface* watcher,
                                        bool delay_unsubscription) {
  GRPC_CHECK_NE(type, nullptr);
  RefCountedPtr<ResourceWatcherInterface> removed_watcher;
  {
    MutexLock lock(mu_);
    WatchKey key{type, std::string(name)};
    auto it = watchers_.find(key);
    GRPC_CHECK(it != watchers_.end())
        << "CancelResourceWatch for non-existent watch: " << type->type_url()
        << " " << name;
    auto watcher_it = it->second.find(watcher);
    GRPC_CHECK(watcher_it != it->second.end())
        << "Watcher not found for " << type->type_url() << " " << name;
    removed_watcher = std::move(watcher_it->second);
    it->second.erase(watcher_it);
    if (it->second.empty()) {
      watchers_.erase(it);
    }
    cancel_history_.push_back(
        CancelRecord{type, std::string(name), delay_unsubscription});
  }
}

void FakeXdsClient::ResetBackoff() {
  MutexLock lock(mu_);
  ++reset_backoff_count_;
}

bool FakeXdsClient::HasWatch(const XdsResourceType* type,
                             absl::string_view name) const {
  MutexLock lock(mu_);
  return watchers_.find(WatchKey{type, std::string(name)}) != watchers_.end();
}

bool FakeXdsClient::HasAnyWatches() const {
  MutexLock lock(mu_);
  return !watchers_.empty();
}

std::set<std::string> FakeXdsClient::GetWatchedResources(
    const XdsResourceType* type) const {
  MutexLock lock(mu_);
  std::set<std::string> result;
  for (const auto& [key, _] : watchers_) {
    if (key.type == type) result.insert(key.name);
  }
  return result;
}

std::vector<FakeXdsClient::CancelRecord> FakeXdsClient::TakeCancelHistory() {
  MutexLock lock(mu_);
  return std::exchange(cancel_history_, {});
}

size_t FakeXdsClient::TakeResetBackoffCount() {
  MutexLock lock(mu_);
  return std::exchange(reset_backoff_count_, 0);
}

RefCountedPtr<XdsClientInterface::ResourceWatcherInterface>
FakeXdsClient::GetWatcher(const XdsResourceType* type,
                          absl::string_view name) const {
  MutexLock lock(mu_);
  auto it = watchers_.find(WatchKey{type, std::string(name)});
  if (it == watchers_.end() || it->second.empty()) return nullptr;
  return it->second.begin()->second;
}

std::vector<RefCountedPtr<XdsClientInterface::ResourceWatcherInterface>>
FakeXdsClient::GetWatchers(const XdsResourceType* type,
                           absl::string_view name) const {
  MutexLock lock(mu_);
  std::vector<RefCountedPtr<ResourceWatcherInterface>> result;
  auto it = watchers_.find(WatchKey{type, std::string(name)});
  if (it == watchers_.end()) return result;
  result.reserve(it->second.size());
  for (const auto& [_, watcher] : it->second) {
    result.push_back(watcher);
  }
  return result;
}

bool FakeXdsClient::TriggerResourceUpdate(
    const XdsResourceType* type, absl::string_view name,
    absl::StatusOr<std::shared_ptr<const XdsResourceType::ResourceData>>
        resource,
    RefCountedPtr<ReadDelayHandle> read_delay_handle) {
  GRPC_CHECK_NE(type, nullptr);
  std::vector<RefCountedPtr<ResourceWatcherInterface>> target_watchers;
  {
    MutexLock lock(mu_);
    auto it = watchers_.find(WatchKey{type, std::string(name)});
    if (it == watchers_.end() || it->second.empty()) return false;
    target_watchers.reserve(it->second.size());
    for (const auto& [_, watcher] : it->second) {
      target_watchers.push_back(watcher);
    }
  }
  for (const auto& watcher : target_watchers) {
    watcher->OnGenericResourceChanged(resource, read_delay_handle);
  }
  return true;
}

bool FakeXdsClient::TriggerAmbientError(
    const XdsResourceType* type, absl::string_view name, absl::Status status,
    RefCountedPtr<ReadDelayHandle> read_delay_handle) {
  GRPC_CHECK_NE(type, nullptr);
  std::vector<RefCountedPtr<ResourceWatcherInterface>> target_watchers;
  {
    MutexLock lock(mu_);
    auto it = watchers_.find(WatchKey{type, std::string(name)});
    if (it == watchers_.end() || it->second.empty()) return false;
    target_watchers.reserve(it->second.size());
    for (const auto& [_, watcher] : it->second) {
      target_watchers.push_back(watcher);
    }
  }
  for (const auto& watcher : target_watchers) {
    watcher->OnAmbientError(status, read_delay_handle);
  }
  return true;
}

void FakeXdsClient::DeliverToWatcher(
    ResourceWatcherInterface* watcher,
    absl::StatusOr<std::shared_ptr<const XdsResourceType::ResourceData>>
        resource,
    RefCountedPtr<ReadDelayHandle> read_delay_handle) {
  GRPC_CHECK_NE(watcher, nullptr);
  watcher->OnGenericResourceChanged(std::move(resource),
                                    std::move(read_delay_handle));
}

void FakeXdsClient::DeliverAmbientErrorToWatcher(
    ResourceWatcherInterface* watcher, absl::Status status,
    RefCountedPtr<ReadDelayHandle> read_delay_handle) {
  GRPC_CHECK_NE(watcher, nullptr);
  watcher->OnAmbientError(std::move(status), std::move(read_delay_handle));
}

}  // namespace grpc_core
