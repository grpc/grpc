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

#include "src/core/xds/grpc/xds_transport_factory_grpc.h"

#include <grpc/channel_factory.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>

#include <memory>
#include <string>
#include <utility>

#include "src/core/client_channel/channel_factory.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/call/composite/composite_call_credentials.h"
#include "src/core/credentials/transport/channel_creds_registry.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/init_internally.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/certificate_provider_store_interface.h"
#include "src/core/xds/grpc/xds_server_grpc_interface.h"
#include "src/core/xds/grpc/xds_transport_grpc.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

//
// GrpcXdsTransportFactory::XdsSharedChannel
//

namespace {

RefCountedPtr<Channel> CreateXdsChannel(
    const ChannelArgs& args,
    CertificateProviderStoreInterface& certificate_provider_store,
    const GrpcXdsServerInterface& server) {
  RefCountedPtr<grpc_channel_credentials> channel_creds =
      CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
          server.channel_creds_config(), certificate_provider_store);
  ChannelArgs channel_args = args;
  const grpc_channel_args* child_args =
      args.GetPointer<grpc_channel_args>(GRPC_ARG_CHILD_CHANNEL_ARGS);
  if (child_args != nullptr) {
    channel_args = ChannelArgs::FromC(child_args).UnionWith(args);
  }
  return RefCountedPtr<Channel>(Channel::FromC(
      grpc_channel_create(server.server_uri().c_str(), channel_creds.get(),
                          channel_args.ToC().get())));
}

std::string GetChannelKey(const GrpcXdsServerInterface& server) {
  std::string result = "{server_uri=";
  absl::StrAppend(&result, server.server_uri());
  if (server.channel_creds_config() != nullptr) {
    absl::StrAppend(
        &result,
        ", channel_creds={type=", server.channel_creds_config()->type(),
        ", config=", server.channel_creds_config()->ToString(), "}");
  }
  absl::StrAppend(&result, "}");
  return result;
}

RefCountedPtr<grpc_call_credentials> GetCallCredsForTransport(
    const GrpcXdsServerInterface& server) {
  RefCountedPtr<grpc_call_credentials> call_creds;
  for (const auto& call_creds_config : server.call_creds_configs()) {
    RefCountedPtr<grpc_call_credentials> creds =
        CoreConfiguration::Get().call_creds_registry().CreateCallCreds(
            call_creds_config);
    if (call_creds == nullptr) {
      call_creds = std::move(creds);
    } else {
      call_creds = MakeRefCounted<grpc_composite_call_credentials>(
          std::move(call_creds), std::move(creds));
    }
  }
  return call_creds;
}

}  // namespace

// SharedChannel implementation for GrpcXdsTransportFactory.  Holds a weak
// ref to the factory, which keeps the factory's pollset_set alive and lets
// this object remove entries from the factory's caches.
class GrpcXdsTransportFactory::XdsSharedChannel final
    : public GrpcXdsTransport::SharedChannel {
 public:
  XdsSharedChannel(std::string key, RefCountedPtr<Channel> channel,
                   WeakRefCountedPtr<GrpcXdsTransportFactory> factory)
      : key_(std::move(key)),
        channel_(std::move(channel)),
        factory_(std::move(factory)) {}

  ~XdsSharedChannel() override {
    MutexLock lock(factory_->mu_);
    auto it = factory_->channels_.find(key_);
    if (it != factory_->channels_.end() && it->second == this) {
      factory_->channels_.erase(it);
    }
  }

  Channel* channel() const override { return channel_.get(); }

  grpc_pollset_set* interested_parties() const override {
    return factory_->interested_parties();
  }

  void OnTransportOrphaned(absl::string_view key,
                           GrpcXdsTransport* transport) override {
    MutexLock lock(factory_->mu_);
    auto it = factory_->transports_.find(key);
    if (it != factory_->transports_.end() && it->second == transport) {
      factory_->transports_.erase(it);
    }
  }

 private:
  std::string key_;
  RefCountedPtr<Channel> channel_;
  WeakRefCountedPtr<GrpcXdsTransportFactory> factory_;
};

//
// GrpcXdsTransportFactory
//

GrpcXdsTransportFactory::GrpcXdsTransportFactory(
    const ChannelArgs& args,
    RefCountedPtr<CertificateProviderStoreInterface> certificate_provider_store)
    : args_(
          args.Set(GRPC_ARG_KEEPALIVE_TIME_MS, Duration::Minutes(5).millis())),
      certificate_provider_store_(std::move(certificate_provider_store)),
      interested_parties_(grpc_pollset_set_create()) {
  // Calling grpc_init to ensure gRPC does not shut down until the XdsClient is
  // destroyed.
  InitInternally();
}

GrpcXdsTransportFactory::~GrpcXdsTransportFactory() {
  grpc_pollset_set_destroy(interested_parties_);
  // Calling grpc_shutdown to ensure gRPC does not shut down until the XdsClient
  // is destroyed.
  ShutdownInternally();
}

RefCountedPtr<XdsTransport> GrpcXdsTransportFactory::GetTransport(
    const XdsBootstrap::XdsServerTarget& server, absl::Status* status) {
  std::string key = server.Key();
  RefCountedPtr<GrpcXdsTransport> transport;
  MutexLock lock(mu_);
  auto it = transports_.find(key);
  if (it != transports_.end()) {
    transport = it->second->RefIfNonZero().TakeAsSubclass<GrpcXdsTransport>();
  }
  if (transport == nullptr) {
    const auto& grpc_server = DownCast<const GrpcXdsServerInterface&>(server);
    std::string channel_key = GetChannelKey(grpc_server);
    auto channel_it = channels_.find(channel_key);
    RefCountedPtr<XdsSharedChannel> channel;
    if (channel_it != channels_.end()) {
      GRPC_TRACE_LOG(xds_client, INFO) << "[GrpcXdsTransportFactory " << this
                                       << "] found cached SharedChannel";
      channel =
          channel_it->second->RefIfNonZero().TakeAsSubclass<XdsSharedChannel>();
    }
    if (channel == nullptr) {
      RefCountedPtr<Channel> raw_channel =
          CreateXdsChannel(args_, *certificate_provider_store_, grpc_server);
      GRPC_CHECK(raw_channel != nullptr);
      channel = MakeRefCounted<XdsSharedChannel>(
          channel_key, std::move(raw_channel),
          WeakRefAsSubclass<GrpcXdsTransportFactory>());
      channels_[channel_key] = channel.get();
    }
    if (channel->channel()->IsLame()) {
      *status = absl::UnavailableError("xds client has a lame channel");
    }
    transport = MakeRefCounted<GrpcXdsTransport>(
        key, std::move(channel), GetCallCredsForTransport(grpc_server),
        grpc_server.initial_metadata(), grpc_server.timeout());
    transports_[std::move(key)] = transport.get();
  }
  return transport;
}

//
// XdsTransportFactoryWrapper
//

XdsTransportFactoryWrapper::XdsTransportFactoryWrapper(
    RefCountedPtr<GrpcXdsTransportFactory> transport_factory, TargetMap targets)
    : transport_factory_(std::move(transport_factory)),
      targets_(std::move(targets)) {}

std::unique_ptr<experimental::ChannelFactory::ChannelHandle>
XdsTransportFactoryWrapper::CreateChannel(absl::string_view key) {
  auto it = targets_.find(key);
  if (it == targets_.end() || it->second == nullptr) {
    return CreateLameChannel(key, absl::UnavailableError(absl::StrCat(
                                      "channel key not allowed: ", key)));
  }
  absl::Status status;
  RefCountedPtr<XdsTransport> transport =
      transport_factory_->GetTransport(*it->second, &status);
  GRPC_CHECK(transport != nullptr);
  return std::make_unique<ChannelHandleImpl>(std::move(transport));
}

}  // namespace grpc_core
