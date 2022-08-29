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

#ifndef GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H
#define GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H
#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "upb/arena.h"
#include "upb/def.h"

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/lib/debug/trace.h"

namespace grpc_core {

// Interface for an xDS resource type.
// Used to inject type-specific logic into XdsClient.
class XdsResourceType {
 public:
  // Context passed into Decode().
  struct DecodeContext {
    XdsClient* client;
    const XdsBootstrap::XdsServer& server;
    TraceFlag* tracer;
    upb_DefPool* symtab;
    upb_Arena* arena;
  };

  // A base type for resource data.
  // Subclasses will extend this, and their DecodeResults will be
  // downcastable to their extended type.
  struct ResourceData {
    virtual ~ResourceData() = default;
  };

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
      const DecodeContext& context, absl::string_view serialized_resource,
      bool is_v2) const = 0;

  // Returns true if r1 and r2 are equal.
  // Must be invoked only on resources returned by this object's Decode()
  // method.
  virtual bool ResourcesEqual(const ResourceData* r1,
                              const ResourceData* r2) const = 0;

  // Returns a copy of resource.
  // Must be invoked only on resources returned by this object's Decode()
  // method.
  virtual std::unique_ptr<ResourceData> CopyResource(
      const ResourceData* resource) const = 0;

  // Indicates whether the resource type requires that all resources must
  // be present in every SotW response from the server.  If true, a
  // response that does not include a previously seen resource will be
  // interpreted as a deletion of that resource.
  virtual bool AllResourcesRequiredInSotW() const { return false; }

  // Populate upb symtab with xDS proto messages that we want to print
  // properly in logs.
  // Note: This won't actually work properly until upb adds support for
  // Any fields in textproto printing (internal b/178821188).
  virtual void InitUpbSymtab(upb_DefPool* symtab) const = 0;

  // Convenience method for checking if resource_type matches this type.
  // Checks against both type_url() and v2_type_url().
  // If is_v2 is non-null, it will be set to true if matching v2_type_url().
  bool IsType(absl::string_view resource_type, bool* is_v2) const;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_RESOURCE_TYPE_H
