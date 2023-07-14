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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_RING_HASH_RING_HASH_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_RING_HASH_RING_HASH_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include "absl/strings/string_view.h"

#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/service_config/service_config_call_data.h"

namespace grpc_core {

class RequestHashAttribute
    : public ServiceConfigCallData::CallAttributeInterface {
 public:
  static UniqueTypeName TypeName();

  explicit RequestHashAttribute(absl::string_view request_hash)
      : request_hash_(request_hash) {}

  absl::string_view request_hash() const { return request_hash_; }

 private:
  UniqueTypeName type() const override { return TypeName(); }

  absl::string_view request_hash_;
};

// Helper Parsing method to parse ring hash policy configs; for example, ring
// hash size validity.
struct RingHashConfig {
  uint64_t min_ring_size = 1024;
  uint64_t max_ring_size = 4096;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
  void JsonPostLoad(const Json& json, const JsonArgs&,
                    ValidationErrors* errors);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_RING_HASH_RING_HASH_H
