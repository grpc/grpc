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

#include <unordered_set>
#include <vector>

#include <grpc++/grpc++.h>

#include "src/cpp/ext/proto_server_reflection.h"

using grpc::Status;
using grpc::StatusCode;
using grpc::reflection::v1alpha::ServerReflectionRequest;
using grpc::reflection::v1alpha::ExtensionRequest;
using grpc::reflection::v1alpha::ServerReflectionResponse;
using grpc::reflection::v1alpha::ListServiceResponse;
using grpc::reflection::v1alpha::ServiceResponse;
using grpc::reflection::v1alpha::ExtensionNumberResponse;
using grpc::reflection::v1alpha::ErrorResponse;
using grpc::reflection::v1alpha::FileDescriptorResponse;

namespace grpc {

ProtoServerReflection::ProtoServerReflection()
    : descriptor_pool_(protobuf::DescriptorPool::generated_pool()) {}

void ProtoServerReflection::SetServiceList(
    const std::vector<grpc::string>* services) {
  services_ = services;
}

Status ProtoServerReflection::ServerReflectionInfo(
    ServerContext* context,
    ServerReaderWriter<ServerReflectionResponse, ServerReflectionRequest>*
        stream) {
  ServerReflectionRequest request;
  ServerReflectionResponse response;
  Status status;
  while (stream->Read(&request)) {
    switch (request.message_request_case()) {
      case ServerReflectionRequest::MessageRequestCase::kFileByFilename:
        status = GetFileByName(context, request.file_by_filename(), &response);
        break;
      case ServerReflectionRequest::MessageRequestCase::kFileContainingSymbol:
        status = GetFileContainingSymbol(
            context, request.file_containing_symbol(), &response);
        break;
      case ServerReflectionRequest::MessageRequestCase::
          kFileContainingExtension:
        status = GetFileContainingExtension(
            context, &request.file_containing_extension(), &response);
        break;
      case ServerReflectionRequest::MessageRequestCase::
          kAllExtensionNumbersOfType:
        status = GetAllExtensionNumbers(
            context, request.all_extension_numbers_of_type(),
            response.mutable_all_extension_numbers_response());
        break;
      case ServerReflectionRequest::MessageRequestCase::kListServices:
        status =
            ListService(context, response.mutable_list_services_response());
        break;
      default:
        status = Status(StatusCode::UNIMPLEMENTED, "");
    }

    if (!status.ok()) {
      FillErrorResponse(status, response.mutable_error_response());
    }
    response.set_valid_host(request.host());
    response.set_allocated_original_request(
        new ServerReflectionRequest(request));
    stream->Write(response);
  }

  return Status::OK;
}

void ProtoServerReflection::FillErrorResponse(const Status& status,
                                              ErrorResponse* error_response) {
  error_response->set_error_code(status.error_code());
  error_response->set_error_message(status.error_message());
}

Status ProtoServerReflection::ListService(ServerContext* context,
                                          ListServiceResponse* response) {
  if (services_ == nullptr) {
    return Status(StatusCode::NOT_FOUND, "Services not found.");
  }
  for (auto it = services_->begin(); it != services_->end(); ++it) {
    ServiceResponse* service_response = response->add_service();
    service_response->set_name(*it);
  }
  return Status::OK;
}

Status ProtoServerReflection::GetFileByName(
    ServerContext* context, const grpc::string& filename,
    ServerReflectionResponse* response) {
  if (descriptor_pool_ == nullptr) {
    return Status::CANCELLED;
  }

  const protobuf::FileDescriptor* file_desc =
      descriptor_pool_->FindFileByName(filename);
  if (file_desc == nullptr) {
    return Status(StatusCode::NOT_FOUND, "File not found.");
  }
  std::unordered_set<grpc::string> seen_files;
  FillFileDescriptorResponse(file_desc, response, &seen_files);
  return Status::OK;
}

Status ProtoServerReflection::GetFileContainingSymbol(
    ServerContext* context, const grpc::string& symbol,
    ServerReflectionResponse* response) {
  if (descriptor_pool_ == nullptr) {
    return Status::CANCELLED;
  }

  const protobuf::FileDescriptor* file_desc =
      descriptor_pool_->FindFileContainingSymbol(symbol);
  if (file_desc == nullptr) {
    return Status(StatusCode::NOT_FOUND, "Symbol not found.");
  }
  std::unordered_set<grpc::string> seen_files;
  FillFileDescriptorResponse(file_desc, response, &seen_files);
  return Status::OK;
}

Status ProtoServerReflection::GetFileContainingExtension(
    ServerContext* context, const ExtensionRequest* request,
    ServerReflectionResponse* response) {
  if (descriptor_pool_ == nullptr) {
    return Status::CANCELLED;
  }

  const protobuf::Descriptor* desc =
      descriptor_pool_->FindMessageTypeByName(request->containing_type());
  if (desc == nullptr) {
    return Status(StatusCode::NOT_FOUND, "Type not found.");
  }

  const protobuf::FieldDescriptor* field_desc =
      descriptor_pool_->FindExtensionByNumber(desc,
                                              request->extension_number());
  if (field_desc == nullptr) {
    return Status(StatusCode::NOT_FOUND, "Extension not found.");
  }
  std::unordered_set<grpc::string> seen_files;
  FillFileDescriptorResponse(field_desc->file(), response, &seen_files);
  return Status::OK;
}

Status ProtoServerReflection::GetAllExtensionNumbers(
    ServerContext* context, const grpc::string& type,
    ExtensionNumberResponse* response) {
  if (descriptor_pool_ == nullptr) {
    return Status::CANCELLED;
  }

  const protobuf::Descriptor* desc =
      descriptor_pool_->FindMessageTypeByName(type);
  if (desc == nullptr) {
    return Status(StatusCode::NOT_FOUND, "Type not found.");
  }

  std::vector<const protobuf::FieldDescriptor*> extensions;
  descriptor_pool_->FindAllExtensions(desc, &extensions);
  for (auto it = extensions.begin(); it != extensions.end(); it++) {
    response->add_extension_number((*it)->number());
  }
  response->set_base_type_name(type);
  return Status::OK;
}

void ProtoServerReflection::FillFileDescriptorResponse(
    const protobuf::FileDescriptor* file_desc,
    ServerReflectionResponse* response,
    std::unordered_set<grpc::string>* seen_files) {
  if (seen_files->find(file_desc->name()) != seen_files->end()) {
    return;
  }
  seen_files->insert(file_desc->name());

  protobuf::FileDescriptorProto file_desc_proto;
  grpc::string data;
  file_desc->CopyTo(&file_desc_proto);
  file_desc_proto.SerializeToString(&data);
  response->mutable_file_descriptor_response()->add_file_descriptor_proto(data);

  for (int i = 0; i < file_desc->dependency_count(); ++i) {
    FillFileDescriptorResponse(file_desc->dependency(i), response, seen_files);
  }
}

}  // namespace grpc
