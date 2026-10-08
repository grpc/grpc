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

#include "src/core/client_channel/channel_factory.h"

#include <grpc/channel_factory.h>
#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/credentials/call/call_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/xds_transport_grpc.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {

// SharedChannel implementation for transports that are not created by
// GrpcXdsTransportFactory.  Owns its channel and pollset_set, and is not
// shared with any other transport, so there is no cache to update.
class StandaloneSharedChannel final : public GrpcXdsTransport::SharedChannel {
 public:
  explicit StandaloneSharedChannel(RefCountedPtr<Channel> channel)
      : channel_(std::move(channel)),
        interested_parties_(grpc_pollset_set_create()) {}

  ~StandaloneSharedChannel() override {
    grpc_pollset_set_destroy(interested_parties_);
  }

  Channel* channel() const override { return channel_.get(); }

  grpc_pollset_set* interested_parties() const override {
    return interested_parties_;
  }

  void OnTransportOrphaned(absl::string_view /*key*/,
                           GrpcXdsTransport* /*transport*/) override {}

 private:
  RefCountedPtr<Channel> channel_;
  grpc_pollset_set* interested_parties_;
};

std::unique_ptr<experimental::ChannelFactory::ChannelHandle>
MakeStandaloneChannel(absl::string_view target,
                      RefCountedPtr<Channel> channel) {
  return std::make_unique<ChannelHandleImpl>(MakeRefCounted<GrpcXdsTransport>(
      std::string(target),
      MakeRefCounted<StandaloneSharedChannel>(std::move(channel)),
      /*call_creds=*/nullptr,
      /*initial_metadata=*/std::vector<std::pair<std::string, std::string>>(),
      Duration::Infinity()));
}

}  // namespace

namespace experimental {

std::unique_ptr<ChannelFactory::ChannelHandle>
ChannelFactory::CreateCoreChannel(absl::string_view target,
                                  grpc_channel_credentials* creds) {
  ChannelArgs channel_args = ChannelArgs().Set(GRPC_ARG_KEEPALIVE_TIME_MS,
                                               Duration::Minutes(5).millis());
  RefCountedPtr<Channel> channel(Channel::FromC(grpc_channel_create(
      std::string(target).c_str(), creds, channel_args.ToC().get())));
  return MakeStandaloneChannel(target, std::move(channel));
}

std::unique_ptr<ChannelFactory::ChannelHandle>
ChannelFactory::CreateLameChannel(absl::string_view target,
                                  absl::Status status) {
  // A lame channel must fail RPCs, so an OK status is a caller bug.
  GRPC_CHECK(!status.ok());
  RefCountedPtr<Channel> channel(Channel::FromC(grpc_lame_client_channel_create(
      std::string(target).c_str(), static_cast<grpc_status_code>(status.code()),
      std::string(status.message()).c_str())));
  return MakeStandaloneChannel(target, std::move(channel));
}

const grpc_arg_pointer_vtable* ChannelFactory::ChannelArgVtable() {
  return ChannelArgTypeTraits<std::shared_ptr<ChannelFactory>>::VTable();
}

}  // namespace experimental

std::shared_ptr<experimental::ChannelFactory> GetChannelFactoryFromChannelArgs(
    const ChannelArgs& args) {
  auto* factory = static_cast<std::shared_ptr<experimental::ChannelFactory>*>(
      args.GetVoidPointer(GRPC_ARG_CHANNEL_FACTORY));
  if (factory == nullptr) return nullptr;
  return *factory;
}

}  // namespace grpc_core
