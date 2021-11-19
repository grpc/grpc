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

#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "upb/text_encode.h"
#include "upb/upb.h"
#include "upb/upb.hpp"

#include "src/core/lib/debug/trace.h"
#include "src/core/ext/xds/certificate_provider_store.h"

#ifndef GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H
#define GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H

namespace grpc_core {

class XdsClient;

struct XdsEncodingContext {
  XdsClient* client;  // Used only for logging. Unsafe for dereferencing.
  TraceFlag* tracer;
  upb_symtab* symtab;
  upb_arena* arena;
  bool use_v3;
  const CertificateProviderStore::PluginDefinitionMap*
      certificate_provider_definition_map;
};

class XdsResourceType {
 public:
  // A base type for resource data.
  // Subclasses will extend this, and their DecodeResults will be
  // downcastable to their extended type.
  struct ResourceData {};

  // Result returned by Decode().
  struct DecodeResult {
    std::string name;
    absl::StatusOr<std::unique_ptr<ResourceData>> resource;
  };

  virtual ~XdsResourceType() = default;

  // Returns v3 resource type.
  virtual absl::string_view type_url() const = 0;

  // Returns v2 resource type.
  virtual absl::string_view v2_type_url() const = 0;

  // Decodes and validates a serialized resource proto.
  // If the resource fails protobuf deserialization, returns non-OK status.
  // If the deserialized resource fails validation, returns a DecodeResult
  // whose resource field is set to a non-OK status.
  // Otherwise, returns a DecodeResult with a valid resource.
  virtual absl::StatusOr<DecodeResult> Decode(
      const XdsEncodingContext& context, absl::string_view serialized_resource,
      bool is_v2) const = 0;

  // Convenient method for checking if a resource type matches this type.
  bool IsType(absl::string_view resource_type, bool* is_v2) const {
    if (resource_type == type_url()) return true;
    if (resource_type == v2_type_url()) {
      if (is_v2 != nullptr) *is_v2 = true;
      return true;
    }
    return false;
  }
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H
