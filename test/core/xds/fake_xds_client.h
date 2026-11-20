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

#ifndef GRPC_TEST_CORE_XDS_FAKE_XDS_CLIENT_H
#define GRPC_TEST_CORE_XDS_FAKE_XDS_CLIENT_H

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/core/util/json/json.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_client_interface.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "src/core/xds/xds_client/xds_transport_factory.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Minimal XdsBootstrap implementation for tests using FakeXdsClient.
class FakeXdsBootstrap : public XdsBootstrap {
 public:
  class FakeNode final : public Node {
   public:
    FakeNode() = default;
    explicit FakeNode(std::string id) : id_(std::move(id)) {}

    const std::string& id() const override { return id_; }
    const std::string& cluster() const override { return cluster_; }
    const std::string& locality_region() const override { return region_; }
    const std::string& locality_zone() const override { return zone_; }
    const std::string& locality_sub_zone() const override { return sub_zone_; }
    const Json::Object& metadata() const override { return metadata_; }

    FakeNode& set_id(std::string id) {
      id_ = std::move(id);
      return *this;
    }
    FakeNode& set_cluster(std::string cluster) {
      cluster_ = std::move(cluster);
      return *this;
    }
    FakeNode& set_locality_region(std::string region) {
      region_ = std::move(region);
      return *this;
    }
    FakeNode& set_locality_zone(std::string zone) {
      zone_ = std::move(zone);
      return *this;
    }
    FakeNode& set_locality_sub_zone(std::string sub_zone) {
      sub_zone_ = std::move(sub_zone);
      return *this;
    }
    FakeNode& set_metadata(Json::Object metadata) {
      metadata_ = std::move(metadata);
      return *this;
    }

   private:
    std::string id_;
    std::string cluster_;
    std::string region_;
    std::string zone_;
    std::string sub_zone_;
    Json::Object metadata_;
  };

  FakeXdsBootstrap() = default;
  explicit FakeXdsBootstrap(std::optional<std::string> node_id) {
    if (node_id.has_value()) {
      node_.emplace(std::move(*node_id));
    }
  }
  explicit FakeXdsBootstrap(FakeNode node) : node_(std::move(node)) {}

  void set_node(std::optional<FakeNode> node) { node_ = std::move(node); }

  std::string ToString() const override { return "<fake>"; }
  std::vector<const XdsServer*> servers() const override { return {}; }
  const Node* node() const override {
    return node_.has_value() ? &*node_ : nullptr;
  }
  const Authority* LookupAuthority(const std::string&) const override {
    return nullptr;
  }

 private:
  std::optional<FakeNode> node_;
};

// Generic fake XdsClientInterface implementation for unit tests.
// Does not depend on any gRPC-specific xDS extensions or GoogleTest.
class FakeXdsClient final : public XdsClientInterface {
 public:
  // A ReadDelayHandle that sets *destroyed = true upon destruction, allowing
  // tests to verify read-delay handle lifetime.
  class TestReadDelayHandle final : public ReadDelayHandle {
   public:
    explicit TestReadDelayHandle(bool* destroyed) : destroyed_(destroyed) {}
    ~TestReadDelayHandle() override {
      if (destroyed_ != nullptr) *destroyed_ = true;
    }

   private:
    bool* destroyed_;
  };

  struct CancelRecord {
    const XdsResourceType* type;
    std::string name;
    bool delay_unsubscription;

    bool operator==(const CancelRecord& other) const {
      return type == other.type && name == other.name &&
             delay_unsubscription == other.delay_unsubscription;
    }

    friend std::ostream& operator<<(std::ostream& os,
                                    const CancelRecord& record) {
      return os << "{"
                << (record.type != nullptr ? record.type->type_url() : "null")
                << ", \"" << record.name
                << "\", delay_unsubscription=" << record.delay_unsubscription
                << "}";
    }
  };

  explicit FakeXdsClient(std::shared_ptr<XdsBootstrap> bootstrap =
                             std::make_shared<FakeXdsBootstrap>());

  // XdsClientInterface overrides.
  void WatchResource(const XdsResourceType* type, absl::string_view name,
                     RefCountedPtr<ResourceWatcherInterface> watcher) override;
  void CancelResourceWatch(const XdsResourceType* type, absl::string_view name,
                           ResourceWatcherInterface* watcher,
                           bool delay_unsubscription) override;
  void ResetBackoff() override;
  const XdsBootstrap& bootstrap() const override { return *bootstrap_; }
  XdsTransportFactory* transport_factory() const override { return nullptr; }

  // Watch inspection helpers.
  bool HasWatch(const XdsResourceType* type, absl::string_view name) const;
  template <typename ResourceType>
  bool HasWatch(absl::string_view name) const {
    return HasWatch(ResourceType::Get(), name);
  }

  bool HasAnyWatches() const;

  std::set<std::string> GetWatchedResources(const XdsResourceType* type) const;
  template <typename ResourceType>
  std::set<std::string> GetWatchedResources() const {
    return GetWatchedResources(ResourceType::Get());
  }

  std::vector<CancelRecord> TakeCancelHistory();

  size_t TakeResetBackoffCount();

  // Returns an active watcher for (type, name), or nullptr if none exists.
  RefCountedPtr<ResourceWatcherInterface> GetWatcher(
      const XdsResourceType* type, absl::string_view name) const;
  template <typename ResourceType>
  RefCountedPtr<ResourceWatcherInterface> GetWatcher(
      absl::string_view name) const {
    return GetWatcher(ResourceType::Get(), name);
  }

  // Returns all active watchers for (type, name).
  std::vector<RefCountedPtr<ResourceWatcherInterface>> GetWatchers(
      const XdsResourceType* type, absl::string_view name) const;
  template <typename ResourceType>
  std::vector<RefCountedPtr<ResourceWatcherInterface>> GetWatchers(
      absl::string_view name) const {
    return GetWatchers(ResourceType::Get(), name);
  }

  // Delivers a resource update to all active watchers for (type, name).
  // Returns false if there is no active watch for (type, name).
  [[nodiscard]] bool TriggerResourceUpdate(
      const XdsResourceType* type, absl::string_view name,
      absl::StatusOr<std::shared_ptr<const XdsResourceType::ResourceData>>
          resource,
      RefCountedPtr<ReadDelayHandle> read_delay_handle =
          ReadDelayHandle::NoWait());

  template <typename ResourceType>
  [[nodiscard]] bool TriggerResourceUpdate(
      absl::string_view name,
      absl::StatusOr<std::shared_ptr<const typename ResourceType::ResourceType>>
          resource,
      RefCountedPtr<ReadDelayHandle> read_delay_handle =
          ReadDelayHandle::NoWait()) {
    if (resource.ok()) {
      return TriggerResourceUpdate(
          ResourceType::Get(), name,
          std::shared_ptr<const XdsResourceType::ResourceData>(
              std::move(*resource)),
          std::move(read_delay_handle));
    }
    return TriggerResourceUpdate(ResourceType::Get(), name, resource.status(),
                                 std::move(read_delay_handle));
  }

  // Delivers an ambient error to all active watchers for (type, name).
  // Returns false if there is no active watch for (type, name).
  [[nodiscard]] bool TriggerAmbientError(
      const XdsResourceType* type, absl::string_view name, absl::Status status,
      RefCountedPtr<ReadDelayHandle> read_delay_handle =
          ReadDelayHandle::NoWait());

  template <typename ResourceType>
  [[nodiscard]] bool TriggerAmbientError(
      absl::string_view name, absl::Status status,
      RefCountedPtr<ReadDelayHandle> read_delay_handle =
          ReadDelayHandle::NoWait()) {
    return TriggerAmbientError(ResourceType::Get(), name, std::move(status),
                               std::move(read_delay_handle));
  }

  // Direct delivery to a specific watcher (e.g., for testing stale watcher
  // callbacks after CancelResourceWatch()).
  static void DeliverToWatcher(
      ResourceWatcherInterface* watcher,
      absl::StatusOr<std::shared_ptr<const XdsResourceType::ResourceData>>
          resource,
      RefCountedPtr<ReadDelayHandle> read_delay_handle =
          ReadDelayHandle::NoWait());

  static void DeliverAmbientErrorToWatcher(
      ResourceWatcherInterface* watcher, absl::Status status,
      RefCountedPtr<ReadDelayHandle> read_delay_handle =
          ReadDelayHandle::NoWait());

 private:
  void Orphaned() override {}

  struct WatchKey {
    const XdsResourceType* type;
    std::string name;

    bool operator<(const WatchKey& other) const {
      if (type != other.type) return type < other.type;
      return name < other.name;
    }
  };

  std::shared_ptr<XdsBootstrap> bootstrap_;
  mutable Mutex mu_;
  std::map<WatchKey, std::map<ResourceWatcherInterface*,
                              RefCountedPtr<ResourceWatcherInterface>>>
      watchers_ ABSL_GUARDED_BY(mu_);
  std::vector<CancelRecord> cancel_history_ ABSL_GUARDED_BY(mu_);
  size_t reset_backoff_count_ ABSL_GUARDED_BY(mu_) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_XDS_FAKE_XDS_CLIENT_H
