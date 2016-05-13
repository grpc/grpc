/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPCXX_IMPL_SERVER_BUILDER_PLUGIN_H
#define GRPCXX_IMPL_SERVER_BUILDER_PLUGIN_H

#include <memory>

#include <grpc++/support/config.h>

namespace grpc {

class ServerInitializer;

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

  virtual bool has_sync_methods() const { return false; }
  virtual bool has_async_methods() const { return false; }
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_SERVER_BUILDER_PLUGIN_H
