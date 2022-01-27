//
// Copyright 2022 gRPC authors.
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

#ifndef GRPC_CORE_EXT_XDS_XDS_CHANNEL_CREDS_H
#define GRPC_CORE_EXT_XDS_XDS_CHANNEL_CREDS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/credentials.h"

namespace grpc_core {

class XdsChannelCredsImpl {
 public:
  virtual ~XdsChannelCredsImpl() {}
  virtual absl::string_view creds_type() const = 0;
  virtual bool IsValidConfig(const Json& config) const = 0;
  virtual RefCountedPtr<grpc_channel_credentials> CreateXdsChannelCreds(
      const Json& config) const = 0;
};

class XdsChannelCredsRegistry {
 public:
  static bool IsSupported(const std::string& creds_type);
  static bool IsValidConfig(const std::string& creds_type, const Json& config);
  static RefCountedPtr<grpc_channel_credentials> CreateXdsChannelCreds(
      const std::string& creds_type, const Json& config);
  static void Init();
  static void Shutdown();
  static void RegisterXdsChannelCreds(
      std::unique_ptr<XdsChannelCredsImpl> creds);
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_CHANNEL_CREDS_H
