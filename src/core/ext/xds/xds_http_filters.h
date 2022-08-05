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

#ifndef GRPC_CORE_EXT_XDS_XDS_HTTP_FILTERS_H
#define GRPC_CORE_EXT_XDS_XDS_HTTP_FILTERS_H

#include <grpc/support/port_platform.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "upb/arena.h"
#include "upb/def.h"
#include "upb/upb.h"

#include "src/core/lib/json/json.h"

namespace grpc_core {

class XdsHttpFilter {
 public:
  struct FilterConfig {
    absl::string_view config_proto_type_name;
    Json config;

    bool operator==(const FilterConfig& other) const {
      return config_proto_type_name == other.config_proto_type_name &&
             config == other.config;
    }
    std::string ToString() const {
      return absl::StrCat("{config_proto_type_name=", config_proto_type_name,
                          " config=", config.Dump(), "}");
    }
  };

  virtual ~XdsHttpFilter() = default;

  // Returns the xDS proto type for the primary config.
  virtual absl::string_view ConfigProtoType() const = 0;

  // Returns the xDS proto type for the override config, or the empty
  // string if the filter does not support an override config.
  virtual absl::string_view OverrideConfigProtoType() const = 0;

  // Loads the proto message into the upb symtab.
  virtual void PopulateSymtab(upb_DefPool* symtab) const = 0;

  // Generates a Config from the xDS filter config proto.
  // Used for the top-level config in the HCM HTTP filter list.
  virtual absl::StatusOr<FilterConfig> GenerateFilterConfig(
      upb_StringView serialized_filter_config, upb_Arena* arena) const = 0;

  // Generates a Config from the xDS filter config proto.
  // Used for the typed_per_filter_config override in VirtualHost and Route.
  virtual absl::StatusOr<FilterConfig> GenerateFilterConfigOverride(
      upb_StringView serialized_filter_config, upb_Arena* arena) const = 0;

  // Returns true if the filter is supported on clients; false otherwise
  virtual bool IsSupportedOnClients() const = 0;

  // Returns true if the filter is supported on servers; false otherwise
  virtual bool IsSupportedOnServers() const = 0;

  // Returns true if the filter must be the last filter in the chain.
  virtual bool IsTerminalFilter() const { return false; }
};

class XdsHttpRouterFilter : public XdsHttpFilter {
 public:
  absl::string_view ConfigProtoType() const override;
  absl::string_view OverrideConfigProtoType() const override;
  void PopulateSymtab(upb_DefPool* symtab) const override;
  absl::StatusOr<FilterConfig> GenerateFilterConfig(
      upb_StringView serialized_filter_config, upb_Arena* arena) const override;
  absl::StatusOr<FilterConfig> GenerateFilterConfigOverride(
      upb_StringView /*serialized_filter_config*/,
      upb_Arena* /*arena*/) const override;
  bool IsSupportedOnClients() const override { return true; }
  bool IsSupportedOnServers() const override { return true; }
  bool IsTerminalFilter() const override { return true; }
};

class XdsHttpFilterRegistry {
 public:
  void RegisterFilter(std::unique_ptr<XdsHttpFilter> filter);

  const XdsHttpFilter* GetFilterForType(
      absl::string_view proto_type_name) const;

  void PopulateSymtab(upb_DefPool* symtab) const;

 private:
  std::vector<std::unique_ptr<XdsHttpFilter>> filters_;
  std::map<absl::string_view, XdsHttpFilter*> filter_registry_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_HTTP_FILTERS_H
