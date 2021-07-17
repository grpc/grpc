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

#ifndef GRPC_INTERNAL_CPP_EXT_PROTO_SERVER_REFLECTION_H
#define GRPC_INTERNAL_CPP_EXT_PROTO_SERVER_REFLECTION_H

#include <unordered_set>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h"

namespace grpc {

class ProtoServerReflection final
    : public reflection::v1alpha::ServerReflection::Service {
 public:
  ProtoServerReflection();

  // Add the full names of registered services
  void SetServiceList(const std::vector<std::string>* services);

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

  Status GetFileByName(ServerContext* context, const std::string& file_name,
                       reflection::v1alpha::ServerReflectionResponse* response);

  Status GetFileContainingSymbol(
      ServerContext* context, const std::string& symbol,
      reflection::v1alpha::ServerReflectionResponse* response);

  Status GetFileContainingExtension(
      ServerContext* context,
      const reflection::v1alpha::ExtensionRequest* request,
      reflection::v1alpha::ServerReflectionResponse* response);

  Status GetAllExtensionNumbers(
      ServerContext* context, const std::string& type,
      reflection::v1alpha::ExtensionNumberResponse* response);

  void FillFileDescriptorResponse(
      const protobuf::FileDescriptor* file_desc,
      reflection::v1alpha::ServerReflectionResponse* response,
      std::unordered_set<std::string>* seen_files);

  void FillErrorResponse(const Status& status,
                         reflection::v1alpha::ErrorResponse* error_response);

  const protobuf::DescriptorPool* descriptor_pool_;
  const std::vector<string>* services_;
};

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_EXT_PROTO_SERVER_REFLECTION_H
