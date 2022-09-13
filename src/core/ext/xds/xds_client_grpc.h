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

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/ext/xds/certificate_provider_store.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"

namespace grpc_core {

class GrpcXdsClient : public XdsClient {
 public:
  // Factory function to get or create the global XdsClient instance.
  static absl::StatusOr<RefCountedPtr<GrpcXdsClient>> GetOrCreate(
      const ChannelArgs& args, const char* reason);

  // Do not instantiate directly -- use GetOrCreate() instead.
  GrpcXdsClient(std::unique_ptr<GrpcXdsBootstrap> bootstrap,
                const ChannelArgs& args);
  ~GrpcXdsClient() override;

  // Helpers for encoding the XdsClient object in channel args.
  static absl::string_view ChannelArgName() {
    return "grpc.internal.xds_client";
  }
  static int ChannelArgsCompare(const XdsClient* a, const XdsClient* b) {
    return QsortCompare(a, b);
  }

  grpc_pollset_set* interested_parties() const;

  CertificateProviderStore& certificate_provider_store() const {
    return *certificate_provider_store_;
  }

 private:
  OrphanablePtr<CertificateProviderStore> certificate_provider_store_;
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
