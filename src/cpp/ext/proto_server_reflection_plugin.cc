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

#include <grpc++/ext/proto_server_reflection_plugin.h>
#include <grpc++/impl/server_builder_plugin.h>
#include <grpc++/impl/server_initializer.h>
#include <grpc++/server.h>

#include "src/cpp/ext/proto_server_reflection.h"

namespace grpc {
namespace reflection {

ProtoServerReflectionPlugin::ProtoServerReflectionPlugin()
    : reflection_service_(new grpc::ProtoServerReflection()) {}

grpc::string ProtoServerReflectionPlugin::name() {
  return "proto_server_reflection";
}

void ProtoServerReflectionPlugin::InitServer(grpc::ServerInitializer* si) {
  si->RegisterService(reflection_service_);
}

void ProtoServerReflectionPlugin::Finish(grpc::ServerInitializer* si) {
  reflection_service_->SetServiceList(si->GetServiceList());
}

void ProtoServerReflectionPlugin::ChangeArguments(const grpc::string& name,
                                                  void* value) {}

bool ProtoServerReflectionPlugin::has_sync_methods() const {
  if (reflection_service_) {
    return reflection_service_->has_synchronous_methods();
  }
  return false;
}

bool ProtoServerReflectionPlugin::has_async_methods() const {
  if (reflection_service_) {
    return reflection_service_->has_async_methods();
  }
  return false;
}

static std::unique_ptr< ::grpc::ServerBuilderPlugin> CreateProtoReflection() {
  return std::unique_ptr< ::grpc::ServerBuilderPlugin>(
      new ProtoServerReflectionPlugin());
}

void InitProtoReflectionServerBuilderPlugin() {
  static bool already_here = false;
  if (already_here) return;
  already_here = true;
  ::grpc::ServerBuilder::InternalAddPluginFactory(&CreateProtoReflection);
}

// Force InitProtoReflectionServerBuilderPlugin() to be called at static
// initialization time.
struct StaticProtoReflectionPluginInitializer {
  StaticProtoReflectionPluginInitializer() {
    InitProtoReflectionServerBuilderPlugin();
  }
} static_proto_reflection_plugin_initializer;

}  // namespace reflection
}  // namespace grpc
