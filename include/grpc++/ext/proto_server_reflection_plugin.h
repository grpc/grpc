/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPCXX_EXT_PROTO_SERVER_REFLECTION_PLUGIN_H
#define GRPCXX_EXT_PROTO_SERVER_REFLECTION_PLUGIN_H

#include <grpc++/impl/server_builder_plugin.h>
#include <grpc++/support/config.h>

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

// Add proto reflection plugin to ServerBuilder. This function should be called
// at the static initialization time.
void InitProtoReflectionServerBuilderPlugin();

}  // namespace reflection
}  // namespace grpc

#endif  // GRPCXX_EXT_PROTO_SERVER_REFLECTION_PLUGIN_H
