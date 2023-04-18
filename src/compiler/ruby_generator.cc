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

#include "src/compiler/ruby_generator.h"

#include <cctype>
#include <map>
#include <vector>

#include "src/compiler/config.h"
#include "src/compiler/ruby_generator_helpers-inl.h"
#include "src/compiler/ruby_generator_map-inl.h"
#include "src/compiler/ruby_generator_string-inl.h"

using grpc::protobuf::FileDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::io::Printer;
using grpc::protobuf::io::StringOutputStream;
using std::map;
using std::vector;

namespace grpc_ruby_generator {
namespace {

// Prints out the method using the ruby gRPC DSL.
void PrintMethod(const MethodDescriptor* method, Printer* out) {
  std::string input_type = RubyTypeOf(method->input_type());
  if (method->client_streaming()) {
    input_type = "stream(" + input_type + ")";
  }
  std::string output_type = RubyTypeOf(method->output_type());
  if (method->server_streaming()) {
    output_type = "stream(" + output_type + ")";
  }
  std::map<std::string, std::string> method_vars = ListToDict({
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
void PrintService(const ServiceDescriptor* service, Printer* out) {
  if (service->method_count() == 0) {
    return;
  }

  // Begin the service module
  std::map<std::string, std::string> module_vars = ListToDict({
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
  out->Print("include ::GRPC::GenericService\n");
  out->Print("\n");
  out->Print("self.marshal_class_method = :encode\n");
  out->Print("self.unmarshal_class_method = :decode\n");
  std::map<std::string, std::string> pkg_vars =
      ListToDict({"service_full_name", service->full_name()});
  out->Print(pkg_vars, "self.service_name = '$service_full_name$'\n");
  out->Print("\n");
  for (int i = 0; i < service->method_count(); ++i) {
    PrintMethod(service->method(i), out);
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
// https://github.com/protocolbuffers/protobuf/blob/63895855d7b1298bee97591cbafced49f23902da/src/google/protobuf/compiler/ruby/ruby_generator.cc#L312
// TODO: keep up to date with protoc code generation, though this behavior isn't
// expected to change

// Locale-agnostic utility functions.
bool IsLower(char ch) { return ch >= 'a' && ch <= 'z'; }

bool IsUpper(char ch) { return ch >= 'A' && ch <= 'Z'; }

bool IsAlpha(char ch) { return IsLower(ch) || IsUpper(ch); }

char UpperChar(char ch) { return IsLower(ch) ? (ch - 'a' + 'A') : ch; }

// Package names in protobuf are snake_case by convention, but Ruby module
// names must be PascalCased.
//
//   foo_bar_baz -> FooBarBaz
std::string PackageToModule(const std::string& name) {
  bool next_upper = true;
  std::string result;
  result.reserve(name.size());

  for (std::string::size_type i = 0; i < name.size(); i++) {
    if (name[i] == '_') {
      next_upper = true;
    } else {
      if (next_upper) {
        result.push_back(UpperChar(name[i]));
      } else {
        result.push_back(name[i]);
      }
      next_upper = false;
    }
  }

  return result;
}

// Class and enum names in protobuf should be PascalCased by convention, but
// since there is nothing enforcing this we need to ensure that they are valid
// Ruby constants.  That mainly means making sure that the first character is
// an upper-case letter.
std::string RubifyConstant(const std::string& name) {
  std::string ret = name;
  if (!ret.empty()) {
    if (IsLower(ret[0])) {
      // If it starts with a lowercase letter, capitalize it.
      ret[0] = UpperChar(ret[0]);
    } else if (!IsAlpha(ret[0])) {
      // Otherwise (e.g. if it begins with an underscore), we need to come up
      // with some prefix that starts with a capital letter. We could be smarter
      // here, e.g. try to strip leading underscores, but this may cause other
      // problems if the user really intended the name. So let's just prepend a
      // well-known suffix.
      ret = "PB_" + ret;
    }
  }

  return ret;
}
// end copying of protoc generator for ruby code

std::string GetServices(const FileDescriptor* file) {
  std::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.

    StringOutputStream output_stream(&output);
    Printer out(&output_stream, '$');

    // Don't write out any output if there no services, to avoid empty service
    // files being generated for proto files that don't declare any.
    if (file->service_count() == 0) {
      return output;
    }

    std::string package_name = RubyPackage(file);

    // Write out a file header.
    std::map<std::string, std::string> header_comment_vars = ListToDict({
        "file.name",
        file->name(),
        "file.package",
        package_name,
    });
    out.Print("# Generated by the protocol buffer compiler.  DO NOT EDIT!\n");
    out.Print(header_comment_vars,
              "# Source: $file.name$ for package '$file.package$'\n");

    std::string leading_comments = GetRubyComments(file, true);
    if (!leading_comments.empty()) {
      out.Print("# Original file comments:\n");
      out.PrintRaw(leading_comments.c_str());
    }

    out.Print("\n");
    out.Print("require 'grpc'\n");
    // Write out require statemment to import the separately generated file
    // that defines the messages used by the service. This is generated by the
    // main ruby plugin.
    std::map<std::string, std::string> dep_vars = ListToDict({
        "dep.name",
        MessagesRequireName(file),
    });
    out.Print(dep_vars, "require '$dep.name$'\n");

    // Write out services within the modules
    out.Print("\n");
    std::vector<std::string> modules = Split(package_name, '.');
    for (size_t i = 0; i < modules.size(); ++i) {
      std::map<std::string, std::string> module_vars = ListToDict({
          "module.name",
          PackageToModule(modules[i]),
      });
      out.Print(module_vars, "module $module.name$\n");
      out.Indent();
    }
    for (int i = 0; i < file->service_count(); ++i) {
      auto service = file->service(i);
      PrintService(service, &out);
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
