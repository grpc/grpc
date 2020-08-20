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

#include <map>

#include <google/protobuf/compiler/php/php_generator.h>
#include "src/compiler/config.h"
#include "src/compiler/generator_helpers.h"
#include "src/compiler/php_generator_helpers.h"

using google::protobuf::compiler::php::GeneratedClassName;
using grpc::protobuf::Descriptor;
using grpc::protobuf::FileDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::io::Printer;
using grpc::protobuf::io::StringOutputStream;
using std::map;

namespace grpc_php_generator {
namespace {

std::string ConvertToPhpNamespace(const std::string& name) {
  std::vector<std::string> tokens = grpc_generator::tokenize(name, ".");
  std::ostringstream oss;
  for (unsigned int i = 0; i < tokens.size(); i++) {
    oss << (i == 0 ? "" : "\\")
        << grpc_generator::CapitalizeFirstLetter(tokens[i]);
  }
  return oss.str();
}

std::string PackageName(const FileDescriptor* file) {
  if (file->options().has_php_namespace()) {
    return file->options().php_namespace();
  } else {
    return ConvertToPhpNamespace(file->package());
  }
}

std::string MessageIdentifierName(const std::string& name,
                                  const FileDescriptor* file) {
  std::vector<std::string> tokens = grpc_generator::tokenize(name, ".");
  std::ostringstream oss;
  if (PackageName(file) != "") {
    oss << PackageName(file) << "\\";
  }
  oss << grpc_generator::CapitalizeFirstLetter(tokens[tokens.size() - 1]);
  return oss.str();
}

void PrintMethod(const MethodDescriptor* method, Printer* out) {
  const Descriptor* input_type = method->input_type();
  const Descriptor* output_type = method->output_type();
  map<std::string, std::string> vars;
  vars["service_name"] = method->service()->full_name();
  vars["name"] = method->name();
  vars["input_type_id"] =
      MessageIdentifierName(GeneratedClassName(input_type), input_type->file());
  vars["output_type_id"] = MessageIdentifierName(
      GeneratedClassName(output_type), output_type->file());

  out->Print("/**\n");
  out->Print(GetPHPComments(method, " *").c_str());
  if (method->client_streaming()) {
    if (method->server_streaming()) {
      vars["return_type_id"] = "\\Grpc\\BidiStreamingCall";
    } else {
      vars["return_type_id"] = "\\Grpc\\ClientStreamingCall";
    }
    out->Print(vars,
               " * @param array $$metadata metadata\n"
               " * @param array $$options call options\n"
               " * @return $return_type_id$\n */\n"
               "public function $name$($$metadata = [], "
               "$$options = []) {\n");
    out->Indent();
    out->Indent();
    if (method->server_streaming()) {
      out->Print("return $$this->_bidiRequest(");
    } else {
      out->Print("return $$this->_clientStreamRequest(");
    }
    out->Print(vars,
               "'/$service_name$/$name$',\n"
               "['\\$output_type_id$','decode'],\n"
               "$$metadata, $$options);\n");
  } else {
    if (method->server_streaming()) {
      vars["return_type_id"] = "\\Grpc\\ServerStreamingCall";
    } else {
      vars["return_type_id"] = "\\Grpc\\UnaryCall";
    }
    out->Print(vars,
               " * @param \\$input_type_id$ $$argument input argument\n"
               " * @param array $$metadata metadata\n"
               " * @param array $$options call options\n"
               " * @return $return_type_id$\n */\n"
               "public function $name$(\\$input_type_id$ $$argument,\n"
               "  $$metadata = [], $$options = []) {\n");
    out->Indent();
    out->Indent();
    if (method->server_streaming()) {
      out->Print("return $$this->_serverStreamRequest(");
    } else {
      out->Print("return $$this->_simpleRequest(");
    }
    out->Print(vars,
               "'/$service_name$/$name$',\n"
               "$$argument,\n"
               "['\\$output_type_id$', 'decode'],\n"
               "$$metadata, $$options);\n");
  }
  out->Outdent();
  out->Outdent();
  out->Print("}\n\n");
}

// Prints out the service descriptor object
void PrintService(const ServiceDescriptor* service,
                  const std::string& class_suffix, Printer* out) {
  map<std::string, std::string> vars;
  out->Print("/**\n");
  out->Print(GetPHPComments(service, " *").c_str());
  out->Print(" */\n");
  vars["name"] = GetPHPServiceClassname(service, class_suffix);
  out->Print(vars, "class $name$ extends \\Grpc\\BaseStub {\n\n");
  out->Indent();
  out->Indent();
  out->Print(
      "/**\n * @param string $$hostname hostname\n"
      " * @param array $$opts channel options\n"
      " * @param \\Grpc\\Channel $$channel (optional) re-use channel "
      "object\n */\n"
      "public function __construct($$hostname, $$opts, "
      "$$channel = null) {\n");
  out->Indent();
  out->Indent();
  out->Print("parent::__construct($$hostname, $$opts, $$channel);\n");
  out->Outdent();
  out->Outdent();
  out->Print("}\n\n");
  for (int i = 0; i < service->method_count(); i++) {
    std::string method_name =
        grpc_generator::LowercaseFirstLetter(service->method(i)->name());
    PrintMethod(service->method(i), out);
  }
  out->Outdent();
  out->Outdent();
  out->Print("}\n");
}
}  // namespace

std::string GenerateFile(const FileDescriptor* file,
                         const ServiceDescriptor* service,
                         const std::string& class_suffix) {
  std::string output;
  {
    StringOutputStream output_stream(&output);
    Printer out(&output_stream, '$');

    out.Print("<?php\n");
    out.Print("// GENERATED CODE -- DO NOT EDIT!\n\n");

    std::string leading_comments = GetPHPComments(file, "//");
    if (!leading_comments.empty()) {
      out.Print("// Original file comments:\n");
      out.PrintRaw(leading_comments.c_str());
    }

    map<std::string, std::string> vars;
    std::string php_namespace = PackageName(file);
    vars["package"] = php_namespace;
    out.Print(vars, "namespace $package$;\n\n");

    PrintService(service, class_suffix, &out);
  }
  return output;
}

}  // namespace grpc_php_generator
