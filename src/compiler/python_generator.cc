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

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <tuple>
#include <vector>

#include <grpc++/config.h>
#include "src/compiler/config.h"
#include "src/compiler/generator_helpers.h"
#include "src/compiler/python_generator.h"

using grpc_generator::StringReplace;
using grpc_generator::StripProto;
using grpc::protobuf::Descriptor;
using grpc::protobuf::FileDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::compiler::GeneratorContext;
using grpc::protobuf::io::CodedOutputStream;
using grpc::protobuf::io::Printer;
using grpc::protobuf::io::StringOutputStream;
using grpc::protobuf::io::ZeroCopyOutputStream;
using std::initializer_list;
using std::make_pair;
using std::map;
using std::pair;
using std::replace;
using std::vector;

namespace grpc_python_generator {

PythonGrpcGenerator::PythonGrpcGenerator(const GeneratorConfiguration& config)
    : config_(config) {}

PythonGrpcGenerator::~PythonGrpcGenerator() {}

bool PythonGrpcGenerator::Generate(
    const FileDescriptor* file, const grpc::string& parameter,
    GeneratorContext* context, grpc::string* error) const {
  // Get output file name.
  grpc::string file_name;
  static const int proto_suffix_length = strlen(".proto");
  if (file->name().size() > static_cast<size_t>(proto_suffix_length) &&
      file->name().find_last_of(".proto") == file->name().size() - 1) {
    file_name = file->name().substr(
        0, file->name().size() - proto_suffix_length) + "_pb2.py";
  } else {
    *error = "Invalid proto file name. Proto file must end with .proto";
    return false;
  }

  std::unique_ptr<ZeroCopyOutputStream> output(
      context->OpenForInsert(file_name, "module_scope"));
  CodedOutputStream coded_out(output.get());
  bool success = false;
  grpc::string code = "";
  tie(success, code) = grpc_python_generator::GetServices(file, config_);
  if (success) {
    coded_out.WriteRaw(code.data(), code.size());
    return true;
  } else {
    return false;
  }
}

namespace {
//////////////////////////////////
// BEGIN FORMATTING BOILERPLATE //
//////////////////////////////////

// Converts an initializer list of the form { key0, value0, key1, value1, ... }
// into a map of key* to value*. Is merely a readability helper for later code.
map<grpc::string, grpc::string> ListToDict(
    const initializer_list<grpc::string>& values) {
  assert(values.size() % 2 == 0);
  map<grpc::string, grpc::string> value_map;
  auto value_iter = values.begin();
  for (unsigned i = 0; i < values.size()/2; ++i) {
    grpc::string key = *value_iter;
    ++value_iter;
    grpc::string value = *value_iter;
    value_map[key] = value;
    ++value_iter;
  }
  return value_map;
}

// Provides RAII indentation handling. Use as:
// {
//   IndentScope raii_my_indent_var_name_here(my_py_printer);
//   // constructor indented my_py_printer
//   ...
//   // destructor called at end of scope, un-indenting my_py_printer
// }
class IndentScope {
 public:
  explicit IndentScope(Printer* printer) : printer_(printer) {
    printer_->Indent();
  }

  ~IndentScope() {
    printer_->Outdent();
  }

 private:
  Printer* printer_;
};

////////////////////////////////
// END FORMATTING BOILERPLATE //
////////////////////////////////

bool PrintServicer(const ServiceDescriptor* service,
                   Printer* out) {
  grpc::string doc = "<fill me in later!>";
  map<grpc::string, grpc::string> dict = ListToDict({
        "Service", service->name(),
        "Documentation", doc,
      });
  out->Print(dict, "class EarlyAdopter$Service$Servicer(object):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print(dict, "\"\"\"$Documentation$\"\"\"\n");
    out->Print("__metaclass__ = abc.ABCMeta\n");
    for (int i = 0; i < service->method_count(); ++i) {
      auto meth = service->method(i);
      grpc::string arg_name = meth->client_streaming() ?
          "request_iterator" : "request";
      out->Print("@abc.abstractmethod\n");
      out->Print("def $Method$(self, $ArgName$, context):\n",
                 "Method", meth->name(), "ArgName", arg_name);
      {
        IndentScope raii_method_indent(out);
        out->Print("raise NotImplementedError()\n");
      }
    }
  }
  return true;
}

bool PrintServer(const ServiceDescriptor* service, Printer* out) {
  grpc::string doc = "<fill me in later!>";
  map<grpc::string, grpc::string> dict = ListToDict({
        "Service", service->name(),
        "Documentation", doc,
      });
  out->Print(dict, "class EarlyAdopter$Service$Server(object):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print(dict, "\"\"\"$Documentation$\"\"\"\n");
    out->Print("__metaclass__ = abc.ABCMeta\n");
    out->Print("@abc.abstractmethod\n");
    out->Print("def start(self):\n");
    {
      IndentScope raii_method_indent(out);
      out->Print("raise NotImplementedError()\n");
    }

    out->Print("@abc.abstractmethod\n");
    out->Print("def stop(self):\n");
    {
      IndentScope raii_method_indent(out);
      out->Print("raise NotImplementedError()\n");
    }
  }
  return true;
}

bool PrintStub(const ServiceDescriptor* service,
               Printer* out) {
  grpc::string doc = "<fill me in later!>";
  map<grpc::string, grpc::string> dict = ListToDict({
        "Service", service->name(),
        "Documentation", doc,
      });
  out->Print(dict, "class EarlyAdopter$Service$Stub(object):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print(dict, "\"\"\"$Documentation$\"\"\"\n");
    out->Print("__metaclass__ = abc.ABCMeta\n");
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* meth = service->method(i);
      grpc::string arg_name = meth->client_streaming() ?
          "request_iterator" : "request";
      auto methdict = ListToDict({"Method", meth->name(), "ArgName", arg_name});
      out->Print("@abc.abstractmethod\n");
      out->Print(methdict, "def $Method$(self, $ArgName$):\n");
      {
        IndentScope raii_method_indent(out);
        out->Print("raise NotImplementedError()\n");
      }
      out->Print(methdict, "$Method$.async = None\n");
    }
  }
  return true;
}

// TODO(protobuf team): Export `ModuleName` from protobuf's
// `src/google/protobuf/compiler/python/python_generator.cc` file.
grpc::string ModuleName(const grpc::string& filename) {
  grpc::string basename = StripProto(filename);
  basename = StringReplace(basename, "-", "_");
  basename = StringReplace(basename, "/", ".");
  return basename + "_pb2";
}

bool GetModuleAndMessagePath(const Descriptor* type,
                             pair<grpc::string, grpc::string>* out) {
  const Descriptor* path_elem_type = type;
  vector<const Descriptor*> message_path;
  do {
    message_path.push_back(path_elem_type);
    path_elem_type = path_elem_type->containing_type();
  } while (path_elem_type != nullptr);
  grpc::string file_name = type->file()->name();
  static const int proto_suffix_length = strlen(".proto");
  if (!(file_name.size() > static_cast<size_t>(proto_suffix_length) &&
        file_name.find_last_of(".proto") == file_name.size() - 1)) {
    return false;
  }
  grpc::string module = ModuleName(file_name);
  grpc::string message_type;
  for (auto path_iter = message_path.rbegin();
       path_iter != message_path.rend(); ++path_iter) {
    message_type += (*path_iter)->name() + ".";
  }
  // no pop_back prior to C++11
  message_type.resize(message_type.size() - 1);
  *out = make_pair(module, message_type);
  return true;
}

bool PrintServerFactory(const grpc::string& package_qualified_service_name,
                        const ServiceDescriptor* service, Printer* out) {
  out->Print("def early_adopter_create_$Service$_server(servicer, port, "
             "private_key=None, certificate_chain=None):\n",
             "Service", service->name());
  {
    IndentScope raii_create_server_indent(out);
    map<grpc::string, grpc::string> method_description_constructors;
    map<grpc::string, pair<grpc::string, grpc::string>>
        input_message_modules_and_classes;
    map<grpc::string, pair<grpc::string, grpc::string>>
        output_message_modules_and_classes;
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      const grpc::string method_description_constructor =
          grpc::string(method->client_streaming() ? "stream_" : "unary_") +
          grpc::string(method->server_streaming() ? "stream_" : "unary_") +
          "service_description";
      pair<grpc::string, grpc::string> input_message_module_and_class;
      if (!GetModuleAndMessagePath(method->input_type(),
                                   &input_message_module_and_class)) {
        return false;
      }
      pair<grpc::string, grpc::string> output_message_module_and_class;
      if (!GetModuleAndMessagePath(method->output_type(),
                                   &output_message_module_and_class)) {
        return false;
      }
      // Import the modules that define the messages used in RPCs.
      out->Print("import $Module$\n", "Module",
                 input_message_module_and_class.first);
      out->Print("import $Module$\n", "Module",
                 output_message_module_and_class.first);
      method_description_constructors.insert(
          make_pair(method->name(), method_description_constructor));
      input_message_modules_and_classes.insert(
          make_pair(method->name(), input_message_module_and_class));
      output_message_modules_and_classes.insert(
          make_pair(method->name(), output_message_module_and_class));
    }
    out->Print("method_service_descriptions = {\n");
    for (auto name_and_description_constructor =
	   method_description_constructors.begin();
	 name_and_description_constructor !=
	   method_description_constructors.end();
	 name_and_description_constructor++) {
      IndentScope raii_descriptions_indent(out);
      const grpc::string method_name = name_and_description_constructor->first;
      auto input_message_module_and_class =
          input_message_modules_and_classes.find(method_name);
      auto output_message_module_and_class =
          output_message_modules_and_classes.find(method_name);
      out->Print("\"$Method$\": utilities.$Constructor$(\n", "Method",
                 method_name, "Constructor",
                 name_and_description_constructor->second);
      {
        IndentScope raii_description_arguments_indent(out);
        out->Print("servicer.$Method$,\n", "Method", method_name);
        out->Print(
            "$InputTypeModule$.$InputTypeClass$.FromString,\n",
            "InputTypeModule", input_message_module_and_class->second.first,
            "InputTypeClass", input_message_module_and_class->second.second);
        out->Print(
            "$OutputTypeModule$.$OutputTypeClass$.SerializeToString,\n",
            "OutputTypeModule", output_message_module_and_class->second.first,
            "OutputTypeClass", output_message_module_and_class->second.second);
      }
      out->Print("),\n");
    }
    out->Print("}\n");
    out->Print(
        "return implementations.server("
        "\"$PackageQualifiedServiceName$\","
        " method_service_descriptions, port, private_key=private_key,"
        " certificate_chain=certificate_chain)\n",
        "PackageQualifiedServiceName", package_qualified_service_name);
  }
  return true;
}

bool PrintStubFactory(const grpc::string& package_qualified_service_name,
                      const ServiceDescriptor* service, Printer* out) {
  map<grpc::string, grpc::string> dict = ListToDict({
        "Service", service->name(),
      });
  out->Print(dict, "def early_adopter_create_$Service$_stub(host, port,"
             " metadata_transformer=None,"
             " secure=False, root_certificates=None, private_key=None,"
             " certificate_chain=None, server_host_override=None):\n");
  {
    IndentScope raii_create_server_indent(out);
    map<grpc::string, grpc::string> method_description_constructors;
    map<grpc::string, pair<grpc::string, grpc::string>>
        input_message_modules_and_classes;
    map<grpc::string, pair<grpc::string, grpc::string>>
        output_message_modules_and_classes;
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      const grpc::string method_description_constructor =
          grpc::string(method->client_streaming() ? "stream_" : "unary_") +
          grpc::string(method->server_streaming() ? "stream_" : "unary_") +
          "invocation_description";
      pair<grpc::string, grpc::string> input_message_module_and_class;
      if (!GetModuleAndMessagePath(method->input_type(),
                                   &input_message_module_and_class)) {
        return false;
      }
      pair<grpc::string, grpc::string> output_message_module_and_class;
      if (!GetModuleAndMessagePath(method->output_type(),
                                   &output_message_module_and_class)) {
        return false;
      }
      // Import the modules that define the messages used in RPCs.
      out->Print("import $Module$\n", "Module",
                 input_message_module_and_class.first);
      out->Print("import $Module$\n", "Module",
                 output_message_module_and_class.first);
      method_description_constructors.insert(
          make_pair(method->name(), method_description_constructor));
      input_message_modules_and_classes.insert(
          make_pair(method->name(), input_message_module_and_class));
      output_message_modules_and_classes.insert(
          make_pair(method->name(), output_message_module_and_class));
    }
    out->Print("method_invocation_descriptions = {\n");
    for (auto name_and_description_constructor =
	   method_description_constructors.begin();
	 name_and_description_constructor !=
	   method_description_constructors.end();
	 name_and_description_constructor++) {
      IndentScope raii_descriptions_indent(out);
      const grpc::string method_name = name_and_description_constructor->first;
      auto input_message_module_and_class =
          input_message_modules_and_classes.find(method_name);
      auto output_message_module_and_class =
          output_message_modules_and_classes.find(method_name);
      out->Print("\"$Method$\": utilities.$Constructor$(\n", "Method",
                 method_name, "Constructor",
                 name_and_description_constructor->second);
      {
        IndentScope raii_description_arguments_indent(out);
        out->Print(
            "$InputTypeModule$.$InputTypeClass$.SerializeToString,\n",
            "InputTypeModule", input_message_module_and_class->second.first,
            "InputTypeClass", input_message_module_and_class->second.second);
        out->Print(
            "$OutputTypeModule$.$OutputTypeClass$.FromString,\n",
            "OutputTypeModule", output_message_module_and_class->second.first,
            "OutputTypeClass", output_message_module_and_class->second.second);
      }
      out->Print("),\n");
    }
    out->Print("}\n");
    out->Print(
        "return implementations.stub("
        "\"$PackageQualifiedServiceName$\","
        " method_invocation_descriptions, host, port,"
        " metadata_transformer=metadata_transformer, secure=secure,"
        " root_certificates=root_certificates, private_key=private_key,"
        " certificate_chain=certificate_chain,"
        " server_host_override=server_host_override)\n",
        "PackageQualifiedServiceName", package_qualified_service_name);
  }
  return true;
}

bool PrintPreamble(const FileDescriptor* file,
                   const GeneratorConfiguration& config, Printer* out) {
  out->Print("import abc\n");
  out->Print("from $Package$ import implementations\n",
             "Package", config.implementations_package_root);
  out->Print("from grpc.framework.alpha import utilities\n");
  return true;
}

}  // namespace

pair<bool, grpc::string> GetServices(const FileDescriptor* file,
                                    const GeneratorConfiguration& config) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    StringOutputStream output_stream(&output);
    Printer out(&output_stream, '$');
    if (!PrintPreamble(file, config, &out)) {
      return make_pair(false, "");
    }
    auto package = file->package();
    if (!package.empty()) {
      package = package.append(".");
    }
    for (int i = 0; i < file->service_count(); ++i) {
      auto service = file->service(i);
      auto package_qualified_service_name = package + service->name();
      if (!(PrintServicer(service, &out) &&
            PrintServer(service, &out) &&
            PrintStub(service, &out) &&
            PrintServerFactory(package_qualified_service_name, service, &out) &&
            PrintStubFactory(package_qualified_service_name, service, &out))) {
        return make_pair(false, "");
      }
    }
  }
  return make_pair(true, std::move(output));
}

}  // namespace grpc_python_generator
