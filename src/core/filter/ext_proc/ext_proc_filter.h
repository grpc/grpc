//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_FILTER_EXT_PROC_EXT_PROC_FILTER_H
#define GRPC_SRC_CORE_FILTER_EXT_PROC_EXT_PROC_FILTER_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/call_destination.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class ExtProcFilter final : public V3InterceptorToV2Bridge<CompositeFilter> {
 public:
  struct ProcessingMode {
    // nullopt is DEFAULT, true is SEND, false is SKIP
    std::optional<bool> send_request_headers;
    std::optional<bool> send_response_headers;
    std::optional<bool> send_response_trailers;

    // true is GRPC, false is NONE
    bool send_request_body;
    bool send_response_body;
  };

  // Top-level filter config.
  struct Config final : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("ext_proc_filter_config");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override {
      const auto& o = DownCast<const Config&>(other);
// FIXME: other fields
      return grpc_service == o.grpc_service;
    }
    std::string ToString() const override {
// FIXME: other fields
      return absl::StrCat("{grpc_service=", grpc_service.ToString(), "}");
    }

    XdsGrpcService grpc_service;
    bool failure_mode_allow;
    ProcessingMode processing_mode;
    bool allow_mode_override;
    std::vector<ProcessingMode> allowed_override_modes;
    std::vector<std::string> request_attributes;
    std::vector<std::string> response_attributes;
    std::optional<XdsHeaderMutationRules> mutation_rules;
    std::vector<StringMatcher> forwarding_allowed_headers;
    std::vector<StringMatcher> forwarding_disallowed_headers;
    bool disable_immediate_response;
    bool observability_mode;
    Duration deferred_close_timeout;
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "ext_proc"; }

  static absl::StatusOr<RefCountedPtr<ExtProcFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  ExtProcFilter(const ChannelArgs& args, RefCountedPtr<const Config> config,
                ChannelFilter::Args filter_args);

 private:
  void Orphaned() override {}

  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override;

  RefCountedPtr<const Config> config_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_EXT_PROC_EXT_PROC_FILTER_H
