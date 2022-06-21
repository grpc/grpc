//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_IMPL_H
#define GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_IMPL_H
#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/strings/string_view.h"

#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

// Base class for XdsResourceType implementations.
// Handles all down-casting logic for a particular resource type struct.
template <typename Subclass, typename ResourceTypeStruct>
class XdsResourceTypeImpl : public XdsResourceType {
 public:
  struct ResourceDataSubclass : public ResourceData {
    ResourceTypeStruct resource;
  };

  // XdsClient watcher that handles down-casting.
  class WatcherInterface : public XdsClient::ResourceWatcherInterface {
   public:
    virtual void OnResourceChanged(ResourceTypeStruct listener) = 0;

   private:
    // Get result from XdsClient generic watcher interface, perform
    // down-casting, and invoke the caller's OnListenerChanged() method.
    void OnGenericResourceChanged(
        const XdsResourceType::ResourceData* resource) override {
      OnResourceChanged(
          static_cast<const ResourceDataSubclass*>(resource)->resource);
    }
  };

  static const Subclass* Get() {
    static const Subclass* g_instance = new Subclass();
    return g_instance;
  }

  // Convenient wrappers around XdsClient generic watcher API that provide
  // type-safety.
  static void StartWatch(XdsClient* xds_client, absl::string_view resource_name,
                         RefCountedPtr<WatcherInterface> watcher) {
    xds_client->WatchResource(Get(), resource_name, std::move(watcher));
  }
  static void CancelWatch(XdsClient* xds_client,
                          absl::string_view resource_name,
                          WatcherInterface* watcher,
                          bool delay_unsubscription = false) {
    xds_client->CancelResourceWatch(Get(), resource_name, watcher,
                                    delay_unsubscription);
  }

  bool ResourcesEqual(const ResourceData* r1,
                      const ResourceData* r2) const override {
    return static_cast<const ResourceDataSubclass*>(r1)->resource ==
           static_cast<const ResourceDataSubclass*>(r2)->resource;
  }

  std::unique_ptr<ResourceData> CopyResource(
      const ResourceData* resource) const override {
    auto* resource_copy = new ResourceDataSubclass();
    resource_copy->resource =
        static_cast<const ResourceDataSubclass*>(resource)->resource;
    return std::unique_ptr<ResourceData>(resource_copy);
  }
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_IMPL_H
