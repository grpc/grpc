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

#ifndef GRPCPP_EXT_PROTO_SERVER_REFLECTION_PLUGIN_H
#define GRPCPP_EXT_PROTO_SERVER_REFLECTION_PLUGIN_H

#include <grpcpp/impl/server_builder_plugin.h>
#include <grpcpp/support/config.h>

namespace grpc {
class ServerInitializer;
class ProtoServerReflection;
}  // namespace grpc

namespace grpc {
namespace reflection {

class ProtoServerReflectionPlugin : public ::grpc::ServerBuilderPlugin {
 public:
  ProtoServerReflectionPlugin();
  ::grpc::string name() override;
  void InitServer(::grpc::ServerInitializer* si) override;
  void Finish(::grpc::ServerInitializer* si) override;
  void ChangeArguments(const ::grpc::string& name, void* value) override;
  bool has_async_methods() const override;
  bool has_sync_methods() const override;

 private:
  std::shared_ptr<grpc::ProtoServerReflection> reflection_service_;
};

/// Add proto reflection plugin to \a ServerBuilder.
/// This function should be called at the static initialization time.
void InitProtoReflectionServerBuilderPlugin();

}  // namespace reflection
}  // namespace grpc

#endif  // GRPCPP_EXT_PROTO_SERVER_REFLECTION_PLUGIN_H
