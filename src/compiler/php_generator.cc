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

#include <map>

#include "src/compiler/config.h"
#include "src/compiler/generator_helpers.h"
#include "src/compiler/php_generator_helpers.h"

using grpc::protobuf::FileDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::Descriptor;
using grpc::protobuf::io::Printer;
using grpc::protobuf::io::StringOutputStream;
using std::map;

namespace grpc_php_generator {
namespace {

grpc::string MessageIdentifierName(const grpc::string &name) {
  std::vector<grpc::string> tokens = grpc_generator::tokenize(name, ".");
  std::ostringstream oss;
  for (unsigned int i = 0; i < tokens.size(); i++) {
    oss << (i == 0 ? "" : "\\")
        << grpc_generator::CapitalizeFirstLetter(tokens[i]);
  }
  return oss.str();
}

void PrintMethod(const MethodDescriptor *method, Printer *out) {
  const Descriptor *input_type = method->input_type();
  const Descriptor *output_type = method->output_type();
  map<grpc::string, grpc::string> vars;
  vars["service_name"] = method->service()->full_name();
  vars["name"] = method->name();
  vars["input_type_id"] = MessageIdentifierName(input_type->full_name());
  vars["output_type_id"] = MessageIdentifierName(output_type->full_name());

  out->Print("/**\n");
  out->Print(GetPHPComments(method, " *").c_str());
  if (method->client_streaming()) {
    out->Print(vars,
               " * @param array $$metadata metadata\n"
               " * @param array $$options call options\n */\n"
               "public function $name$($$metadata = [], "
               "$$options = []) {\n");
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
  out->Print("}\n\n");
}

// Prints out the service descriptor object
void PrintService(const ServiceDescriptor *service, Printer *out) {
  map<grpc::string, grpc::string> vars;
  out->Print(GetPHPComments(service, "//").c_str());
  vars["name"] = service->name();
  out->Print(vars, "class $name$Client extends \\Grpc\\BaseStub {\n\n");
  out->Indent();
  out->Print(
      "/**\n * @param string $$hostname hostname\n"
      " * @param array $$opts channel options\n"
      " * @param \\Grpc\\Channel $$channel (optional) re-use channel "
      "object\n */\n"
      "public function __construct($$hostname, $$opts, "
      "$$channel = null) {\n");
  out->Indent();
  out->Print("parent::__construct($$hostname, $$opts, $$channel);\n");
  out->Outdent();
  out->Print("}\n\n");
  for (int i = 0; i < service->method_count(); i++) {
    grpc::string method_name =
        grpc_generator::LowercaseFirstLetter(service->method(i)->name());
    PrintMethod(service->method(i), out);
  }
  out->Outdent();
  out->Print("}\n\n");
}
}

grpc::string GenerateFile(const FileDescriptor *file,
                          const ServiceDescriptor *service) {
  grpc::string output;
  {
    StringOutputStream output_stream(&output);
    Printer out(&output_stream, '$');

    out.Print("<?php\n");
    out.Print("// GENERATED CODE -- DO NOT EDIT!\n\n");

    grpc::string leading_comments = GetPHPComments(file, "//");
    if (!leading_comments.empty()) {
      out.Print("// Original file comments:\n");
      out.Print(leading_comments.c_str());
    }

    map<grpc::string, grpc::string> vars;
    vars["package"] = MessageIdentifierName(file->package());
    out.Print(vars, "namespace $package$ {\n\n");
    out.Indent();

    PrintService(service, &out);

    out.Outdent();
    out.Print("}\n");
  }
  return output;
}

}  // namespace grpc_php_generator
