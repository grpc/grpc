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

#include <iostream>
#include <memory>
#include <string>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <grpc++/grpc++.h>

#include "src/cpp/plugin/reflection/proto_server_reflection.h"
#include "src/cpp/plugin/reflection/reflection.grpc.pb.h"

using grpc::Status;
using grpc::StatusCode;
using google::protobuf::MethodDescriptor;
using google::protobuf::ServiceDescriptor;
using google::protobuf::Descriptor;
using google::protobuf::EnumDescriptor;
using google::protobuf::EnumValueDescriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::DescriptorPool;
using grpc::reflection::v1::ListServiceRequest;
using grpc::reflection::v1::ListServiceResponse;
using grpc::reflection::v1::GetDescriptorRequest;
using grpc::reflection::v1::GetMethodResponse;
using grpc::reflection::v1::GetServiceResponse;
using grpc::reflection::v1::GetMessageTypeResponse;
using grpc::reflection::v1::GetEnumTypeResponse;
using grpc::reflection::v1::GetEnumValueResponse;
using grpc::reflection::v1::GetExtensionResponse;

namespace grpc {

ProtoServerReflection::ProtoServerReflection()
    : descriptor_pool_(DescriptorPool::generated_pool()) {}

ProtoServerReflection::ProtoServerReflection(const Server* server)
    : server_(server) {}

void ProtoServerReflection::SetServer(const Server* server) {
  server_ = server;
}

void ProtoServerReflection::SetSeviceList(
    const std::vector<grpc::string>* services) {
  services_ = services;
}

Status ProtoServerReflection::ListService(ServerContext* context,
                                          const ListServiceRequest* request,
                                          ListServiceResponse* response) {
  if (services_ == nullptr) {
    return Status(StatusCode::NOT_FOUND, "Services not found.");
  }
  for (auto it = services_->begin(); it != services_->end(); ++it) {
    response->add_services(*it);
  }
  return Status::OK;
}

Status ProtoServerReflection::GetMethod(ServerContext* context,
                                        const GetDescriptorRequest* request,
                                        GetMethodResponse* response) {
  assert(descriptor_pool_ != nullptr);
  const MethodDescriptor* method_descriptor =
      descriptor_pool_->FindMethodByName(request->name());
  if (!method_descriptor) {
    return Status(StatusCode::NOT_FOUND, "Method not found.");
  }
  method_descriptor->CopyTo(response->mutable_method());
  return Status::OK;
}

Status ProtoServerReflection::GetService(ServerContext* context,
                                         const GetDescriptorRequest* request,
                                         GetServiceResponse* response) {
  assert(descriptor_pool_ != nullptr);
  const ServiceDescriptor* service_descriptor =
      descriptor_pool_->FindServiceByName(request->name());
  if (!service_descriptor) {
    return Status(StatusCode::NOT_FOUND, "Service not found.");
  }
  service_descriptor->CopyTo(response->mutable_service());
  return Status::OK;
}

Status ProtoServerReflection::GetMessageType(
    ServerContext* context, const GetDescriptorRequest* request,
    GetMessageTypeResponse* response) {
  assert(descriptor_pool_ != nullptr);
  const Descriptor* message_type_descriptor =
      descriptor_pool_->FindMessageTypeByName(request->name());
  if (!message_type_descriptor) {
    return Status(StatusCode::NOT_FOUND, "Message type not found.");
  }
  message_type_descriptor->CopyTo(response->mutable_message_type());
  return Status::OK;
}

Status ProtoServerReflection::GetEnumType(ServerContext* context,
                                          const GetDescriptorRequest* request,
                                          GetEnumTypeResponse* response) {
  assert(descriptor_pool_ != nullptr);
  const EnumDescriptor* enum_type_descriptor =
      descriptor_pool_->FindEnumTypeByName(request->name());
  if (!enum_type_descriptor) {
    return Status(StatusCode::NOT_FOUND, "Enum type not found.");
  }
  enum_type_descriptor->CopyTo(response->mutable_enum_type());
  return Status::OK;
}

Status ProtoServerReflection::GetEnumValue(ServerContext* context,
                                           const GetDescriptorRequest* request,
                                           GetEnumValueResponse* response) {
  assert(descriptor_pool_ != nullptr);
  const EnumValueDescriptor* enum_value_descriptor =
      descriptor_pool_->FindEnumValueByName(request->name());
  if (!enum_value_descriptor) {
    return Status(StatusCode::NOT_FOUND, "Enum value not found.");
  }
  enum_value_descriptor->CopyTo(response->mutable_enum_value());
  return Status::OK;
}

Status ProtoServerReflection::GetExtension(ServerContext* context,
                                           const GetDescriptorRequest* request,
                                           GetExtensionResponse* response) {
  assert(descriptor_pool_ != nullptr);
  const FieldDescriptor* extension_descriptor =
      descriptor_pool_->FindExtensionByName(request->name());
  if (!extension_descriptor) {
    return Status(StatusCode::NOT_FOUND, "Extension not found.");
  }
  extension_descriptor->CopyTo(response->mutable_extension());
  return Status::OK;
}

}  // namespace grpc
