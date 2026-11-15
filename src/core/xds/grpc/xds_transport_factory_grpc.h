//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_TRANSPORT_FACTORY_GRPC_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_TRANSPORT_FACTORY_GRPC_H

#include <grpc/channel_factory.h>

#include <memory>
#include <string>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/xds/grpc/certificate_provider_store_interface.h"
#include "src/core/xds/grpc/xds_server_grpc_interface.h"
#include "src/core/xds/grpc/xds_transport_grpc.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "src/core/xds/xds_client/xds_transport_factory.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class GrpcXdsTransportFactory final : public XdsTransportFactory {
 public:
  GrpcXdsTransportFactory(const ChannelArgs& args,
                          RefCountedPtr<CertificateProviderStoreInterface>
                              certificate_provider_store);
  ~GrpcXdsTransportFactory() override;

  void Orphaned() override {}

  RefCountedPtr<XdsTransport> GetTransport(
      const XdsBootstrap::XdsServerTarget& server,
      absl::Status* status) override;

  grpc_pollset_set* interested_parties() const { return interested_parties_; }

 private:
  class XdsSharedChannel;

  ChannelArgs args_;
  RefCountedPtr<CertificateProviderStoreInterface> certificate_provider_store_;
  grpc_pollset_set* interested_parties_;

  Mutex mu_;
  absl::flat_hash_map<std::string /*XdsServerTarget key*/, GrpcXdsTransport*>
      transports_ ABSL_GUARDED_BY(&mu_);
  absl::flat_hash_map<std::string /*Channel key*/, XdsSharedChannel*> channels_
      ABSL_GUARDED_BY(&mu_);
};

// A ChannelFactory for the xDS case.  Only the keys in the map are
// supported: an unknown key gets a lame channel.  Transports come from
// the XdsClient's existing GrpcXdsTransportFactory, so channels are shared
// with other xDS users.
class XdsTransportFactoryWrapper final : public experimental::ChannelFactory {
 public:
  using TargetMap =
      absl::flat_hash_map<std::string /*key*/,
                          std::shared_ptr<const GrpcXdsServerInterface>>;

  XdsTransportFactoryWrapper(
      RefCountedPtr<GrpcXdsTransportFactory> transport_factory,
      TargetMap targets);

  std::unique_ptr<ChannelHandle> CreateChannel(absl::string_view key) override;

 private:
  RefCountedPtr<GrpcXdsTransportFactory> transport_factory_;
  // Does not change after construction, so no lock is needed.
  const TargetMap targets_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_TRANSPORT_FACTORY_GRPC_H
