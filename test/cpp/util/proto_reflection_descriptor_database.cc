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

#include "proto_reflection_descriptor_database.h"

#include <vector>

#include <grpc/support/log.h>

namespace grpc {

ProtoReflectionDescriptorDatabase::ProtoReflectionDescriptorDatabase(
    std::unique_ptr<reflection::v1alpha::ServerReflection::Stub> stub)
    : stub_(std::move(stub)) {}

ProtoReflectionDescriptorDatabase::ProtoReflectionDescriptorDatabase(
    std::shared_ptr<grpc::Channel> channel)
    : stub_(reflection::v1alpha::ServerReflection::NewStub(channel)) {}

ProtoReflectionDescriptorDatabase::~ProtoReflectionDescriptorDatabase() {}

bool ProtoReflectionDescriptorDatabase::FindFileByName(
    const string& filename, google::protobuf::FileDescriptorProto* output) {
  if (cached_db_.FindFileByName(filename, output)) {
    return true;
  }

  if (known_files_.find(filename) != known_files_.end()) {
    return false;
  }

  ClientContext ctx;
  reflection::v1alpha::FileNameRequest request;
  request.set_filename(filename);
  reflection::v1alpha::FileDescriptorProtoResponse response;

  Status status = stub_->GetFileByName(&ctx, request, &response);
  if (status.ok()) {
    // const google::protobuf::FileDescriptorProto* file_proto =
    //     response.mutable_file_descriptor_proto();
    const google::protobuf::FileDescriptorProto file_proto =
        ParseFileDescriptorProtoResponse(&response);
    known_files_.insert(file_proto.name());
    cached_db_.Add(file_proto);
  } else if (status.error_code() == StatusCode::NOT_FOUND) {
    gpr_log(GPR_INFO, "NOT_FOUND from server for FindFileByName(%s)",
            filename.c_str());
  } else {
    gpr_log(GPR_INFO,
            "Error on FindFileByName(%s)\n\tError code: %d\n"
            "\tError Message: %s",
            filename.c_str(), status.error_code(),
            status.error_message().c_str());
  }

  return cached_db_.FindFileByName(filename, output);
}

bool ProtoReflectionDescriptorDatabase::FindFileContainingSymbol(
    const string& symbol_name, google::protobuf::FileDescriptorProto* output) {
  if (cached_db_.FindFileContainingSymbol(symbol_name, output)) {
    return true;
  }

  if (missing_symbols_.find(symbol_name) != missing_symbols_.end()) {
    return false;
  }

  ClientContext ctx;
  reflection::v1alpha::SymbolRequest request;
  request.set_symbol(symbol_name);
  reflection::v1alpha::FileDescriptorProtoResponse response;

  Status status = stub_->GetFileContainingSymbol(&ctx, request, &response);
  if (status.ok()) {
    const google::protobuf::FileDescriptorProto file_proto =
        ParseFileDescriptorProtoResponse(&response);
    if (known_files_.find(file_proto.name()) == known_files_.end()) {
      known_files_.insert(file_proto.name());
      cached_db_.Add(file_proto);
    }
  } else if (status.error_code() == StatusCode::NOT_FOUND) {
    missing_symbols_.insert(symbol_name);
    gpr_log(GPR_INFO, "NOT_FOUND from server for FindFileContainingSymbol(%s)",
            symbol_name.c_str());
  } else {
    gpr_log(GPR_INFO,
            "Error on FindFileContainingSymbol(%s)\n"
            "\tError code: %d\n\tError Message: %s",
            symbol_name.c_str(), status.error_code(),
            status.error_message().c_str());
  }

  return cached_db_.FindFileContainingSymbol(symbol_name, output);
}

bool ProtoReflectionDescriptorDatabase::FindFileContainingExtension(
    const string& containing_type, int field_number,
    google::protobuf::FileDescriptorProto* output) {
  if (cached_db_.FindFileContainingExtension(containing_type, field_number,
                                             output)) {
    return true;
  }

  if (missing_extensions_.find(containing_type) != missing_extensions_.end() &&
      missing_extensions_[containing_type].find(field_number) !=
          missing_extensions_[containing_type].end()) {
    gpr_log(GPR_INFO, "nested map.");
    return false;
  }

  ClientContext ctx;
  reflection::v1alpha::ExtensionRequest request;
  request.set_containing_type(containing_type);
  request.set_extension_number(field_number);
  reflection::v1alpha::FileDescriptorProtoResponse response;

  Status status = stub_->GetFileContainingExtension(&ctx, request, &response);
  if (status.ok()) {
    const google::protobuf::FileDescriptorProto file_proto =
        ParseFileDescriptorProtoResponse(&response);
    if (known_files_.find(file_proto.name()) == known_files_.end()) {
      known_files_.insert(file_proto.name());
      cached_db_.Add(file_proto);
    }
  } else if (status.error_code() == StatusCode::NOT_FOUND) {
    if (missing_extensions_.find(containing_type) ==
        missing_extensions_.end()) {
      missing_extensions_[containing_type] = {};
    }
    missing_extensions_[containing_type].insert(field_number);
    gpr_log(GPR_INFO,
            "NOT_FOUND from server for FindFileContainingExtension(%s, %d)",
            containing_type.c_str(), field_number);
  } else {
    gpr_log(GPR_INFO,
            "Error on FindFileContainingExtension(%s, %d)\n"
            "\tError code: %d\n\tError Message: %s",
            containing_type.c_str(), field_number, status.error_code(),
            status.error_message().c_str());
  }

  return cached_db_.FindFileContainingExtension(containing_type, field_number,
                                                output);
}

bool ProtoReflectionDescriptorDatabase::FindAllExtensionNumbers(
    const string& extendee_type, std::vector<int>* output) {
  if (cached_extension_numbers_.find(extendee_type) !=
      cached_extension_numbers_.end()) {
    *output = cached_extension_numbers_[extendee_type];
    return true;
  }

  ClientContext ctx;
  reflection::v1alpha::TypeRequest request;
  request.set_type(extendee_type);
  reflection::v1alpha::ExtensionNumberResponse response;

  Status status = stub_->GetAllExtensionNumbers(&ctx, request, &response);
  if (status.ok()) {
    auto number = response.extension_number();
    *output = std::vector<int>(number.begin(), number.end());
    cached_extension_numbers_[extendee_type] = *output;
    return true;
  } else if (status.error_code() == StatusCode::NOT_FOUND) {
    gpr_log(GPR_INFO, "NOT_FOUND from server for FindAllExtensionNumbers(%s)",
            extendee_type.c_str());
  } else {
    gpr_log(GPR_INFO,
            "Error on FindAllExtensionNumbersExtension(%s)\n"
            "\tError code: %d\n\tError Message: %s",
            extendee_type.c_str(), status.error_code(),
            status.error_message().c_str());
  }
  return false;
}

bool ProtoReflectionDescriptorDatabase::GetServices(
    std::vector<std::string>* output) {
  ClientContext ctx;
  reflection::v1alpha::EmptyRequest request;
  reflection::v1alpha::ListServiceResponse response;

  Status status = stub_->ListService(&ctx, request, &response);
  if (status.ok()) {
    for (int i = 0; i < response.services_size(); ++i) {
      (*output).push_back(response.services(i));
    }
    return true;
  } else {
    gpr_log(GPR_INFO,
            "Error on GetServices()\n\tError code: %d\n"
            "\tError Message: %s",
            status.error_code(), status.error_message().c_str());
  }
  return false;
}

const google::protobuf::FileDescriptorProto
ProtoReflectionDescriptorDatabase::ParseFileDescriptorProtoResponse(
    reflection::v1alpha::FileDescriptorProtoResponse* response) {
  google::protobuf::FileDescriptorProto file_desc_proto;
  file_desc_proto.ParseFromString(response->file_descriptor_proto());
  return file_desc_proto;
}

}  // namespace grpc
