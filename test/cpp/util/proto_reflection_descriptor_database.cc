//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "test/cpp/util/proto_reflection_descriptor_database.h"

#include <vector>

#include "absl/log/log.h"
#include "src/core/util/crash.h"

using grpc::reflection::v1alpha::ErrorResponse;
using grpc::reflection::v1alpha::ListServiceResponse;
using grpc::reflection::v1alpha::ServerReflection;
using grpc::reflection::v1alpha::ServerReflectionRequest;
using grpc::reflection::v1alpha::ServerReflectionResponse;

namespace grpc {

ProtoReflectionDescriptorDatabase::ProtoReflectionDescriptorDatabase(
    std::unique_ptr<ServerReflection::Stub> stub)
    : stub_(std::move(stub)) {}

ProtoReflectionDescriptorDatabase::ProtoReflectionDescriptorDatabase(
    const std::shared_ptr<grpc::ChannelInterface>& channel)
    : stub_(ServerReflection::NewStub(channel)) {}

ProtoReflectionDescriptorDatabase::~ProtoReflectionDescriptorDatabase() {
  if (stream_) {
    stream_->WritesDone();
    Status status = stream_->Finish();
    if (!status.ok()) {
      if (status.error_code() == StatusCode::UNIMPLEMENTED) {
        fprintf(stderr,
                "Reflection request not implemented; "
                "is the ServerReflection service enabled?\n");
      } else {
        fprintf(stderr,
                "ServerReflectionInfo rpc failed. Error code: %d, message: %s, "
                "debug info: %s\n",
                static_cast<int>(status.error_code()),
                status.error_message().c_str(),
                ctx_.debug_error_string().c_str());
      }
    }
  }
}

bool ProtoReflectionDescriptorDatabase::FindFileByName(
    const string& filename, protobuf::FileDescriptorProto* output) {
  if (cached_db_.FindFileByName(filename, output)) {
    return true;
  }

  if (known_files_.find(filename) != known_files_.end()) {
    return false;
  }

  ServerReflectionRequest request;
  request.set_file_by_filename(filename);
  ServerReflectionResponse response;

  if (!DoOneRequest(request, response)) {
    return false;
  }

  if (response.message_response_case() ==
      ServerReflectionResponse::MessageResponseCase::kFileDescriptorResponse) {
    AddFileFromResponse(response.file_descriptor_response());
  } else if (response.message_response_case() ==
             ServerReflectionResponse::MessageResponseCase::kErrorResponse) {
    const ErrorResponse& error = response.error_response();
    if (error.error_code() == StatusCode::NOT_FOUND) {
      LOG(INFO) << "NOT_FOUND from server for FindFileByName(" << filename
                << ")";
    } else {
      LOG(INFO) << "Error on FindFileByName(" << filename
                << ")\n\tError code: " << error.error_code()
                << "\n\tError Message: " << error.error_message();
    }
  } else {
    LOG(INFO) << "Error on FindFileByName(" << filename
              << ") response type\n\tExpecting: "
              << ServerReflectionResponse::MessageResponseCase::
                     kFileDescriptorResponse
              << "\n\tReceived: " << response.message_response_case();
  }

  return cached_db_.FindFileByName(filename, output);
}

bool ProtoReflectionDescriptorDatabase::FindFileContainingSymbol(
    const string& symbol_name, protobuf::FileDescriptorProto* output) {
  if (cached_db_.FindFileContainingSymbol(symbol_name, output)) {
    return true;
  }

  if (missing_symbols_.find(symbol_name) != missing_symbols_.end()) {
    return false;
  }

  ServerReflectionRequest request;
  request.set_file_containing_symbol(symbol_name);
  ServerReflectionResponse response;

  if (!DoOneRequest(request, response)) {
    return false;
  }

  if (response.message_response_case() ==
      ServerReflectionResponse::MessageResponseCase::kFileDescriptorResponse) {
    AddFileFromResponse(response.file_descriptor_response());
  } else if (response.message_response_case() ==
             ServerReflectionResponse::MessageResponseCase::kErrorResponse) {
    const ErrorResponse& error = response.error_response();
    if (error.error_code() == StatusCode::NOT_FOUND) {
      missing_symbols_.insert(symbol_name);
      LOG(INFO) << "NOT_FOUND from server for FindFileContainingSymbol("
                << symbol_name << ")";
    } else {
      LOG(INFO) << "Error on FindFileContainingSymbol(" << symbol_name
                << ")\n\tError code: " << error.error_code()
                << "\n\tError Message: " << error.error_message();
    }
  } else {
    LOG(INFO) << "Error on FindFileContainingSymbol(" << symbol_name
              << ") response type\n\tExpecting: "
              << ServerReflectionResponse::MessageResponseCase::
                     kFileDescriptorResponse
              << "\n\tReceived: " << response.message_response_case();
  }
  return cached_db_.FindFileContainingSymbol(symbol_name, output);
}

bool ProtoReflectionDescriptorDatabase::FindFileContainingExtension(
    const string& containing_type, int field_number,
    protobuf::FileDescriptorProto* output) {
  if (cached_db_.FindFileContainingExtension(containing_type, field_number,
                                             output)) {
    return true;
  }

  if (missing_extensions_.find(containing_type) != missing_extensions_.end() &&
      missing_extensions_[containing_type].find(field_number) !=
          missing_extensions_[containing_type].end()) {
    LOG(INFO) << "nested map.";
    return false;
  }

  ServerReflectionRequest request;
  request.mutable_file_containing_extension()->set_containing_type(
      containing_type);
  request.mutable_file_containing_extension()->set_extension_number(
      field_number);
  ServerReflectionResponse response;

  if (!DoOneRequest(request, response)) {
    return false;
  }

  if (response.message_response_case() ==
      ServerReflectionResponse::MessageResponseCase::kFileDescriptorResponse) {
    AddFileFromResponse(response.file_descriptor_response());
  } else if (response.message_response_case() ==
             ServerReflectionResponse::MessageResponseCase::kErrorResponse) {
    const ErrorResponse& error = response.error_response();
    if (error.error_code() == StatusCode::NOT_FOUND) {
      if (missing_extensions_.find(containing_type) ==
          missing_extensions_.end()) {
        missing_extensions_[containing_type] = {};
      }
      missing_extensions_[containing_type].insert(field_number);
      LOG(INFO) << "NOT_FOUND from server for FindFileContainingExtension("
                << containing_type << ", " << field_number << ")";
    } else {
      LOG(INFO) << "Error on FindFileContainingExtension(" << containing_type
                << ", " << field_number
                << ")\n\tError code: " << error.error_code()
                << "\n\tError Message: " << error.error_message();
    }
  } else {
    LOG(INFO) << "Error on FindFileContainingExtension(" << containing_type
              << ", " << field_number << ") response type\n\tExpecting: "
              << ServerReflectionResponse::MessageResponseCase::
                     kFileDescriptorResponse
              << "\n\tReceived: " << response.message_response_case();
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

  ServerReflectionRequest request;
  request.set_all_extension_numbers_of_type(extendee_type);
  ServerReflectionResponse response;

  if (!DoOneRequest(request, response)) {
    return false;
  }

  if (response.message_response_case() ==
      ServerReflectionResponse::MessageResponseCase::
          kAllExtensionNumbersResponse) {
    auto number = response.all_extension_numbers_response().extension_number();
    *output = std::vector<int>(number.begin(), number.end());
    cached_extension_numbers_[extendee_type] = *output;
    return true;
  } else if (response.message_response_case() ==
             ServerReflectionResponse::MessageResponseCase::kErrorResponse) {
    const ErrorResponse& error = response.error_response();
    if (error.error_code() == StatusCode::NOT_FOUND) {
      LOG(INFO) << "NOT_FOUND from server for FindAllExtensionNumbers("
                << extendee_type << ")";
    } else {
      LOG(INFO) << "Error on FindAllExtensionNumbersExtension(" << extendee_type
                << ")\n\tError code: " << error.error_code()
                << "\n\tError Message: " << error.error_message();
    }
  }
  return false;
}

bool ProtoReflectionDescriptorDatabase::GetServices(
    std::vector<std::string>* output) {
  ServerReflectionRequest request;
  request.set_list_services("");
  ServerReflectionResponse response;

  if (!DoOneRequest(request, response)) {
    return false;
  }

  if (response.message_response_case() ==
      ServerReflectionResponse::MessageResponseCase::kListServicesResponse) {
    const ListServiceResponse& ls_response = response.list_services_response();
    for (int i = 0; i < ls_response.service_size(); ++i) {
      (*output).push_back(ls_response.service(i).name());
    }
    return true;
  } else if (response.message_response_case() ==
             ServerReflectionResponse::MessageResponseCase::kErrorResponse) {
    const ErrorResponse& error = response.error_response();
    LOG(INFO) << "Error on GetServices()\n\tError code: " << error.error_code()
              << "\n\tError Message: " << error.error_message();
  } else {
    LOG(INFO)
        << "Error on GetServices() response type\n\tExpecting: "
        << ServerReflectionResponse::MessageResponseCase::kListServicesResponse
        << "\n\tReceived: " << response.message_response_case();
  }
  return false;
}

protobuf::FileDescriptorProto
ProtoReflectionDescriptorDatabase::ParseFileDescriptorProtoResponse(
    const std::string& byte_fd_proto) {
  protobuf::FileDescriptorProto file_desc_proto;
  file_desc_proto.ParseFromString(byte_fd_proto);
  return file_desc_proto;
}

void ProtoReflectionDescriptorDatabase::AddFileFromResponse(
    const grpc::reflection::v1alpha::FileDescriptorResponse& response) {
  for (int i = 0; i < response.file_descriptor_proto_size(); ++i) {
    const protobuf::FileDescriptorProto file_proto =
        ParseFileDescriptorProtoResponse(response.file_descriptor_proto(i));
    if (known_files_.find(file_proto.name()) == known_files_.end()) {
      known_files_.insert(file_proto.name());
      cached_db_.Add(file_proto);
    }
  }
}

std::shared_ptr<ProtoReflectionDescriptorDatabase::ClientStream>
ProtoReflectionDescriptorDatabase::GetStream() {
  if (!stream_) {
    stream_ = stub_->ServerReflectionInfo(&ctx_);
  }
  return stream_;
}

bool ProtoReflectionDescriptorDatabase::DoOneRequest(
    const ServerReflectionRequest& request,
    ServerReflectionResponse& response) {
  bool success = false;
  stream_mutex_.lock();
  if (GetStream()->Write(request) && GetStream()->Read(&response)) {
    success = true;
  }
  stream_mutex_.unlock();
  return success;
}

}  // namespace grpc
