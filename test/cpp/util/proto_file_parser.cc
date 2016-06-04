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

#include <google/protobuf/text_format.h>
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

class ErrorPrinter
    : public google::protobuf::compiler::MultiFileErrorCollector {
 public:
  explicit ErrorPrinter(ProtoFileParser* parser) : parser_(parser) {}

  void AddError(const grpc::string& filename, int line, int column,
                const grpc::string& message) GRPC_OVERRIDE {
    std::ostringstream oss;
    oss << "error " << filename << " " << line << " " << column << " "
        << message << "\n";
    parser_->LogError(oss.str());
  }

  void AddWarning(const grpc::string& filename, int line, int column,
                  const grpc::string& message) GRPC_OVERRIDE {
    std::cout << "warning " << filename << " " << line << " " << column << " "
              << message << std::endl;
  }

 private:
  ProtoFileParser* parser_;  // not owned
};

ProtoFileParser::ProtoFileParser(const grpc::string& proto_path,
                                 const grpc::string& file_name,
                                 const grpc::string& method)
    : has_error_(false) {
  source_tree_.MapPath("", proto_path);
  error_printer_.reset(new ErrorPrinter(this));
  importer_.reset(new google::protobuf::compiler::Importer(
      &source_tree_, error_printer_.get()));
  const auto* file_desc = importer_->Import(file_name);
  if (!file_desc) {
    LogError("");
    return;
  }
  dynamic_factory_.reset(
      new google::protobuf::DynamicMessageFactory(importer_->pool()));

  const google::protobuf::MethodDescriptor* method_descriptor = nullptr;
  for (int i = 0; !method_descriptor && i < file_desc->service_count(); i++) {
    const auto* service_desc = file_desc->service(i);
    for (int j = 0; j < service_desc->method_count(); j++) {
      const auto* method_desc = service_desc->method(j);
      if (MethodNameMatch(method_desc->full_name(), method)) {
        if (method_descriptor) {
          std::ostringstream error_stream("Ambiguous method names: ");
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
    return;
  }
  full_method_name_ = method_descriptor->full_name();
  size_t last_dot = full_method_name_.find_last_of('.');
  if (last_dot != grpc::string::npos) {
    full_method_name_[last_dot] = '/';
  }
  full_method_name_.insert(full_method_name_.begin(), '/');

  request_prototype_.reset(
      dynamic_factory_->GetPrototype(method_descriptor->input_type())->New());
  response_prototype_.reset(
      dynamic_factory_->GetPrototype(method_descriptor->output_type())->New());
}

ProtoFileParser::~ProtoFileParser() {}

grpc::string ProtoFileParser::GetSerializedProto(
    const grpc::string& text_format_proto, bool is_request) {
  grpc::string serialized;
  grpc::protobuf::Message* msg =
      is_request ? request_prototype_.get() : response_prototype_.get();
  bool ok =
      google::protobuf::TextFormat::ParseFromString(text_format_proto, msg);
  if (!ok) {
    LogError("Failed to parse text format to proto.");
    return "";
  }
  ok = request_prototype_->SerializeToString(&serialized);
  if (!ok) {
    LogError("Failed to serialize proto.");
    return "";
  }
  return serialized;
}

grpc::string ProtoFileParser::GetTextFormat(
    const grpc::string& serialized_proto, bool is_request) {
  grpc::protobuf::Message* msg =
      is_request ? request_prototype_.get() : response_prototype_.get();
  if (!msg->ParseFromString(serialized_proto)) {
    LogError("Failed to deserialize proto.");
    return "";
  }
  grpc::string text_format;
  if (!google::protobuf::TextFormat::PrintToString(*msg, &text_format)) {
    LogError("Failed to print proto message to text format");
    return "";
  }
  return text_format;
}

void ProtoFileParser::LogError(const grpc::string& error_msg) {
  if (!error_msg.empty()) {
    std::cout << error_msg << std::endl;
  }
  has_error_ = true;
}

}  // namespace testing
}  // namespace grpc
