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

// TODO(https://github.com/google/protobuf/issues/888):
// Export `ModuleName` from protobuf's
// `src/google/protobuf/compiler/python/python_generator.cc` file.
grpc::string ModuleName(const grpc::string& filename) {
  grpc::string basename = StripProto(filename);
  basename = StringReplace(basename, "-", "_");
  basename = StringReplace(basename, "/", ".");
  return basename + "_pb2";
}

// TODO(https://github.com/google/protobuf/issues/888):
// Export `ModuleAlias` from protobuf's
// `src/google/protobuf/compiler/python/python_generator.cc` file.
grpc::string ModuleAlias(const grpc::string& filename) {
  grpc::string module_name = ModuleName(filename);
  // We can't have dots in the module name, so we replace each with _dot_.
  // But that could lead to a collision between a.b and a_dot_b, so we also
  // duplicate each underscore.
  module_name = StringReplace(module_name, "_", "__");
  module_name = StringReplace(module_name, ".", "_dot_");
  return module_name;
}


bool GetModuleAndMessagePath(const Descriptor* type,
                             const ServiceDescriptor* service,
                             grpc::string* out) {
  const Descriptor* path_elem_type = type;
  vector<const Descriptor*> message_path;
  do {
    message_path.push_back(path_elem_type);
    path_elem_type = path_elem_type->containing_type();
  } while (path_elem_type); // implicit nullptr comparison; don't be explicit
  grpc::string file_name = type->file()->name();
  static const int proto_suffix_length = strlen(".proto");
  if (!(file_name.size() > static_cast<size_t>(proto_suffix_length) &&
        file_name.find_last_of(".proto") == file_name.size() - 1)) {
    return false;
  }
  grpc::string service_file_name = service->file()->name();
  grpc::string module = service_file_name == file_name ?
          "" : ModuleAlias(file_name) + ".";
  grpc::string message_type;
  for (auto path_iter = message_path.rbegin();
       path_iter != message_path.rend(); ++path_iter) {
    message_type += (*path_iter)->name() + ".";
  }
  // no pop_back prior to C++11
  message_type.resize(message_type.size() - 1);
  *out = module + message_type;
  return true;
}

// Get all comments (leading, leading_detached, trailing) and print them as a
// docstring. Any leading space of a line will be removed, but the line wrapping
// will not be changed.
template <typename DescriptorType>
static void PrintAllComments(const DescriptorType* desc, Printer* printer) {
  std::vector<grpc::string> comments;
  grpc_generator::GetComment(desc, grpc_generator::COMMENTTYPE_LEADING_DETACHED,
                             &comments);
  grpc_generator::GetComment(desc, grpc_generator::COMMENTTYPE_LEADING,
                             &comments);
  grpc_generator::GetComment(desc, grpc_generator::COMMENTTYPE_TRAILING,
                             &comments);
  if (comments.empty()) {
    return;
  }
  printer->Print("\"\"\"");
  for (auto it = comments.begin(); it != comments.end(); ++it) {
    size_t start_pos = it->find_first_not_of(' ');
    if (start_pos != grpc::string::npos) {
      printer->Print(it->c_str() + start_pos);
    }
    printer->Print("\n");
  }
  printer->Print("\"\"\"\n");
}

bool PrintBetaServicer(const ServiceDescriptor* service,
                       Printer* out) {
  out->Print("\n\n");
  out->Print("class Beta$Service$Servicer(object):\n", "Service",
             service->name());
  {
    IndentScope raii_class_indent(out);
    PrintAllComments(service, out);
    for (int i = 0; i < service->method_count(); ++i) {
      auto meth = service->method(i);
      grpc::string arg_name = meth->client_streaming() ?
          "request_iterator" : "request";
      out->Print("def $Method$(self, $ArgName$, context):\n",
                 "Method", meth->name(), "ArgName", arg_name);
      {
        IndentScope raii_method_indent(out);
        PrintAllComments(meth, out);
        out->Print("context.code(beta_interfaces.StatusCode.UNIMPLEMENTED)\n");
      }
    }
  }
  return true;
}

bool PrintBetaStub(const ServiceDescriptor* service,
                   Printer* out) {
  out->Print("\n\n");
  out->Print("class Beta$Service$Stub(object):\n", "Service", service->name());
  {
    IndentScope raii_class_indent(out);
    PrintAllComments(service, out);
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* meth = service->method(i);
      grpc::string arg_name = meth->client_streaming() ?
          "request_iterator" : "request";
      auto methdict = ListToDict({"Method", meth->name(), "ArgName", arg_name});
      out->Print(methdict, "def $Method$(self, $ArgName$, timeout, metadata=None, with_call=False, protocol_options=None):\n");
      {
        IndentScope raii_method_indent(out);
        PrintAllComments(meth, out);
        out->Print("raise NotImplementedError()\n");
      }
      if (!meth->server_streaming()) {
        out->Print(methdict, "$Method$.future = None\n");
      }
    }
  }
  return true;
}

bool PrintBetaServerFactory(const grpc::string& package_qualified_service_name,
                            const ServiceDescriptor* service, Printer* out) {
  out->Print("\n\n");
  out->Print("def beta_create_$Service$_server(servicer, pool=None, "
             "pool_size=None, default_timeout=None, maximum_timeout=None):\n",
             "Service", service->name());
  {
    IndentScope raii_create_server_indent(out);
    map<grpc::string, grpc::string> method_implementation_constructors;
    map<grpc::string, grpc::string> input_message_modules_and_classes;
    map<grpc::string, grpc::string> output_message_modules_and_classes;
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      const grpc::string method_implementation_constructor =
          grpc::string(method->client_streaming() ? "stream_" : "unary_") +
          grpc::string(method->server_streaming() ? "stream_" : "unary_") +
          "inline";
      grpc::string input_message_module_and_class;
      if (!GetModuleAndMessagePath(method->input_type(), service,
                                   &input_message_module_and_class)) {
        return false;
      }
      grpc::string output_message_module_and_class;
      if (!GetModuleAndMessagePath(method->output_type(), service,
                                   &output_message_module_and_class)) {
        return false;
      }
      method_implementation_constructors.insert(
          make_pair(method->name(), method_implementation_constructor));
      input_message_modules_and_classes.insert(
          make_pair(method->name(), input_message_module_and_class));
      output_message_modules_and_classes.insert(
          make_pair(method->name(), output_message_module_and_class));
    }
    out->Print("request_deserializers = {\n");
    for (auto name_and_input_module_class_pair =
           input_message_modules_and_classes.begin();
         name_and_input_module_class_pair !=
           input_message_modules_and_classes.end();
         name_and_input_module_class_pair++) {
      IndentScope raii_indent(out);
      out->Print("(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
                 "$InputTypeModuleAndClass$.FromString,\n",
                 "PackageQualifiedServiceName", package_qualified_service_name,
                 "MethodName", name_and_input_module_class_pair->first,
                 "InputTypeModuleAndClass",
                 name_and_input_module_class_pair->second);
    }
    out->Print("}\n");
    out->Print("response_serializers = {\n");
    for (auto name_and_output_module_class_pair =
           output_message_modules_and_classes.begin();
         name_and_output_module_class_pair !=
           output_message_modules_and_classes.end();
         name_and_output_module_class_pair++) {
      IndentScope raii_indent(out);
      out->Print("(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
                 "$OutputTypeModuleAndClass$.SerializeToString,\n",
                 "PackageQualifiedServiceName", package_qualified_service_name,
                 "MethodName", name_and_output_module_class_pair->first,
                 "OutputTypeModuleAndClass",
                 name_and_output_module_class_pair->second);
    }
    out->Print("}\n");
    out->Print("method_implementations = {\n");
    for (auto name_and_implementation_constructor =
	   method_implementation_constructors.begin();
	 name_and_implementation_constructor !=
	   method_implementation_constructors.end();
	 name_and_implementation_constructor++) {
      IndentScope raii_descriptions_indent(out);
      const grpc::string method_name =
          name_and_implementation_constructor->first;
      out->Print("(\'$PackageQualifiedServiceName$\', \'$Method$\'): "
                 "face_utilities.$Constructor$(servicer.$Method$),\n",
                 "PackageQualifiedServiceName", package_qualified_service_name,
                 "Method", name_and_implementation_constructor->first,
                 "Constructor", name_and_implementation_constructor->second);
    }
    out->Print("}\n");
    out->Print("server_options = beta_implementations.server_options("
               "request_deserializers=request_deserializers, "
               "response_serializers=response_serializers, "
               "thread_pool=pool, thread_pool_size=pool_size, "
               "default_timeout=default_timeout, "
               "maximum_timeout=maximum_timeout)\n");
    out->Print("return beta_implementations.server(method_implementations, "
               "options=server_options)\n");
  }
  return true;
}

bool PrintBetaStubFactory(const grpc::string& package_qualified_service_name,
                          const ServiceDescriptor* service, Printer* out) {
  map<grpc::string, grpc::string> dict = ListToDict({
        "Service", service->name(),
      });
  out->Print("\n\n");
  out->Print(dict, "def beta_create_$Service$_stub(channel, host=None,"
             " metadata_transformer=None, pool=None, pool_size=None):\n");
  {
    IndentScope raii_create_server_indent(out);
    map<grpc::string, grpc::string> method_cardinalities;
    map<grpc::string, grpc::string> input_message_modules_and_classes;
    map<grpc::string, grpc::string> output_message_modules_and_classes;
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      const grpc::string method_cardinality =
          grpc::string(method->client_streaming() ? "STREAM" : "UNARY") +
          "_" +
          grpc::string(method->server_streaming() ? "STREAM" : "UNARY");
      grpc::string input_message_module_and_class;
      if (!GetModuleAndMessagePath(method->input_type(), service,
                                   &input_message_module_and_class)) {
        return false;
      }
      grpc::string output_message_module_and_class;
      if (!GetModuleAndMessagePath(method->output_type(), service,
                                   &output_message_module_and_class)) {
        return false;
      }
      method_cardinalities.insert(
          make_pair(method->name(), method_cardinality));
      input_message_modules_and_classes.insert(
          make_pair(method->name(), input_message_module_and_class));
      output_message_modules_and_classes.insert(
          make_pair(method->name(), output_message_module_and_class));
    }
    out->Print("request_serializers = {\n");
    for (auto name_and_input_module_class_pair =
           input_message_modules_and_classes.begin();
         name_and_input_module_class_pair !=
           input_message_modules_and_classes.end();
         name_and_input_module_class_pair++) {
      IndentScope raii_indent(out);
      out->Print("(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
                 "$InputTypeModuleAndClass$.SerializeToString,\n",
                 "PackageQualifiedServiceName", package_qualified_service_name,
                 "MethodName", name_and_input_module_class_pair->first,
                 "InputTypeModuleAndClass",
                 name_and_input_module_class_pair->second);
    }
    out->Print("}\n");
    out->Print("response_deserializers = {\n");
    for (auto name_and_output_module_class_pair =
           output_message_modules_and_classes.begin();
         name_and_output_module_class_pair !=
           output_message_modules_and_classes.end();
         name_and_output_module_class_pair++) {
      IndentScope raii_indent(out);
      out->Print("(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
                 "$OutputTypeModuleAndClass$.FromString,\n",
                 "PackageQualifiedServiceName", package_qualified_service_name,
                 "MethodName", name_and_output_module_class_pair->first,
                 "OutputTypeModuleAndClass",
                 name_and_output_module_class_pair->second);
    }
    out->Print("}\n");
    out->Print("cardinalities = {\n");
    for (auto name_and_cardinality = method_cardinalities.begin();
         name_and_cardinality != method_cardinalities.end();
         name_and_cardinality++) {
      IndentScope raii_descriptions_indent(out);
      out->Print("\'$Method$\': cardinality.Cardinality.$Cardinality$,\n",
                 "Method", name_and_cardinality->first,
                 "Cardinality", name_and_cardinality->second);
    }
    out->Print("}\n");
    out->Print("stub_options = beta_implementations.stub_options("
               "host=host, metadata_transformer=metadata_transformer, "
               "request_serializers=request_serializers, "
               "response_deserializers=response_deserializers, "
               "thread_pool=pool, thread_pool_size=pool_size)\n");
    out->Print(
        "return beta_implementations.dynamic_stub(channel, \'$PackageQualifiedServiceName$\', "
        "cardinalities, options=stub_options)\n",
        "PackageQualifiedServiceName", package_qualified_service_name);
  }
  return true;
}

bool PrintPreamble(const FileDescriptor* file,
                   const GeneratorConfiguration& config, Printer* out) {
  out->Print("from $Package$ import implementations as beta_implementations\n",
             "Package", config.beta_package_root);
  out->Print("from $Package$ import interfaces as beta_interfaces\n",
             "Package", config.beta_package_root);
  out->Print("from grpc.framework.common import cardinality\n");
  out->Print("from grpc.framework.interfaces.face import utilities as face_utilities\n");
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
      if (!(PrintBetaServicer(service, &out) &&
            PrintBetaStub(service, &out) &&
            PrintBetaServerFactory(package_qualified_service_name, service, &out) &&
            PrintBetaStubFactory(package_qualified_service_name, service, &out))) {
        return make_pair(false, "");
      }
    }
  }
  return make_pair(true, std::move(output));
}

}  // namespace grpc_python_generator
