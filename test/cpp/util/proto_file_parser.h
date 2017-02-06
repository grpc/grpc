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

#include <grpc++/channel.h>

#include "test/cpp/util/config_grpc_cli.h"
#include "test/cpp/util/proto_reflection_descriptor_database.h"

namespace grpc {
namespace testing {
class ErrorPrinter;

// Find method and associated request/response types.
class ProtoFileParser {
 public:
  // The parser will search proto files using the server reflection service
  // provided on the given channel. The given protofiles in a source tree rooted
  // from proto_path will also be searched.
  ProtoFileParser(std::shared_ptr<grpc::Channel> channel,
                  const grpc::string& proto_path,
                  const grpc::string& protofiles);

  ~ProtoFileParser();

  // The input method name in the following four functions could be a partial
  // string such as Service.Method or even just Method. It will log an error if
  // there is ambiguity.
  // Full method name is in the form of Service.Method, it's good to be used in
  // descriptor database queries.
  grpc::string GetFullMethodName(const grpc::string& method);

  // Formatted method name is in the form of /Service/Method, it's good to be
  // used as the argument of Stub::Call()
  grpc::string GetFormattedMethodName(const grpc::string& method);

  grpc::string GetSerializedProtoFromMethod(
      const grpc::string& method, const grpc::string& text_format_proto,
      bool is_request);

  grpc::string GetTextFormatFromMethod(const grpc::string& method,
                                       const grpc::string& serialized_proto,
                                       bool is_request);

  grpc::string GetSerializedProtoFromMessageType(
      const grpc::string& message_type_name,
      const grpc::string& text_format_proto);

  grpc::string GetTextFormatFromMessageType(
      const grpc::string& message_type_name,
      const grpc::string& serialized_proto);

  bool IsStreaming(const grpc::string& method, bool is_request);

  bool HasError() const { return has_error_; }

  void LogError(const grpc::string& error_msg);

 private:
  grpc::string GetMessageTypeFromMethod(const grpc::string& method,
                                        bool is_request);

  bool has_error_;
  grpc::string request_text_;
  protobuf::compiler::DiskSourceTree source_tree_;
  std::unique_ptr<ErrorPrinter> error_printer_;
  std::unique_ptr<protobuf::compiler::Importer> importer_;
  std::unique_ptr<grpc::ProtoReflectionDescriptorDatabase> reflection_db_;
  std::unique_ptr<protobuf::DescriptorPoolDatabase> file_db_;
  std::unique_ptr<protobuf::DescriptorDatabase> desc_db_;
  std::unique_ptr<protobuf::DescriptorPool> desc_pool_;
  std::unique_ptr<protobuf::DynamicMessageFactory> dynamic_factory_;
  std::unique_ptr<grpc::protobuf::Message> request_prototype_;
  std::unique_ptr<grpc::protobuf::Message> response_prototype_;
  std::unordered_map<grpc::string, grpc::string> known_methods_;
  std::vector<const protobuf::ServiceDescriptor*> service_desc_list_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_PROTO_FILE_PARSER_H
