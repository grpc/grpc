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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOAD_BALANCER_API_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOAD_BALANCER_API_H

#include <grpc/support/port_platform.h>

#include <grpc/slice_buffer.h>

#include <stdint.h>
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_client_stats.h"
#include "src/core/ext/filters/client_channel/server_address.h"

namespace grpc_core {

struct XdsLocalityInfo {
  bool operator==(const XdsLocalityInfo& other) const {
    return *name == *other.name && serverlist == other.serverlist &&
           lb_weight == other.lb_weight && priority == other.priority;
  }

  // This comparator only compares the locality names.
  struct Less {
    bool operator()(const XdsLocalityInfo& lhs,
                    const XdsLocalityInfo& rhs) const {
      return XdsLocalityName::Less()(lhs.name, rhs.name);
    }
  };

  RefCountedPtr<XdsLocalityName> name;
  ServerAddressList serverlist;
  uint32_t lb_weight;
  uint32_t priority;
};

struct XdsLocalityList {
  bool Has(const XdsLocalityName& name) const {
    for (size_t i = 0; i < list.size(); ++i) {
      if (*list[i].name == name) return true;
    }
    return false;
  }

  size_t Size() const { return list.size(); }

  InlinedVector<XdsLocalityInfo, 1> list;
  bool applied = false;
};

class XdsLocalityListPriorityMap {
 public:
  bool operator==(XdsLocalityListPriorityMap& other);

  void Add(XdsLocalityInfo locality_info);

  // If a locality list has already been applied and doesn't change the content,
  // then it's still applied.
  void UpdateAppliedLocked(XdsLocalityListPriorityMap& old);

  void Sort();

  uint32_t NextPriority(uint32_t priority) const;

  const XdsLocalityList* Find(uint32_t priority) const;

  bool Has(uint32_t priority) { return map_.find(priority) != map_.end(); }
  bool Has(const XdsLocalityName& name);

  bool Empty() const { return sorted_priorities_.empty(); }
  size_t Size() const { return sorted_priorities_.size(); }

  const InlinedVector<uint32_t, 1>& sorted_priorities() const {
    return sorted_priorities_;
  }
  const Map<uint32_t, XdsLocalityList>& map() const { return map_; }
  uint32_t LowestPriority() const {
    auto last = --map_.cend();
    return last->first;
  }

 private:
  Map<uint32_t, XdsLocalityList> map_;
  InlinedVector<uint32_t, 1> sorted_priorities_;
};

// There are two phases of accessing this class's content:
// 1. to initialize in the control plane combiner;
// 2. to use in the data plane combiner.
// So no additional synchronization is needed.
class XdsDropConfig : public RefCounted<XdsDropConfig> {
 public:
  struct DropCategory {
    bool operator==(const DropCategory& other) const {
      return strcmp(name.get(), other.name.get()) == 0 &&
             parts_per_million == other.parts_per_million;
    }

    UniquePtr<char> name;
    const uint32_t parts_per_million;
  };

  using DropCategoryList = InlinedVector<DropCategory, 2>;

  void AddCategory(UniquePtr<char> name, uint32_t parts_per_million) {
    drop_category_list_.emplace_back(
        DropCategory{std::move(name), parts_per_million});
  }

  // The only method invoked from the data plane combiner.
  bool ShouldDrop(const UniquePtr<char>** category_name) const;

  const DropCategoryList& drop_category_list() const {
    return drop_category_list_;
  }

  bool operator==(const XdsDropConfig& other) const {
    return drop_category_list_ == other.drop_category_list_;
  }
  bool operator!=(const XdsDropConfig& other) const {
    return !(*this == other);
  }

 private:
  DropCategoryList drop_category_list_;
};

struct XdsUpdate {
  XdsLocalityListPriorityMap locality_list_map;
  RefCountedPtr<XdsDropConfig> drop_config;
  bool drop_all = false;
};

// Creates an EDS request querying \a service_name.
grpc_slice XdsEdsRequestCreateAndEncode(const char* service_name);

// Parses the EDS response and returns the args to update locality map. If there
// is any error, the output update is invalid.
grpc_error* XdsEdsResponseDecodeAndParse(const grpc_slice& encoded_response,
                                         XdsUpdate* update);

// Creates an LRS request querying \a server_name.
grpc_slice XdsLrsRequestCreateAndEncode(const char* server_name);

// Creates an LRS request sending client-side load reports. If all the counters
// in \a client_stats are zero, returns empty slice.
grpc_slice XdsLrsRequestCreateAndEncode(const char* server_name,
                                        XdsClientStats* client_stats);

// Parses the LRS response and returns the client-side load reporting interval.
// If there is any error (e.g., the found server name doesn't match \a
// expected_server_name), the output config is invalid.
grpc_error* XdsLrsResponseDecodeAndParse(const grpc_slice& encoded_response,
                                         grpc_millis* load_reporting_interval,
                                         const char* expected_server_name);

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOAD_BALANCER_API_H \
        */
