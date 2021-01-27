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

#include "src/core/ext/xds/xds_http_filters.h"

namespace grpc_core {

namespace {

using FilterRegistryMap =
    std::map<absl::string_view, std::unique_ptr<XdsHttpFilterImpl>>;

FilterRegistryMap* g_filter_registry = nullptr;

}  // namespace

XdsHttpFilterImpl* XdsHttpFilterRegistry::GetFilterForType(
    absl::string_view proto_type_name) {
// FIXME: implement
}

void XdsHttpFilterRegistry::RegisterFilter(
    std::unique_ptr<XdsHttpFilterImpl> filter) {
// FIXME: implement
}

void XdsHttpFilterRegistry::Init() {
  g_filter_registry = new FilterRegistryMap;
}

void XdsHttpFilterRegistry::Shutdown() { delete g_filter_registry; }

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_XDS_XDS_HTTP_FILTERS_H */
