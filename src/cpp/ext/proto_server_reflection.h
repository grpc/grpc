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

#ifndef GRPC_INTERNAL_CPP_EXT_PROTO_SERVER_REFLECTION_H
#define GRPC_INTERNAL_CPP_EXT_PROTO_SERVER_REFLECTION_H

#include <unordered_set>
#include <vector>

#include <grpc++/grpc++.h>
#include "src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h"

namespace grpc {

class ProtoServerReflection final
    : public reflection::v1alpha::ServerReflection::Service {
 public:
  ProtoServerReflection();

  // Add the full names of registered services
  void SetServiceList(const std::vector<grpc::string>* services);

  // implementation of ServerReflectionInfo(stream ServerReflectionRequest) rpc
  // in ServerReflection service
  Status ServerReflectionInfo(
      ServerContext* context,
      ServerReaderWriter<reflection::v1alpha::ServerReflectionResponse,
                         reflection::v1alpha::ServerReflectionRequest>* stream)
      override;

 private:
  Status ListService(ServerContext* context,
                     reflection::v1alpha::ListServiceResponse* response);

  Status GetFileByName(ServerContext* context, const grpc::string& file_name,
                       reflection::v1alpha::ServerReflectionResponse* response);

  Status GetFileContainingSymbol(
      ServerContext* context, const grpc::string& symbol,
      reflection::v1alpha::ServerReflectionResponse* response);

  Status GetFileContainingExtension(
      ServerContext* context,
      const reflection::v1alpha::ExtensionRequest* request,
      reflection::v1alpha::ServerReflectionResponse* response);

  Status GetAllExtensionNumbers(
      ServerContext* context, const grpc::string& type,
      reflection::v1alpha::ExtensionNumberResponse* response);

  void FillFileDescriptorResponse(
      const protobuf::FileDescriptor* file_desc,
      reflection::v1alpha::ServerReflectionResponse* response,
      std::unordered_set<grpc::string>* seen_files);

  void FillErrorResponse(const Status& status,
                         reflection::v1alpha::ErrorResponse* error_response);

  const protobuf::DescriptorPool* descriptor_pool_;
  const std::vector<string>* services_;
};

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_EXT_PROTO_SERVER_REFLECTION_H
