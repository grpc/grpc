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

ChannelzServicePlugin::ChannelzServicePlugin()
    : channelz_service_(new grpc::ChannelzService()) {}

grpc::string ChannelzServicePlugin::name() { return "channelz_service"; }

void ChannelzServicePlugin::InitServer(grpc::ServerInitializer* si) {
  si->RegisterService(channelz_service_);
}

void ChannelzServicePlugin::Finish(grpc::ServerInitializer* si) {}

void ChannelzServicePlugin::ChangeArguments(const grpc::string& name,
                                            void* value) {}

bool ChannelzServicePlugin::has_sync_methods() const {
  if (channelz_service_) {
    return channelz_service_->has_synchronous_methods();
  }
  return false;
}

bool ChannelzServicePlugin::has_async_methods() const {
  if (channelz_service_) {
    return channelz_service_->has_async_methods();
  }
  return false;
}

static std::unique_ptr< ::grpc::ServerBuilderPlugin>
CreateChannelzServicePlugin() {
  return std::unique_ptr< ::grpc::ServerBuilderPlugin>(
      new ChannelzServicePlugin());
}

void InitChannelzServiceBuilderPlugin() {
  static bool already_here = false;
  if (already_here) return;
  already_here = true;
  ::grpc::ServerBuilder::InternalAddPluginFactory(&CreateChannelzServicePlugin);
}

// Force InitChannelzServiceBuilderPlugin() to be called at static
// initialization time.
struct StaticChannelServicePluginInitializer {
  StaticChannelServicePluginInitializer() {
    InitChannelzServiceBuilderPlugin();
  }
} static_channelz_service_plugin_initializer;

}  // namespace experimental
}  // namespace channelz
}  // namespace grpc
