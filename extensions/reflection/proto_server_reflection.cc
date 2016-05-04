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
#include <vector>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <grpc++/grpc++.h>

#include "reflection/proto_server_reflection.h"

using grpc::Status;
using grpc::StatusCode;
using google::protobuf::MethodDescriptor;
using google::protobuf::ServiceDescriptor;
using google::protobuf::Descriptor;
using google::protobuf::FileDescriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::DescriptorPool;
using google::protobuf::FileDescriptorProto;
using grpc::reflection::v1alpha::EmptyRequest;
using grpc::reflection::v1alpha::ListServiceResponse;
using grpc::reflection::v1alpha::FileNameRequest;
using grpc::reflection::v1alpha::SymbolRequest;
using grpc::reflection::v1alpha::ExtensionRequest;
using grpc::reflection::v1alpha::TypeRequest;
using grpc::reflection::v1alpha::FileDescriptorProtoResponse;
using grpc::reflection::v1alpha::ExtensionNumberResponse;

namespace grpc {

ProtoServerReflection::ProtoServerReflection()
    : descriptor_pool_(DescriptorPool::generated_pool()) {}

void ProtoServerReflection::SetServiceList(
    const std::vector<grpc::string>* services) {
  services_ = services;
}

Status ProtoServerReflection::ListService(ServerContext* context,
                                          const EmptyRequest* request,
                                          ListServiceResponse* response) {
  if (services_ == nullptr) {
    return Status(StatusCode::NOT_FOUND, "Services not found.");
  }
  for (auto it = services_->begin(); it != services_->end(); ++it) {
    response->add_services(*it);
  }
  return Status::OK;
}

Status ProtoServerReflection::GetFileByName(
    ServerContext* context, const FileNameRequest* request,
    FileDescriptorProtoResponse* response) {
  if (descriptor_pool_ == nullptr) {
    return Status::CANCELLED;
  }

  const FileDescriptor* file_desc =
      descriptor_pool_->FindFileByName(request->filename());
  if (file_desc == nullptr) {
    return Status(StatusCode::NOT_FOUND, "File not found.");
  }
  FillFileDescriptorProtoResponse(file_desc, response);
  // file_desc->CopyTo(response->mutable_file_descriptor_proto());
  return Status::OK;
}

Status ProtoServerReflection::GetFileContainingSymbol(
    ServerContext* context, const SymbolRequest* request,
    FileDescriptorProtoResponse* response) {
  if (descriptor_pool_ == nullptr) {
    return Status::CANCELLED;
  }

  const FileDescriptor* file_desc =
      descriptor_pool_->FindFileContainingSymbol(request->symbol());
  if (file_desc == nullptr) {
    return Status(StatusCode::NOT_FOUND, "Symbol not found.");
  }
  FillFileDescriptorProtoResponse(file_desc, response);
  // file_desc->CopyTo(response->mutable_file_descriptor_proto());
  return Status::OK;
}

Status ProtoServerReflection::GetFileContainingExtension(
    ServerContext* context, const ExtensionRequest* request,
    FileDescriptorProtoResponse* response) {
  if (descriptor_pool_ == nullptr) {
    return Status::CANCELLED;
  }

  const Descriptor* desc =
      descriptor_pool_->FindMessageTypeByName(request->containing_type());
  if (desc == nullptr) {
    return Status(StatusCode::NOT_FOUND, "Type not found.");
  }

  const FieldDescriptor* field_desc = descriptor_pool_->FindExtensionByNumber(
      desc, request->extension_number());
  if (field_desc == nullptr) {
    return Status(StatusCode::NOT_FOUND, "Extension not found.");
  }
  FillFileDescriptorProtoResponse(field_desc->file(), response);
  // field_desc->file()->CopyTo(response->mutable_file_descriptor_proto());
  return Status::OK;
}

Status ProtoServerReflection::GetAllExtensionNumbers(
    ServerContext* context, const TypeRequest* request,
    ExtensionNumberResponse* response) {
  if (descriptor_pool_ == nullptr) {
    return Status::CANCELLED;
  }

  const Descriptor* desc =
      descriptor_pool_->FindMessageTypeByName(request->type());
  if (desc == nullptr) {
    return Status(StatusCode::NOT_FOUND, "Type not found.");
  }

  std::vector<const FieldDescriptor*> extensions;
  descriptor_pool_->FindAllExtensions(desc, &extensions);
  for (auto extension : extensions) {
    response->add_extension_number(extension->number());
  }
  return Status::OK;
}

void ProtoServerReflection::FillFileDescriptorProtoResponse(
    const FileDescriptor* file_desc, FileDescriptorProtoResponse* response) {
  FileDescriptorProto file_desc_proto;
  grpc::string data;
  file_desc->CopyTo(&file_desc_proto);
  file_desc_proto.SerializeToString(&data);
  response->set_file_descriptor_proto(data);
}

}  // namespace grpc
