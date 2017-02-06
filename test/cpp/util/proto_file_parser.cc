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

#include "test/cpp/util/proto_file_parser.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include <grpc++/support/config.h>

namespace grpc {
namespace testing {
namespace {

// Match the user input method string to the full_name from method descriptor.
bool MethodNameMatch(const grpc::string& full_name, const grpc::string& input) {
  grpc::string clean_input = input;
  std::replace(clean_input.begin(), clean_input.end(), '/', '.');
  if (clean_input.size() > full_name.size()) {
    return false;
  }
  return full_name.compare(full_name.size() - clean_input.size(),
                           clean_input.size(), clean_input) == 0;
}
}  // namespace

class ErrorPrinter : public protobuf::compiler::MultiFileErrorCollector {
 public:
  explicit ErrorPrinter(ProtoFileParser* parser) : parser_(parser) {}

  void AddError(const grpc::string& filename, int line, int column,
                const grpc::string& message) override {
    std::ostringstream oss;
    oss << "error " << filename << " " << line << " " << column << " "
        << message << "\n";
    parser_->LogError(oss.str());
  }

  void AddWarning(const grpc::string& filename, int line, int column,
                  const grpc::string& message) override {
    std::cerr << "warning " << filename << " " << line << " " << column << " "
              << message << std::endl;
  }

 private:
  ProtoFileParser* parser_;  // not owned
};

ProtoFileParser::ProtoFileParser(std::shared_ptr<grpc::Channel> channel,
                                 const grpc::string& proto_path,
                                 const grpc::string& protofiles)
    : has_error_(false),
      dynamic_factory_(new protobuf::DynamicMessageFactory()) {
  std::vector<grpc::string> service_list;
  if (channel) {
    reflection_db_.reset(new grpc::ProtoReflectionDescriptorDatabase(channel));
    reflection_db_->GetServices(&service_list);
  }

  std::unordered_set<grpc::string> known_services;
  if (!protofiles.empty()) {
    source_tree_.MapPath("", proto_path);
    error_printer_.reset(new ErrorPrinter(this));
    importer_.reset(
        new protobuf::compiler::Importer(&source_tree_, error_printer_.get()));

    grpc::string file_name;
    std::stringstream ss(protofiles);
    while (std::getline(ss, file_name, ',')) {
      const auto* file_desc = importer_->Import(file_name);
      if (file_desc) {
        for (int i = 0; i < file_desc->service_count(); i++) {
          service_desc_list_.push_back(file_desc->service(i));
          known_services.insert(file_desc->service(i)->full_name());
        }
      } else {
        std::cerr << file_name << " not found" << std::endl;
      }
    }

    file_db_.reset(new protobuf::DescriptorPoolDatabase(*importer_->pool()));
  }

  if (!reflection_db_ && !file_db_) {
    LogError("No available proto database");
    return;
  }

  if (!reflection_db_) {
    desc_db_ = std::move(file_db_);
  } else if (!file_db_) {
    desc_db_ = std::move(reflection_db_);
  } else {
    desc_db_.reset(new protobuf::MergedDescriptorDatabase(reflection_db_.get(),
                                                          file_db_.get()));
  }

  desc_pool_.reset(new protobuf::DescriptorPool(desc_db_.get()));

  for (auto it = service_list.begin(); it != service_list.end(); it++) {
    if (known_services.find(*it) == known_services.end()) {
      if (const protobuf::ServiceDescriptor* service_desc =
              desc_pool_->FindServiceByName(*it)) {
        service_desc_list_.push_back(service_desc);
        known_services.insert(*it);
      }
    }
  }
}

ProtoFileParser::~ProtoFileParser() {}

grpc::string ProtoFileParser::GetFullMethodName(const grpc::string& method) {
  has_error_ = false;

  if (known_methods_.find(method) != known_methods_.end()) {
    return known_methods_[method];
  }

  const protobuf::MethodDescriptor* method_descriptor = nullptr;
  for (auto it = service_desc_list_.begin(); it != service_desc_list_.end();
       it++) {
    const auto* service_desc = *it;
    for (int j = 0; j < service_desc->method_count(); j++) {
      const auto* method_desc = service_desc->method(j);
      if (MethodNameMatch(method_desc->full_name(), method)) {
        if (method_descriptor) {
          std::ostringstream error_stream;
          error_stream << "Ambiguous method names: ";
          error_stream << method_descriptor->full_name() << " ";
          error_stream << method_desc->full_name();
          LogError(error_stream.str());
        }
        method_descriptor = method_desc;
      }
    }
  }
  if (!method_descriptor) {
    LogError("Method name not found");
  }
  if (has_error_) {
    return "";
  }

  known_methods_[method] = method_descriptor->full_name();

  return method_descriptor->full_name();
}

grpc::string ProtoFileParser::GetFormattedMethodName(
    const grpc::string& method) {
  has_error_ = false;
  grpc::string formatted_method_name = GetFullMethodName(method);
  if (has_error_) {
    return "";
  }
  size_t last_dot = formatted_method_name.find_last_of('.');
  if (last_dot != grpc::string::npos) {
    formatted_method_name[last_dot] = '/';
  }
  formatted_method_name.insert(formatted_method_name.begin(), '/');
  return formatted_method_name;
}

grpc::string ProtoFileParser::GetMessageTypeFromMethod(
    const grpc::string& method, bool is_request) {
  has_error_ = false;
  grpc::string full_method_name = GetFullMethodName(method);
  if (has_error_) {
    return "";
  }
  const protobuf::MethodDescriptor* method_desc =
      desc_pool_->FindMethodByName(full_method_name);
  if (!method_desc) {
    LogError("Method not found");
    return "";
  }

  return is_request ? method_desc->input_type()->full_name()
                    : method_desc->output_type()->full_name();
}

bool ProtoFileParser::IsStreaming(const grpc::string& method, bool is_request) {
  has_error_ = false;

  grpc::string full_method_name = GetFullMethodName(method);
  if (has_error_) {
    return false;
  }

  const protobuf::MethodDescriptor* method_desc =
      desc_pool_->FindMethodByName(full_method_name);
  if (!method_desc) {
    LogError("Method not found");
    return false;
  }

  return is_request ? method_desc->client_streaming()
                    : method_desc->server_streaming();
}

grpc::string ProtoFileParser::GetSerializedProtoFromMethod(
    const grpc::string& method, const grpc::string& text_format_proto,
    bool is_request) {
  has_error_ = false;
  grpc::string message_type_name = GetMessageTypeFromMethod(method, is_request);
  if (has_error_) {
    return "";
  }
  return GetSerializedProtoFromMessageType(message_type_name,
                                           text_format_proto);
}

grpc::string ProtoFileParser::GetTextFormatFromMethod(
    const grpc::string& method, const grpc::string& serialized_proto,
    bool is_request) {
  has_error_ = false;
  grpc::string message_type_name = GetMessageTypeFromMethod(method, is_request);
  if (has_error_) {
    return "";
  }
  return GetTextFormatFromMessageType(message_type_name, serialized_proto);
}

grpc::string ProtoFileParser::GetSerializedProtoFromMessageType(
    const grpc::string& message_type_name,
    const grpc::string& text_format_proto) {
  has_error_ = false;
  grpc::string serialized;
  const protobuf::Descriptor* desc =
      desc_pool_->FindMessageTypeByName(message_type_name);
  if (!desc) {
    LogError("Message type not found");
    return "";
  }
  std::unique_ptr<grpc::protobuf::Message> msg(
      dynamic_factory_->GetPrototype(desc)->New());
  bool ok = protobuf::TextFormat::ParseFromString(text_format_proto, msg.get());
  if (!ok) {
    LogError("Failed to parse text format to proto.");
    return "";
  }
  ok = msg->SerializeToString(&serialized);
  if (!ok) {
    LogError("Failed to serialize proto.");
    return "";
  }
  return serialized;
}

grpc::string ProtoFileParser::GetTextFormatFromMessageType(
    const grpc::string& message_type_name,
    const grpc::string& serialized_proto) {
  has_error_ = false;
  const protobuf::Descriptor* desc =
      desc_pool_->FindMessageTypeByName(message_type_name);
  if (!desc) {
    LogError("Message type not found");
    return "";
  }
  std::unique_ptr<grpc::protobuf::Message> msg(
      dynamic_factory_->GetPrototype(desc)->New());
  if (!msg->ParseFromString(serialized_proto)) {
    LogError("Failed to deserialize proto.");
    return "";
  }
  grpc::string text_format;
  if (!protobuf::TextFormat::PrintToString(*msg.get(), &text_format)) {
    LogError("Failed to print proto message to text format");
    return "";
  }
  return text_format;
}

void ProtoFileParser::LogError(const grpc::string& error_msg) {
  if (!error_msg.empty()) {
    std::cerr << error_msg << std::endl;
  }
  has_error_ = true;
}

}  // namespace testing
}  // namespace grpc
