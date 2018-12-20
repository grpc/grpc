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
#include <unordered_set>

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
using std::unordered_set;

namespace grpc_ruby_generator {
namespace {
// Looks recursively (BFS) through all FileDescriptor dependencies for the
// input/output message package. Should only be run if ruby_package option is
// set
void GetInputOutputPackage(const FileDescriptor *file,
                           const grpc::string& package,
                           const Descriptor* input_desc,
                           const Descriptor* output_desc,
                           grpc::string& input_package,
                           grpc::string& output_package) {
  const grpc::string& input_name = input_desc->name();
  const grpc::string& output_name = output_desc->name();
  const grpc::string& input_full_name = input_desc->full_name();
  const grpc::string& output_full_name = output_desc->full_name();

  bool found_input_def = false, found_output_def = false;

  vector<const FileDescriptor*> pending;
  unordered_set<const FileDescriptor*> seen;
  pending.push_back(file);

  while (!pending.empty()) {
    // pop a FileDescriptor out of the pending queue
    const FileDescriptor* working = pending[0];
    pending.erase(pending.begin());

    // iterate through all the dependency to see if input_name is defined
    int dep_cnt = working->dependency_count();
    for (int i = 0; i < dep_cnt; ++i) {
      const FileDescriptor* dep = file->dependency(i);
      // if dep is not yet seen, add to pending queue
      if (seen.find(dep) == seen.end()) {
        pending.push_back(dep);
      }

      // if a dependency defines its messages in the same ruby namespace
      if (dep->options().has_ruby_package() &&
          dep->options().ruby_package() == package) {
        // look if input/output is defined in this file
        auto found_input_desc = dep->FindMessageTypeByName(input_name);
        auto found_output_desc = dep->FindMessageTypeByName(output_name);

        // if message is define and matches the fully qualified name,
        // get the package name of that file
        if (found_input_desc &&
            found_input_desc->full_name() == input_full_name) {
          input_package = dep->package();
          found_input_def = true;
        }
        if (found_output_desc &&
            found_output_desc->full_name() == output_full_name) {
          output_package = dep->package();
          found_output_def = true;
        }
      }
      // return as soon as both input and output packages are found
      if (found_input_def && found_output_def)
        return;
    }
  }
}

// Prints out the method using the ruby gRPC DSL.
void PrintMethod(const MethodDescriptor* method, const grpc::string& package,
                 Printer* out) {
  const FileDescriptor *file = method->file();
  grpc::string input_package = package, output_package = package;

  // only bfs if we really need to
  if (file->options().has_ruby_package()) {
    auto input_desc = method->input_type();
    auto output_desc = method->output_type();

    GetInputOutputPackage(file, package,
                          input_desc, output_desc,
                          input_package, output_package);
  }

  grpc::string input_type =
      RubyTypeOf(method->input_type()->full_name(), input_package);
  if (method->client_streaming()) {
    input_type = "stream(" + input_type + ")";
  }
  // if the package changed, append the correct namespace
  if (input_package != package) {
    input_type = RubyTypeOf(package, "") + "::" + input_type;
  }
  grpc::string output_type =
      RubyTypeOf(method->output_type()->full_name(), output_package);
  if (method->server_streaming()) {
    output_type = "stream(" + output_type + ")";
  }
  // if the package changed, append the correct namespace
  if (output_package != package) {
    output_type = RubyTypeOf(package, "") + "::" + output_type;
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
