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

#include <cctype>
#include <map>
#include <vector>

#include "src/compiler/config.h"
#include "src/compiler/ruby_generator.h"
#include "src/compiler/ruby_generator_helpers-inl.h"
#include "src/compiler/ruby_generator_map-inl.h"
#include "src/compiler/ruby_generator_string-inl.h"

using grpc::protobuf::FileDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::Descriptor;
using grpc::protobuf::io::Printer;
using grpc::protobuf::io::StringOutputStream;
using std::map;
using std::vector;

namespace grpc_ruby_generator {
namespace {

// returns the full canonical message name
grpc::string GetCanonicalMessageType(const Descriptor* msg) {
  const Descriptor* top_level_msg;
  const FileDescriptor* file_containing_msg;
  grpc::string msg_full_name;
  grpc::string msg_name;
  grpc::string msg_proto_pkg;
  grpc::string resolved_namespace;
  grpc::string res(msg->full_name());

  // If msg is nested, find the topmost message
  top_level_msg = msg;
  while (top_level_msg->containing_type()) {
    top_level_msg = top_level_msg->containing_type();
  }
  file_containing_msg = top_level_msg->file();

  msg_full_name = top_level_msg->full_name();
  msg_name = top_level_msg->name();
  msg_proto_pkg = msg_full_name.substr(0, msg_full_name.length() -
                                          msg_name.length());

  if (file_containing_msg->options().has_ruby_package()) {
    resolved_namespace = file_containing_msg->options().ruby_package();
  }
  // no need to do translation, full_name already include the correct package
  else {
    return res;
  }

  // remove trailing period
  if (msg_proto_pkg.back() == '.') {
    msg_proto_pkg.pop_back();
  }

  ReplacePrefix(&res, msg_proto_pkg, resolved_namespace);
  return res;
}

// Prints out the method using the ruby gRPC DSL.
void PrintMethod(const MethodDescriptor* method, const grpc::string& package,
                 Printer* out) {
  grpc::string canonical_input_type =
          GetCanonicalMessageType(method->input_type());
  grpc::string canonical_output_type =
          GetCanonicalMessageType(method->output_type());

  grpc::string input_type = RubyTypeOf(canonical_input_type, package);

  if (method->client_streaming()) {
    input_type = "stream(" + input_type + ")";
  }
  grpc::string output_type = RubyTypeOf(canonical_output_type, package);
  if (method->server_streaming()) {
    output_type = "stream(" + output_type + ")";
  }
  std::map<grpc::string, grpc::string> method_vars = ListToDict({
      "mth.name",
      method->name(),
      "input.type",
      input_type,
      "output.type",
      output_type,
  });
  out->Print(GetRubyComments(method, true).c_str());
  out->Print(method_vars, "rpc :$mth.name$, $input.type$, $output.type$\n");
  out->Print(GetRubyComments(method, false).c_str());
}

// Prints out the service using the ruby gRPC DSL.
void PrintService(const ServiceDescriptor* service, const grpc::string& package,
                  Printer* out) {
  if (service->method_count() == 0) {
    return;
  }

  // Begin the service module
  std::map<grpc::string, grpc::string> module_vars = ListToDict({
      "module.name",
      Modularize(service->name()),
  });
  out->Print(module_vars, "module $module.name$\n");
  out->Indent();

  out->Print(GetRubyComments(service, true).c_str());
  out->Print("class Service\n");

  // Write the indented class body.
  out->Indent();
  out->Print("\n");
  out->Print("include GRPC::GenericService\n");
  out->Print("\n");
  out->Print("self.marshal_class_method = :encode\n");
  out->Print("self.unmarshal_class_method = :decode\n");
  std::map<grpc::string, grpc::string> pkg_vars =
      ListToDict({"service_full_name", service->full_name()});
  out->Print(pkg_vars, "self.service_name = '$service_full_name$'\n");
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
  out->Print(GetRubyComments(service, false).c_str());
}

}  // namespace

// The following functions are copied directly from the source for the protoc
// ruby generator
// to ensure compatibility (with the exception of int and string type changes).
// See
// https://github.com/google/protobuf/blob/master/src/google/protobuf/compiler/ruby/ruby_generator.cc#L250
// TODO: keep up to date with protoc code generation, though this behavior isn't
// expected to change
bool IsLower(char ch) { return ch >= 'a' && ch <= 'z'; }

char ToUpper(char ch) { return IsLower(ch) ? (ch - 'a' + 'A') : ch; }

// Package names in protobuf are snake_case by convention, but Ruby module
// names must be PascalCased.
//
//   foo_bar_baz -> FooBarBaz
grpc::string PackageToModule(const grpc::string& name) {
  bool next_upper = true;
  grpc::string result;
  result.reserve(name.size());

  for (grpc::string::size_type i = 0; i < name.size(); i++) {
    if (name[i] == '_') {
      next_upper = true;
    } else {
      if (next_upper) {
        result.push_back(ToUpper(name[i]));
      } else {
        result.push_back(name[i]);
      }
      next_upper = false;
    }
  }

  return result;
}
// end copying of protoc generator for ruby code

grpc::string GetServices(const FileDescriptor* file) {
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

    std::string package_name;

    if (file->options().has_ruby_package()) {
      package_name = file->options().ruby_package();
    } else {
      package_name = file->package();
    }

    // Write out a file header.
    std::map<grpc::string, grpc::string> header_comment_vars = ListToDict({
        "file.name",
        file->name(),
        "file.package",
        package_name,
    });
    out.Print("# Generated by the protocol buffer compiler.  DO NOT EDIT!\n");
    out.Print(header_comment_vars,
              "# Source: $file.name$ for package '$file.package$'\n");

    grpc::string leading_comments = GetRubyComments(file, true);
    if (!leading_comments.empty()) {
      out.Print("# Original file comments:\n");
      out.PrintRaw(leading_comments.c_str());
    }

    out.Print("\n");
    out.Print("require 'grpc'\n");
    // Write out require statement to import the separately generated file
    // that defines the messages used by the service. This is generated by the
    // main ruby plugin.
    std::map<grpc::string, grpc::string> dep_vars = ListToDict({
        "dep.name",
        MessagesRequireName(file),
    });
    out.Print(dep_vars, "require '$dep.name$'\n");

    // Write out services within the modules
    out.Print("\n");
    std::vector<grpc::string> modules = Split(package_name, '.');
    for (size_t i = 0; i < modules.size(); ++i) {
      std::map<grpc::string, grpc::string> module_vars = ListToDict({
          "module.name",
          PackageToModule(modules[i]),
      });
      out.Print(module_vars, "module $module.name$\n");
      out.Indent();
    }
    for (int i = 0; i < file->service_count(); ++i) {
      auto service = file->service(i);
      PrintService(service, package_name, &out);
    }
    for (size_t i = 0; i < modules.size(); ++i) {
      out.Outdent();
      out.Print("end\n");
    }

    out.Print(GetRubyComments(file, false).c_str());
  }
  return output;
}

}  // namespace grpc_ruby_generator
