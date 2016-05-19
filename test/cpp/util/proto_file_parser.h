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

#ifndef GRPC_TEST_CPP_UTIL_PROTO_FILE_PARSER_H
#define GRPC_TEST_CPP_UTIL_PROTO_FILE_PARSER_H

#include <memory>

#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/dynamic_message.h>

#include "src/compiler/config.h"

namespace grpc {
namespace testing {
class ErrorPrinter;

// Find method and associated request/response types.
class ProtoFileParser {
 public:
  // The given proto file_name will be searched in a source tree rooted from
  // proto_path. The method could be a partial string such as Service.Method or
  // even just Method. It will log an error if there is ambiguity.
  ProtoFileParser(const grpc::string& proto_path, const grpc::string& file_name,
                  const grpc::string& method);
  ~ProtoFileParser();

  grpc::string GetFullMethodName() const { return full_method_name_; }

  grpc::string GetSerializedProto(const grpc::string& text_format_proto,
                                  bool is_request);

  grpc::string GetTextFormat(const grpc::string& serialized_proto,
                             bool is_request);

  bool HasError() const { return has_error_; }

  void LogError(const grpc::string& error_msg);

 private:
  bool has_error_;
  grpc::string request_text_;
  grpc::string full_method_name_;
  google::protobuf::compiler::DiskSourceTree source_tree_;
  std::unique_ptr<ErrorPrinter> error_printer_;
  std::unique_ptr<google::protobuf::compiler::Importer> importer_;
  std::unique_ptr<google::protobuf::DynamicMessageFactory> dynamic_factory_;
  std::unique_ptr<grpc::protobuf::Message> request_prototype_;
  std::unique_ptr<grpc::protobuf::Message> response_prototype_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_PROTO_FILE_PARSER_H
