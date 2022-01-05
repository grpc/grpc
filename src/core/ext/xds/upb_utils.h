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

#ifndef GRPC_CORE_EXT_XDS_UPB_UTILS_H
#define GRPC_CORE_EXT_XDS_UPB_UTILS_H

#include <grpc/support/port_platform.h>

#include <string>

#include "absl/strings/string_view.h"
#include "upb/text_encode.h"
#include "upb/upb.h"
#include "upb/upb.hpp"

#include "src/core/ext/xds/certificate_provider_store.h"
#include "src/core/lib/debug/trace.h"

namespace grpc_core {

class XdsClient;

// TODO(roth): Rethink this.  All fields except symtab and arena should come
// from XdsClient, injected into XdsResourceType::Decode() somehow without
// passing through XdsApi code, maybe via the AdsResponseParser.
struct XdsEncodingContext {
  XdsClient* client;  // Used only for logging. Unsafe for dereferencing.
  TraceFlag* tracer;
  upb_symtab* symtab;
  upb_arena* arena;
  bool use_v3;
  const CertificateProviderStore::PluginDefinitionMap*
      certificate_provider_definition_map;
};

// Works for both std::string and absl::string_view.
template <typename T>
inline upb_strview StdStringToUpbString(const T& str) {
  return upb_strview_make(str.data(), str.size());
}

inline absl::string_view UpbStringToAbsl(const upb_strview& str) {
  return absl::string_view(str.data, str.size);
}

inline std::string UpbStringToStdString(const upb_strview& str) {
  return std::string(str.data, str.size);
}

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_UPB_UTILS_H
