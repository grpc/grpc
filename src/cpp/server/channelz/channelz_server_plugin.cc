/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpcpp/ext/channelz_server_plugin.h>
#include <grpcpp/impl/server_builder_plugin.h>
#include <grpcpp/impl/server_initializer.h>
#include <grpcpp/server.h>

#include "src/cpp/server/channelz/channelz_server.h"

namespace grpc {
namespace channelz {

ChannelzServerPlugin::ChannelzServerPlugin()
    : channelz_server_(new grpc::ChannelzServer()) {}

grpc::string ChannelzServerPlugin::name() { return "channelz_server"; }

void ChannelzServerPlugin::InitServer(grpc::ServerInitializer* si) {
  si->RegisterService(channelz_server_);
}

void ChannelzServerPlugin::Finish(grpc::ServerInitializer* si) {}

void ChannelzServerPlugin::ChangeArguments(const grpc::string& name,
                                           void* value) {}

bool ChannelzServerPlugin::has_sync_methods() const {
  if (channelz_server_) {
    return channelz_server_->has_synchronous_methods();
  }
  return false;
}

bool ChannelzServerPlugin::has_async_methods() const {
  if (channelz_server_) {
    return channelz_server_->has_async_methods();
  }
  return false;
}

static std::unique_ptr< ::grpc::ServerBuilderPlugin>
CreateChannelzServerPlugin() {
  return std::unique_ptr< ::grpc::ServerBuilderPlugin>(
      new ChannelzServerPlugin());
}

void InitChannelzServerBuilderPlugin() {
  static bool already_here = false;
  if (already_here) return;
  already_here = true;
  ::grpc::ServerBuilder::InternalAddPluginFactory(&CreateChannelzServerPlugin);
}

// Force InitChannelzServerBuilderPlugin() to be called at static
// initialization time.
struct StaticChannelServerPluginInitializer {
  StaticChannelServerPluginInitializer() { InitChannelzServerBuilderPlugin(); }
} static_channelz_server_plugin_initializer;

}  // namespace channelz
}  // namespace grpc
