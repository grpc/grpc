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

#include "src/compiler/config.h"
#include "src/compiler/generator_helpers.h"
#include "src/compiler/php_generator_helpers.h"

using grpc::protobuf::Descriptor;
using grpc::protobuf::FileDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::io::Printer;
using grpc::protobuf::io::StringOutputStream;
using std::map;

namespace grpc_php_generator {
namespace {

grpc::string ConvertToPhpNamespace(const grpc::string& name) {
  std::vector<grpc::string> tokens = grpc_generator::tokenize(name, ".");
  std::ostringstream oss;
  for (unsigned int i = 0; i < tokens.size(); i++) {
    oss << (i == 0 ? "" : "\\")
        << grpc_generator::CapitalizeFirstLetter(tokens[i]);
  }
  return oss.str();
}

grpc::string PackageName(const FileDescriptor* file) {
  if (file->options().has_php_namespace()) {
    return file->options().php_namespace();
  } else {
    return ConvertToPhpNamespace(file->package());
  }
}

grpc::string MessageIdentifierName(const grpc::string& name,
                                   const FileDescriptor* file) {
  std::vector<grpc::string> tokens = grpc_generator::tokenize(name, ".");
  std::ostringstream oss;
  oss << PackageName(file) << "\\"
      << grpc_generator::CapitalizeFirstLetter(tokens[tokens.size() - 1]);
  return oss.str();
}

void PrintMethod(const MethodDescriptor* method, Printer* out) {
  const Descriptor* input_type = method->input_type();
  const Descriptor* output_type = method->output_type();
  map<grpc::string, grpc::string> vars;
  vars["service_name"] = method->service()->full_name();
  vars["name"] = method->name();
  vars["input_type_id"] =
      MessageIdentifierName(input_type->full_name(), input_type->file());
  vars["output_type_id"] =
      MessageIdentifierName(output_type->full_name(), output_type->file());

  out->Print("/**\n");
  out->Print(GetPHPComments(method, " *").c_str());
  if (method->client_streaming()) {
    out->Print(vars,
               " * @param array $$metadata metadata\n"
               " * @param array $$options call options\n */\n"
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
    out->Print(vars,
               " * @param \\$input_type_id$ $$argument input argument\n"
               " * @param array $$metadata metadata\n"
               " * @param array $$options call options\n */\n"
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
                  const grpc::string& class_suffix, Printer* out) {
  map<grpc::string, grpc::string> vars;
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
    grpc::string method_name =
        grpc_generator::LowercaseFirstLetter(service->method(i)->name());
    PrintMethod(service->method(i), out);
  }
  out->Outdent();
  out->Outdent();
  out->Print("}\n");
}
}  // namespace

grpc::string GenerateFile(const FileDescriptor* file,
                          const ServiceDescriptor* service,
                          const grpc::string& class_suffix) {
  grpc::string output;
  {
    StringOutputStream output_stream(&output);
    Printer out(&output_stream, '$');

    out.Print("<?php\n");
    out.Print("// GENERATED CODE -- DO NOT EDIT!\n\n");

    grpc::string leading_comments = GetPHPComments(file, "//");
    if (!leading_comments.empty()) {
      out.Print("// Original file comments:\n");
      out.PrintRaw(leading_comments.c_str());
    }

    map<grpc::string, grpc::string> vars;
    grpc::string php_namespace = PackageName(file);
    vars["package"] = php_namespace;
    out.Print(vars, "namespace $package$;\n\n");

    PrintService(service, class_suffix, &out);
  }
  return output;
}

}  // namespace grpc_php_generator
