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

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_client_stats.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/proto/grpc/lb/v1/load_balancer.upb.h"

namespace grpc_core {

typedef grpc_lb_v1_LoadBalanceRequest xds_grpclb_request;

class XdsLocalityName : public RefCounted<XdsLocalityName> {
 public:
  struct Less {
    bool operator()(const RefCountedPtr<XdsLocalityName>& lhs,
                    const RefCountedPtr<XdsLocalityName>& rhs) {
      int cmp_result = strcmp(lhs->region_.get(), rhs->region_.get());
      if (cmp_result != 0) return cmp_result < 0;
      cmp_result = strcmp(lhs->zone_.get(), rhs->zone_.get());
      if (cmp_result != 0) return cmp_result < 0;
      return strcmp(lhs->sub_zone_.get(), rhs->sub_zone_.get()) < 0;
    }
  };

  XdsLocalityName(UniquePtr<char> region, UniquePtr<char> zone,
                  UniquePtr<char> sub_zone)
      : region_(std::move(region)),
        zone_(std::move(zone)),
        sub_zone_(std::move(sub_zone)) {}

  bool operator==(const XdsLocalityName& other) const {
    return strcmp(region_.get(), other.region_.get()) == 0 &&
           strcmp(zone_.get(), other.zone_.get()) == 0 &&
           strcmp(sub_zone_.get(), other.sub_zone_.get()) == 0;
  }

  const char* region() const { return region_.get(); }
  const char* zone() const { return zone_.get(); }
  const char* sub_zone() const { return sub_zone_.get(); }

  const char* AsHumanReadableString() {
    if (human_readable_string_ == nullptr) {
      char* tmp;
      gpr_asprintf(&tmp, "{region=\"%s\", zone=\"%s\", sub_zone=\"%s\"}",
                   region_.get(), zone_.get(), sub_zone_.get());
      human_readable_string_.reset(tmp);
    }
    return human_readable_string_.get();
  }

 private:
  UniquePtr<char> region_;
  UniquePtr<char> zone_;
  UniquePtr<char> sub_zone_;
  UniquePtr<char> human_readable_string_;
};

struct XdsLocalityInfo {
  bool operator==(const XdsLocalityInfo& other) const {
    return *locality_name == *other.locality_name &&
           serverlist == other.serverlist && lb_weight == other.lb_weight &&
           priority == other.priority;
  }

  // This comparator only compares the locality names.
  struct Less {
    bool operator()(const XdsLocalityInfo& lhs, const XdsLocalityInfo& rhs) {
      return XdsLocalityName::Less()(lhs.locality_name, rhs.locality_name);
    }
  };

  RefCountedPtr<XdsLocalityName> locality_name;
  ServerAddressList serverlist;
  uint32_t lb_weight;
  uint32_t priority;
};

using XdsLocalityList = InlinedVector<XdsLocalityInfo, 1>;

struct XdsUpdate {
  XdsLocalityList locality_list;
  // TODO(juanlishen): Pass drop_per_million when adding drop support.
};

// Creates an EDS request querying \a service_name.
grpc_slice XdsEdsRequestCreateAndEncode(const char* service_name);

// Parses the EDS response and returns the args to update locality map. If there
// is any error, the output update is invalid.
grpc_error* XdsEdsResponseDecodeAndParse(const grpc_slice& encoded_response,
                                         XdsUpdate* update);

// TODO(juanlishen): Delete these when LRS is added.
xds_grpclb_request* xds_grpclb_load_report_request_create_locked(
    grpc_core::XdsLbClientStats* client_stats, upb_arena* arena);

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOAD_BALANCER_API_H \
        */
