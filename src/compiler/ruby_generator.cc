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
#include <unordered_map>

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
using std::unordered_multimap;
using std::unordered_map;

namespace grpc_ruby_generator {
namespace {

void CheckMessageRedef(unordered_set<grpc::string> &keySet,
                       unordered_multimap<grpc::string, const Descriptor*> &module_map) {
  bool redef = false;
  for (const grpc::string &key:keySet) {
    unordered_map<grpc::string, const Descriptor*> uniqueMap;
    auto rangePair = module_map.equal_range(key);
    for (auto it = rangePair.first; it!=rangePair.second; ++it) {
      const Descriptor* msg_desc = it->second;
      grpc::string msg_name = msg_desc->name();
      auto res = uniqueMap.insert({msg_name, msg_desc});
      // if failed insert, then there is a collision, print error
      if (!res.second) {
        std::cerr << "In Ruby module " << key << ": Message redefinition detected"
                  << "\n\tMessage " << msg_name << " defined in "
                  << msg_desc->file()->name() << "\n\tAlso defined in "
                  << uniqueMap[msg_name]->file()->name() << std::endl;
        redef = true;
      }
    }
  }
  if (redef) {
    std::cerr << "Redefinitions detected, aborting\n";
    exit(1);
  }
}

void GetCurrentContext(const FileDescriptor *file,
                       unordered_multimap<grpc::string, const Descriptor*> &module_map,
                       unordered_map<grpc::string, grpc::string> &package_map) {
  unordered_set<grpc::string> keySet;
  unordered_set<const FileDescriptor*> seen;
  vector <const FileDescriptor*> pending;

  pending.push_back(file);

  // bfs through dependencies
  while (!pending.empty()) {
    // pop from queue
    const FileDescriptor *working = pending.front();
    pending.erase(pending.begin());

    // mark working as seen
    seen.insert(working);

    // push all unseen neighbors onto pending
    int dep_cnt = working->dependency_count();
    for (int i = 0; i < dep_cnt; ++i) {
      const FileDescriptor *dep = working->dependency(i);
      if (seen.find(dep) == seen.end()) {
        pending.push_back(dep);
      }
    }

    // if ruby_package is defined by the user, create a mapping between the
    // proto package and the ruby_package
    grpc::string ruby_module;
    if (working->options().has_ruby_package()) {
      ruby_module = working->options().ruby_package();
      package_map.insert({working->package(), ruby_module});
    }
    else {
      ruby_module = working->package();
      ruby_module = Modularize(ruby_module);
    }

    // create mapping from namespace to Message types
    std::pair<grpc::string, const Descriptor*> module_message;
    module_message.first = ruby_module;
    keySet.insert(ruby_module);
    for (int j = 0; j < working->message_type_count(); ++j) {
      module_message.second = working->message_type(j);
      module_map.insert(module_message);
    }
  }

  // print out MODULE_MAP
  // for debugging
  for (auto& x:module_map) {
    std::cerr << x.first << ": " << x.second->full_name() << std::endl;
  }

  for (auto& x:package_map) {
    std::cerr << x.first << ": " << x.second << std::endl;
  }

  CheckMessageRedef(keySet, module_map);
}

grpc::string ReplaceLongestCommonPrefix(const grpc::string &method_name,
                                        unordered_map<grpc::string, grpc::string> &package_map) {
  grpc::string res(method_name);
  grpc::string longestCommonPrefix;
  for (const auto& pair:package_map) {
    grpc::string protoPackage = pair.first;
    if (protoPackage.length() < method_name.length() &&
        method_name.substr(0, protoPackage.length()) == protoPackage &&
        protoPackage.length() > longestCommonPrefix.length()) {
      longestCommonPrefix = protoPackage;
    }
  }
  if (longestCommonPrefix.length() > 0) {
    grpc::string to = package_map.at(longestCommonPrefix);
    ReplacePrefix(&res, longestCommonPrefix, to);
  }
  return res;
}

// Prints out the method using the ruby gRPC DSL.
void PrintMethod(const MethodDescriptor* method, const grpc::string& package,
                 Printer* out) {
  auto module_map = new unordered_multimap <grpc::string, const Descriptor*>;
  auto package_map = new unordered_map <grpc::string, grpc::string>;

  GetCurrentContext(method->file(), *module_map, *package_map);

  grpc::string canonical_input_method =
          ReplaceLongestCommonPrefix(method->input_type()->full_name(),
                                     *package_map);
  grpc::string canonical_output_method =
          ReplaceLongestCommonPrefix(method->output_type()->full_name(),
                                     *package_map);

  grpc::string input_type = RubyTypeOf(canonical_input_method, package);

  if (method->client_streaming()) {
    input_type = "stream(" + input_type + ")";
  }
  grpc::string output_type = RubyTypeOf(canonical_output_method, package);
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

  free(module_map);
  free(package_map);
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
