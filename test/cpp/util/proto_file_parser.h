/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_TEST_CPP_UTIL_PROTO_FILE_PARSER_H
#define GRPC_TEST_CPP_UTIL_PROTO_FILE_PARSER_H

#include <memory>

#include <grpcpp/channel.h>

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
