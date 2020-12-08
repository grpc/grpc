/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <grpcpp/ext/channelz_service_plugin.h>
#include <grpcpp/impl/server_builder_plugin.h>
#include <grpcpp/impl/server_initializer.h>
#include <grpcpp/server.h>

#include "src/cpp/server/channelz/channelz_service.h"

namespace grpc {
namespace channelz {
namespace experimental {

class ChannelzServicePlugin : public ::grpc::ServerBuilderPlugin {
 public:
  ChannelzServicePlugin() : channelz_service_(new grpc::ChannelzService()) {}

  std::string name() override { return "channelz_service"; }

  void InitServer(grpc::ServerInitializer* si) override {
    si->RegisterService(channelz_service_);
  }

  void Finish(grpc::ServerInitializer* /*si*/) override {}

  void ChangeArguments(const std::string& /*name*/, void* /*value*/) override {}

  bool has_sync_methods() const override {
    if (channelz_service_) {
      return channelz_service_->has_synchronous_methods();
    }
    return false;
  }

  bool has_async_methods() const override {
    if (channelz_service_) {
      return channelz_service_->has_async_methods();
    }
    return false;
  }

 private:
  std::shared_ptr<grpc::ChannelzService> channelz_service_;
};

static std::unique_ptr< ::grpc::ServerBuilderPlugin>
CreateChannelzServicePlugin() {
  return std::unique_ptr< ::grpc::ServerBuilderPlugin>(
      new ChannelzServicePlugin());
}

void InitChannelzService() {
  static struct Initializer {
    Initializer() {
      ::grpc::ServerBuilder::InternalAddPluginFactory(
          &grpc::channelz::experimental::CreateChannelzServicePlugin);
    }
  } initialize;
}

}  // namespace experimental
}  // namespace channelz
}  // namespace grpc
