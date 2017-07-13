/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_INTERNAL_COMPILER_PROTOBUF_PLUGIN_H
#define GRPC_INTERNAL_COMPILER_PROTOBUF_PLUGIN_H

#include "src/compiler/config.h"
#include "src/compiler/cpp_generator_helpers.h"
#include "src/compiler/python_generator_helpers.h"
#include "src/compiler/python_private_generator.h"
#include "src/compiler/schema_interface.h"

#include <vector>

// Get leading or trailing comments in a string.
template <typename DescriptorType>
inline grpc::string GetCommentsHelper(const DescriptorType *desc, bool leading,
                                      const grpc::string &prefix) {
  return grpc_generator::GetPrefixedComments(desc, leading, prefix);
}

class ProtoBufMethod : public grpc_generator::Method {
 public:
  ProtoBufMethod(const grpc::protobuf::MethodDescriptor *method)
      : method_(method) {}

  grpc::string name() const { return method_->name(); }

  grpc::string input_type_name() const {
    return grpc_cpp_generator::ClassName(method_->input_type(), true);
  }
  grpc::string output_type_name() const {
    return grpc_cpp_generator::ClassName(method_->output_type(), true);
  }

  grpc::string get_input_type_name() const {
    return method_->input_type()->file()->name();
  }
  grpc::string get_output_type_name() const {
    return method_->output_type()->file()->name();
  }

  bool get_module_and_message_path_input(grpc::string *str,
                                         grpc::string generator_file_name,
                                         bool generate_in_pb2_grpc,
                                         grpc::string import_prefix) const {
    return grpc_python_generator::GetModuleAndMessagePath(
        method_->input_type(), str, generator_file_name, generate_in_pb2_grpc,
        import_prefix);
  }

  bool get_module_and_message_path_output(grpc::string *str,
                                          grpc::string generator_file_name,
                                          bool generate_in_pb2_grpc,
                                          grpc::string import_prefix) const {
    return grpc_python_generator::GetModuleAndMessagePath(
        method_->output_type(), str, generator_file_name, generate_in_pb2_grpc,
        import_prefix);
  }

  bool NoStreaming() const {
    return !method_->client_streaming() && !method_->server_streaming();
  }

  bool ClientStreaming() const { return method_->client_streaming(); }

  bool ServerStreaming() const { return method_->server_streaming(); }

  bool BidiStreaming() const {
    return method_->client_streaming() && method_->server_streaming();
  }

  grpc::string GetLeadingComments(const grpc::string prefix) const {
    return GetCommentsHelper(method_, true, prefix);
  }

  grpc::string GetTrailingComments(const grpc::string prefix) const {
    return GetCommentsHelper(method_, false, prefix);
  }

  vector<grpc::string> GetAllComments() const {
    return grpc_python_generator::get_all_comments(method_);
  }

 private:
  const grpc::protobuf::MethodDescriptor *method_;
};

class ProtoBufService : public grpc_generator::Service {
 public:
  ProtoBufService(const grpc::protobuf::ServiceDescriptor *service)
      : service_(service) {}

  grpc::string name() const { return service_->name(); }

  int method_count() const { return service_->method_count(); };
  std::unique_ptr<const grpc_generator::Method> method(int i) const {
    return std::unique_ptr<const grpc_generator::Method>(
        new ProtoBufMethod(service_->method(i)));
  };

  grpc::string GetLeadingComments(const grpc::string prefix) const {
    return GetCommentsHelper(service_, true, prefix);
  }

  grpc::string GetTrailingComments(const grpc::string prefix) const {
    return GetCommentsHelper(service_, false, prefix);
  }

  vector<grpc::string> GetAllComments() const {
    return grpc_python_generator::get_all_comments(service_);
  }

 private:
  const grpc::protobuf::ServiceDescriptor *service_;
};

class ProtoBufPrinter : public grpc_generator::Printer {
 public:
  ProtoBufPrinter(grpc::string *str)
      : output_stream_(str), printer_(&output_stream_, '$') {}

  void Print(const std::map<grpc::string, grpc::string> &vars,
             const char *string_template) {
    printer_.Print(vars, string_template);
  }

  void Print(const char *string) { printer_.Print(string); }
  void Indent() { printer_.Indent(); }
  void Outdent() { printer_.Outdent(); }

 private:
  grpc::protobuf::io::StringOutputStream output_stream_;
  grpc::protobuf::io::Printer printer_;
};

class ProtoBufFile : public grpc_generator::File {
 public:
  ProtoBufFile(const grpc::protobuf::FileDescriptor *file) : file_(file) {}

  grpc::string filename() const { return file_->name(); }
  grpc::string filename_without_ext() const {
    return grpc_generator::StripProto(filename());
  }

  grpc::string package() const { return file_->package(); }
  std::vector<grpc::string> package_parts() const {
    return grpc_generator::tokenize(package(), ".");
  }

  grpc::string additional_headers() const { return ""; }

  int service_count() const { return file_->service_count(); };
  std::unique_ptr<const grpc_generator::Service> service(int i) const {
    return std::unique_ptr<const grpc_generator::Service>(
        new ProtoBufService(file_->service(i)));
  }

  std::unique_ptr<grpc_generator::Printer> CreatePrinter(
      grpc::string *str) const {
    return std::unique_ptr<grpc_generator::Printer>(new ProtoBufPrinter(str));
  }

  grpc::string GetLeadingComments(const grpc::string prefix) const {
    return GetCommentsHelper(file_, true, prefix);
  }

  grpc::string GetTrailingComments(const grpc::string prefix) const {
    return GetCommentsHelper(file_, false, prefix);
  }

  vector<grpc::string> GetAllComments() const {
    return grpc_python_generator::get_all_comments(file_);
  }

 private:
  const grpc::protobuf::FileDescriptor *file_;
};

#endif  // GRPC_INTERNAL_COMPILER_PROTOBUF_PLUGIN_H
