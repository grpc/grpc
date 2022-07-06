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

#ifndef GRPC_CORE_EXT_XDS_XDS_CLIENT_GRPC_H
#define GRPC_CORE_EXT_XDS_XDS_CLIENT_GRPC_H

#include <grpc/support/port_platform.h>

#include <memory>

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"

namespace grpc_core {

class GrpcXdsClient : public XdsClient {
 public:
  // Factory function to get or create the global XdsClient instance.
  // If *error is not GRPC_ERROR_NONE upon return, then there was
  // an error initializing the client.
  static RefCountedPtr<XdsClient> GetOrCreate(const grpc_channel_args* args,
                                              grpc_error_handle* error);

  // Do not instantiate directly -- use GetOrCreate() instead.
  GrpcXdsClient(std::unique_ptr<XdsBootstrap> bootstrap,
                const grpc_channel_args* args);
  ~GrpcXdsClient() override;

  grpc_pollset_set* interested_parties() const;

  // Helpers for encoding the XdsClient object in channel args.
  grpc_arg MakeChannelArg() const;
  static RefCountedPtr<XdsClient> GetFromChannelArgs(
      const grpc_channel_args& args);
};

namespace internal {
void SetXdsChannelArgsForTest(grpc_channel_args* args);
void UnsetGlobalXdsClientForTest();
// Sets bootstrap config to be used when no env var is set.
// Does not take ownership of config.
void SetXdsFallbackBootstrapConfig(const char* config);
}  // namespace internal

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_CLIENT_GRPC_H
