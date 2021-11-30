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

#ifndef GRPC_CORE_EXT_XDS_XDS_ENDPOINT_H
#define GRPC_CORE_EXT_XDS_XDS_ENDPOINT_H

#include <grpc/support/port_platform.h>

#include <map>
#include <set>
#include <string>

#include "absl/container/inlined_vector.h"
#include "envoy/config/endpoint/v3/endpoint.upbdefs.h"

#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

struct XdsEndpointResource {
  struct Priority {
    struct Locality {
      RefCountedPtr<XdsLocalityName> name;
      uint32_t lb_weight;
      ServerAddressList endpoints;

      bool operator==(const Locality& other) const {
        return *name == *other.name && lb_weight == other.lb_weight &&
               endpoints == other.endpoints;
      }
      bool operator!=(const Locality& other) const { return !(*this == other); }
      std::string ToString() const;
    };

    std::map<XdsLocalityName*, Locality, XdsLocalityName::Less> localities;

    bool operator==(const Priority& other) const;
    std::string ToString() const;
  };
  using PriorityList = absl::InlinedVector<Priority, 2>;

  // There are two phases of accessing this class's content:
  // 1. to initialize in the control plane combiner;
  // 2. to use in the data plane combiner.
  // So no additional synchronization is needed.
  class DropConfig : public RefCounted<DropConfig> {
   public:
    struct DropCategory {
      bool operator==(const DropCategory& other) const {
        return name == other.name &&
               parts_per_million == other.parts_per_million;
      }

      std::string name;
      const uint32_t parts_per_million;
    };

    using DropCategoryList = absl::InlinedVector<DropCategory, 2>;

    void AddCategory(std::string name, uint32_t parts_per_million) {
      drop_category_list_.emplace_back(
          DropCategory{std::move(name), parts_per_million});
      if (parts_per_million == 1000000) drop_all_ = true;
    }

    // The only method invoked from outside the WorkSerializer (used in
    // the data plane).
    bool ShouldDrop(const std::string** category_name) const;

    const DropCategoryList& drop_category_list() const {
      return drop_category_list_;
    }

    bool drop_all() const { return drop_all_; }

    bool operator==(const DropConfig& other) const {
      return drop_category_list_ == other.drop_category_list_;
    }
    bool operator!=(const DropConfig& other) const { return !(*this == other); }

    std::string ToString() const;

   private:
    DropCategoryList drop_category_list_;
    bool drop_all_ = false;
  };

  PriorityList priorities;
  RefCountedPtr<DropConfig> drop_config;

  bool operator==(const XdsEndpointResource& other) const {
    return priorities == other.priorities && *drop_config == *other.drop_config;
  }
  std::string ToString() const;
};

class XdsEndpointResourceType : public XdsResourceType {
 public:
  struct EndpointData : public ResourceData {
    XdsEndpointResource resource;
  };

  class WatcherInterface : public XdsClient::ResourceWatcherInterface {
   public:
    virtual void OnEndpointChanged(XdsEndpointResource update) = 0;

   private:
    void OnResourceChanged(
        const XdsResourceType::ResourceData* resource) override {
      OnEndpointChanged(
          static_cast<const XdsEndpointResourceType::EndpointData*>(resource)
              ->resource);
    }
  };

  static const XdsEndpointResourceType* Get() {
    static const XdsEndpointResourceType* g_instance =
        new XdsEndpointResourceType();
    return g_instance;
  }

  absl::string_view type_url() const override {
    return "envoy.config.endpoint.v3.ClusterLoadAssignment";
  }
  absl::string_view v2_type_url() const override {
    return "envoy.api.v2.ClusterLoadAssignment";
  }

  static void StartWatch(XdsClient* xds_client, absl::string_view resource_name,
                         RefCountedPtr<WatcherInterface> watcher) {
    xds_client->WatchResource(Get(), resource_name, std::move(watcher));
  }

  static void CancelWatch(XdsClient* xds_client, absl::string_view resource_name,
                          WatcherInterface* watcher,
                          bool delay_unsubscription = false) {
    xds_client->CancelResourceWatch(Get(), resource_name, watcher,
                                    delay_unsubscription);
  }

  absl::StatusOr<DecodeResult> Decode(const XdsEncodingContext& context,
                                      absl::string_view serialized_resource,
                                      bool is_v2) const override;

  bool ResourcesEqual(const ResourceData* r1,
                      const ResourceData* r2) const override {
    return static_cast<const EndpointData*>(r1)->resource ==
           static_cast<const EndpointData*>(r2)->resource;
  }

  std::unique_ptr<ResourceData> CopyResource(
      const ResourceData* resource) const override {
    auto* resource_copy = new EndpointData();
    resource_copy->resource =
        static_cast<const EndpointData*>(resource)->resource;
    return std::unique_ptr<ResourceData>(resource_copy);
  }

  void InitUpbSymtab(upb_symtab* symtab) const override {
    envoy_config_endpoint_v3_ClusterLoadAssignment_getmsgdef(symtab);
  }

 private:
  XdsEndpointResourceType() {
    XdsResourceTypeRegistry::GetOrCreate()->RegisterType(this);
  }
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_ENDPOINT_H
