/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPCXX_IMPL_SERVER_BUILDER_PLUGIN_H
#define GRPCXX_IMPL_SERVER_BUILDER_PLUGIN_H

#include <memory>

#include <grpc++/support/config.h>

namespace grpc {

class ServerInitializer;
class ChannelArguments;

class ServerBuilderPlugin {
 public:
  virtual ~ServerBuilderPlugin() {}
  virtual grpc::string name() = 0;

  // InitServer will be called in ServerBuilder::BuildAndStart(), after the
  // Server instance is created.
  virtual void InitServer(ServerInitializer* si) = 0;

  // Finish will be called at the end of ServerBuilder::BuildAndStart().
  virtual void Finish(ServerInitializer* si) = 0;

  // ChangeArguments is an interface that can be used in
  // ServerBuilderOption::UpdatePlugins
  virtual void ChangeArguments(const grpc::string& name, void* value) = 0;

  // UpdateChannelArguments will be called in ServerBuilder::BuildAndStart(),
  // before the Server instance is created.
  virtual void UpdateChannelArguments(ChannelArguments* args) {}

  virtual bool has_sync_methods() const { return false; }
  virtual bool has_async_methods() const { return false; }
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_SERVER_BUILDER_PLUGIN_H
