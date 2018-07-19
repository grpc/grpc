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

#ifndef GRPCPP_EXT_CHANNELZ_SERVICE_PLUGIN_H
#define GRPCPP_EXT_CHANNELZ_SERVICE_PLUGIN_H

#include <grpc/support/port_platform.h>

#include <grpcpp/impl/server_builder_plugin.h>
#include <grpcpp/impl/server_initializer.h>
#include <grpcpp/support/config.h>

#include "src/cpp/server/channelz/channelz_service.h"

namespace grpc {
namespace channelz {
namespace experimental {

// This plugin is experimental for now. Track progress in
// https://github.com/grpc/grpc/issues/15988.
class ChannelzServicePlugin : public ::grpc::ServerBuilderPlugin {
 public:
  ChannelzServicePlugin();
  ::grpc::string name() override;
  void InitServer(::grpc::ServerInitializer* si) override;
  void Finish(::grpc::ServerInitializer* si) override;
  void ChangeArguments(const ::grpc::string& name, void* value) override;
  bool has_async_methods() const override;
  bool has_sync_methods() const override;

 private:
  std::shared_ptr<grpc::ChannelzService> channelz_service_;
};

/// Add channelz server plugin to \a ServerBuilder. This function should
/// be called at static initialization time.
void InitChannelzServerBuilderPlugin();

}  // namespace experimental
}  // namespace channelz
}  // namespace grpc

#endif  // GRPCPP_EXT_CHANNELZ_SERVICE_PLUGIN_H
