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

void PrintServerMethod(const MethodDescriptor* method, Printer* out) {
  map<std::string, std::string> vars;
  const Descriptor* input_type = method->input_type();
  const Descriptor* output_type = method->output_type();
  vars["service_name"] = method->service()->full_name();
  vars["method_name"] = method->name();
  vars["input_type_id"] =
      MessageIdentifierName(GeneratedClassName(input_type), input_type->file());
  vars["output_type_id"] = MessageIdentifierName(
      GeneratedClassName(output_type), output_type->file());

  out->Print("/**\n");
  out->Print(GetPHPComments(method, " *").c_str());

  const char* method_template;
  if (method->client_streaming() && method->server_streaming()) {
    method_template =
        " * @param \\Grpc\\ServerCallReader $$reader read client request data "
        "of \\$input_type_id$\n"
        " * @param \\Grpc\\ServerCallWriter $$writer write response data of "
        "\\$output_type_id$\n"
        " * @param \\Grpc\\ServerContext $$context server request context\n"
        " * @return void\n"
        " */\n"
        "public function $method_name$(\n"
        "    \\Grpc\\ServerCallReader $$reader,\n"
        "    \\Grpc\\ServerCallWriter $$writer,\n"
        "    \\Grpc\\ServerContext $$context\n"
        "): void {\n"
        "    $$context->setStatus(\\Grpc\\Status::unimplemented());\n"
        "    $$writer->finish();\n"
        "}\n\n";
  } else if (method->client_streaming()) {
    method_template =
        " * @param \\Grpc\\ServerCallReader $$reader read client request data "
        "of \\$input_type_id$\n"
        " * @param \\Grpc\\ServerContext $$context server request context\n"
        " * @return \\$output_type_id$ for response data, null if if error "
        "occured\n"
        " *     initial metadata (if any) and status (if not ok) should be set "
        "to $$context\n"
        " */\n"
        "public function $method_name$(\n"
        "    \\Grpc\\ServerCallReader $$reader,\n"
        "    \\Grpc\\ServerContext $$context\n"
        "): ?\\$output_type_id$ {\n"
        "    $$context->setStatus(\\Grpc\\Status::unimplemented());\n"
        "    return null;\n"
        "}\n\n";
  } else if (method->server_streaming()) {
    method_template =
        " * @param \\$input_type_id$ $$request client request\n"
        " * @param \\Grpc\\ServerCallWriter $$writer write response data of "
        "\\$output_type_id$\n"
        " * @param \\Grpc\\ServerContext $$context server request context\n"
        " * @return void\n"
        " */\n"
        "public function $method_name$(\n"
        "    \\$input_type_id$ $$request,\n"
        "    \\Grpc\\ServerCallWriter $$writer,\n"
        "    \\Grpc\\ServerContext $$context\n"
        "): void {\n"
        "    $$context->setStatus(\\Grpc\\Status::unimplemented());\n"
        "    $$writer->finish();\n"
        "}\n\n";
  } else {
    method_template =
        " * @param \\$input_type_id$ $$request client request\n"
        " * @param \\Grpc\\ServerContext $$context server request context\n"
        " * @return \\$output_type_id$ for response data, null if if error "
        "occured\n"
        " *     initial metadata (if any) and status (if not ok) should be set "
        "to $$context\n"
        " */\n"
        "public function $method_name$(\n"
        "    \\$input_type_id$ $$request,\n"
        "    \\Grpc\\ServerContext $$context\n"
        "): ?\\$output_type_id$ {\n"
        "    $$context->setStatus(\\Grpc\\Status::unimplemented());\n"
        "    return null;\n"
        "}\n\n";
  }
  out->Print(vars, method_template);
}

void PrintServerMethodDescriptors(const ServiceDescriptor* service,
                                  Printer* out) {
  map<std::string, std::string> vars;
  vars["service_name"] = service->full_name();

  out->Print(
      "/**\n"
      " * Get the method descriptors of the service for server registration\n"
      " *\n"
      " * @return array of \\Grpc\\MethodDescriptor for the service methods\n"
      " */\n"
      "public final function getMethodDescriptors(): array\n{\n");
  out->Indent();
  out->Indent();
  out->Print("return [\n");
  out->Indent();
  out->Indent();
  for (int i = 0; i < service->method_count(); i++) {
    auto method = service->method(i);
    auto input_type = method->input_type();
    vars["method_name"] = method->name();
    vars["input_type_id"] = MessageIdentifierName(
        GeneratedClassName(input_type), input_type->file());
    if (method->client_streaming() && method->server_streaming()) {
      vars["call_type"] = "BIDI_STREAMING_CALL";
    } else if (method->client_streaming()) {
      vars["call_type"] = "CLIENT_STREAMING_CALL";
    } else if (method->server_streaming()) {
      vars["call_type"] = "SERVER_STREAMING_CALL";
    } else {
      vars["call_type"] = "UNARY_CALL";
    }
    out->Print(
        vars,
        "'/$service_name$/$method_name$' => new \\Grpc\\MethodDescriptor(\n"
        "    $$this,\n"
        "    '$method_name$',\n"
        "    '\\$input_type_id$',\n"
        "    \\Grpc\\MethodDescriptor::$call_type$\n"
        "),\n");
  }
  out->Outdent();
  out->Outdent();
  out->Print("];\n");
  out->Outdent();
  out->Outdent();
  out->Print("}\n\n");
}

// Prints out the service descriptor object
void PrintService(const ServiceDescriptor* service,
                  const std::string& class_suffix, bool is_server,
                  Printer* out) {
  map<std::string, std::string> vars;
  out->Print("/**\n");
  out->Print(GetPHPComments(service, " *").c_str());
  out->Print(" */\n");
  vars["name"] = GetPHPServiceClassname(service, class_suffix, is_server);
  vars["extends"] = is_server ? "" : "extends \\Grpc\\BaseStub ";
  out->Print(vars, "class $name$ $extends${\n\n");
  out->Indent();
  out->Indent();
  if (!is_server) {
    out->Print(
        "/**\n * @param string $$hostname hostname\n"
        " * @param array $$opts channel options\n"
        " * @param \\Grpc\\Channel $$channel (optional) re-use channel object\n"
        " */\n"
        "public function __construct($$hostname, $$opts, "
        "$$channel = null) {\n");
    out->Indent();
    out->Indent();
    out->Print("parent::__construct($$hostname, $$opts, $$channel);\n");
    out->Outdent();
    out->Outdent();
    out->Print("}\n\n");
  }
  for (int i = 0; i < service->method_count(); i++) {
    if (is_server) {
      PrintServerMethod(service->method(i), out);
    } else {
      PrintMethod(service->method(i), out);
    }
  }
  if (is_server) {
    PrintServerMethodDescriptors(service, out);
  }
  out->Outdent();
  out->Outdent();
  out->Print("}\n");
}
}  // namespace

std::string GenerateFile(const FileDescriptor* file,
                         const ServiceDescriptor* service,
                         const std::string& class_suffix, bool is_server) {
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

    PrintService(service, class_suffix, is_server, &out);
  }
  return output;
}

}  // namespace grpc_php_generator
