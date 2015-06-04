/*
 *
 * Copyright 2015, Google Inc.
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

#include <cctype>
#include <map>
#include <vector>

#include "src/compiler/config.h"
#include "src/compiler/ruby_generator.h"
#include "src/compiler/ruby_generator_helpers-inl.h"
#include "src/compiler/ruby_generator_map-inl.h"
#include "src/compiler/ruby_generator_string-inl.h"

using grpc::protobuf::FileDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::io::Printer;
using grpc::protobuf::io::StringOutputStream;
using std::map;
using std::vector;

namespace grpc_ruby_generator {
namespace {

// Prints out the method using the ruby gRPC DSL.
void PrintMethod(const MethodDescriptor *method, const grpc::string &package,
                 Printer *out) {
  grpc::string input_type = RubyTypeOf(method->input_type()->name(), package);
  if (method->client_streaming()) {
    input_type = "stream(" + input_type + ")";
  }
  grpc::string output_type = RubyTypeOf(method->output_type()->name(), package);
  if (method->server_streaming()) {
    output_type = "stream(" + output_type + ")";
  }
  std::map<grpc::string, grpc::string> method_vars =
      ListToDict({"mth.name", method->name(), "input.type", input_type,
                  "output.type", output_type, });
  out->Print(method_vars, "rpc :$mth.name$, $input.type$, $output.type$\n");
}

// Prints out the service using the ruby gRPC DSL.
void PrintService(const ServiceDescriptor *service, const grpc::string &package,
                  Printer *out) {
  if (service->method_count() == 0) {
    return;
  }

  // Begin the service module
  std::map<grpc::string, grpc::string> module_vars =
      ListToDict({"module.name", CapitalizeFirst(service->name()), });
  out->Print(module_vars, "module $module.name$\n");
  out->Indent();

  // TODO(temiola): add documentation
  grpc::string doc = "TODO: add proto service documentation here";
  std::map<grpc::string, grpc::string> template_vars =
      ListToDict({"Documentation", doc, });
  out->Print("\n");
  out->Print(template_vars, "# $Documentation$\n");
  out->Print("class Service\n");

  // Write the indented class body.
  out->Indent();
  out->Print("\n");
  out->Print("include GRPC::GenericService\n");
  out->Print("\n");
  out->Print("self.marshal_class_method = :encode\n");
  out->Print("self.unmarshal_class_method = :decode\n");
  std::map<grpc::string, grpc::string> pkg_vars =
      ListToDict({"service.name", service->name(), "pkg.name", package, });
  out->Print(pkg_vars, "self.service_name = '$pkg.name$.$service.name$'\n");
  out->Print("\n");
  for (int i = 0; i < service->method_count(); ++i) {
    PrintMethod(service->method(i), package, out);
  }
  out->Outdent();

  out->Print("end\n");
  out->Print("\n");
  out->Print("Stub = Service.rpc_stub_class\n");

  // End the service module
  out->Outdent();
  out->Print("end\n");
}

}  // namespace

grpc::string GetServices(const FileDescriptor *file) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.

    StringOutputStream output_stream(&output);
    Printer out(&output_stream, '$');

    // Don't write out any output if there no services, to avoid empty service
    // files being generated for proto files that don't declare any.
    if (file->service_count() == 0) {
      return output;
    }

    // Write out a file header.
    std::map<grpc::string, grpc::string> header_comment_vars = ListToDict(
        {"file.name", file->name(), "file.package", file->package(), });
    out.Print("# Generated by the protocol buffer compiler.  DO NOT EDIT!\n");
    out.Print(header_comment_vars,
              "# Source: $file.name$ for package '$file.package$'\n");

    out.Print("\n");
    out.Print("require 'grpc'\n");
    // Write out require statemment to import the separately generated file
    // that defines the messages used by the service. This is generated by the
    // main ruby plugin.
    std::map<grpc::string, grpc::string> dep_vars =
        ListToDict({"dep.name", MessagesRequireName(file), });
    out.Print(dep_vars, "require '$dep.name$'\n");

    // Write out services within the modules
    out.Print("\n");
    std::vector<grpc::string> modules = Split(file->package(), '.');
    for (size_t i = 0; i < modules.size(); ++i) {
      std::map<grpc::string, grpc::string> module_vars =
          ListToDict({"module.name", CapitalizeFirst(modules[i]), });
      out.Print(module_vars, "module $module.name$\n");
      out.Indent();
    }
    for (int i = 0; i < file->service_count(); ++i) {
      auto service = file->service(i);
      PrintService(service, file->package(), &out);
    }
    for (size_t i = 0; i < modules.size(); ++i) {
      out.Outdent();
      out.Print("end\n");
    }
  }
  return output;
}

}  // namespace grpc_ruby_generator
