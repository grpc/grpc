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
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <ostream>
#include <set>
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
using std::tuple;
using std::vector;
using std::set;

namespace grpc_python_generator {

namespace {

typedef vector<const Descriptor*> DescriptorVector;
typedef map<grpc::string, grpc::string> StringMap;
typedef vector<grpc::string> StringVector;
typedef tuple<grpc::string, grpc::string> StringPair;
typedef set<StringPair> StringPairSet;

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

  ~IndentScope() { printer_->Outdent(); }

 private:
  Printer* printer_;
};

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

// Tucks all generator state in an anonymous namespace away from
// PythonGrpcGenerator and the header file, mostly to encourage future changes
// to not require updates to the grpcio-tools C++ code part. Assumes that it is
// only ever used from a single thread.
struct PrivateGenerator {
  const GeneratorConfiguration& config;
  const FileDescriptor* file;

  bool generate_in_pb2_grpc;

  Printer* out;

  PrivateGenerator(const GeneratorConfiguration& config,
                   const FileDescriptor* file);

  std::pair<bool, grpc::string> GetGrpcServices();

 private:
  bool PrintPreamble();
  bool PrintBetaPreamble();
  bool PrintGAServices();
  bool PrintBetaServices();

  bool PrintAddServicerToServer(
      const grpc::string& package_qualified_service_name,
      const ServiceDescriptor* service);
  bool PrintServicer(const ServiceDescriptor* service);
  bool PrintStub(const grpc::string& package_qualified_service_name,
                 const ServiceDescriptor* service);

  bool PrintBetaServicer(const ServiceDescriptor* service);
  bool PrintBetaServerFactory(
      const grpc::string& package_qualified_service_name,
      const ServiceDescriptor* service);
  bool PrintBetaStub(const ServiceDescriptor* service);
  bool PrintBetaStubFactory(const grpc::string& package_qualified_service_name,
                            const ServiceDescriptor* service);

  // Get all comments (leading, leading_detached, trailing) and print them as a
  // docstring. Any leading space of a line will be removed, but the line
  // wrapping will not be changed.
  template <typename DescriptorType>
  void PrintAllComments(const DescriptorType* descriptor);

  bool GetModuleAndMessagePath(const Descriptor* type, grpc::string* out);
};

PrivateGenerator::PrivateGenerator(const GeneratorConfiguration& config,
                                   const FileDescriptor* file)
    : config(config), file(file) {}

bool PrivateGenerator::GetModuleAndMessagePath(const Descriptor* type,
                                               grpc::string* out) {
  const Descriptor* path_elem_type = type;
  DescriptorVector message_path;
  do {
    message_path.push_back(path_elem_type);
    path_elem_type = path_elem_type->containing_type();
  } while (path_elem_type);  // implicit nullptr comparison; don't be explicit
  grpc::string file_name = type->file()->name();
  static const int proto_suffix_length = strlen(".proto");
  if (!(file_name.size() > static_cast<size_t>(proto_suffix_length) &&
        file_name.find_last_of(".proto") == file_name.size() - 1)) {
    return false;
  }
  grpc::string generator_file_name = file->name();
  grpc::string module;
  if (generator_file_name != file_name || generate_in_pb2_grpc) {
    module = ModuleAlias(file_name) + ".";
  } else {
    module = "";
  }
  grpc::string message_type;
  for (DescriptorVector::reverse_iterator path_iter = message_path.rbegin();
       path_iter != message_path.rend(); ++path_iter) {
    message_type += (*path_iter)->name() + ".";
  }
  // no pop_back prior to C++11
  message_type.resize(message_type.size() - 1);
  *out = module + message_type;
  return true;
}

template <typename DescriptorType>
void PrivateGenerator::PrintAllComments(const DescriptorType* descriptor) {
  StringVector comments;
  grpc_generator::GetComment(
      descriptor, grpc_generator::COMMENTTYPE_LEADING_DETACHED, &comments);
  grpc_generator::GetComment(descriptor, grpc_generator::COMMENTTYPE_LEADING,
                             &comments);
  grpc_generator::GetComment(descriptor, grpc_generator::COMMENTTYPE_TRAILING,
                             &comments);
  if (comments.empty()) {
    return;
  }
  out->Print("\"\"\"");
  for (StringVector::iterator it = comments.begin(); it != comments.end();
       ++it) {
    size_t start_pos = it->find_first_not_of(' ');
    if (start_pos != grpc::string::npos) {
      out->Print(it->c_str() + start_pos);
    }
    out->Print("\n");
  }
  out->Print("\"\"\"\n");
}

bool PrivateGenerator::PrintBetaServicer(const ServiceDescriptor* service) {
  out->Print("\n\n");
  out->Print("class Beta$Service$Servicer(object):\n", "Service",
             service->name());
  {
    IndentScope raii_class_indent(out);
    out->Print(
        "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
        "\nIt is recommended to use the GA API (classes and functions in this\n"
        "file not marked beta) for all further purposes. This class was "
        "generated\n"
        "only to ease transition from grpcio<0.15.0 to "
        "grpcio>=0.15.0.\"\"\"\n");
    PrintAllComments(service);
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      grpc::string arg_name =
          method->client_streaming() ? "request_iterator" : "request";
      out->Print("def $Method$(self, $ArgName$, context):\n", "Method",
                 method->name(), "ArgName", arg_name);
      {
        IndentScope raii_method_indent(out);
        PrintAllComments(method);
        out->Print("context.code(beta_interfaces.StatusCode.UNIMPLEMENTED)\n");
      }
    }
  }
  return true;
}

bool PrivateGenerator::PrintBetaStub(const ServiceDescriptor* service) {
  out->Print("\n\n");
  out->Print("class Beta$Service$Stub(object):\n", "Service", service->name());
  {
    IndentScope raii_class_indent(out);
    out->Print(
        "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
        "\nIt is recommended to use the GA API (classes and functions in this\n"
        "file not marked beta) for all further purposes. This class was "
        "generated\n"
        "only to ease transition from grpcio<0.15.0 to "
        "grpcio>=0.15.0.\"\"\"\n");
    PrintAllComments(service);
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      grpc::string arg_name =
          method->client_streaming() ? "request_iterator" : "request";
      StringMap method_dict;
      method_dict["Method"] = method->name();
      method_dict["ArgName"] = arg_name;
      out->Print(method_dict,
                 "def $Method$(self, $ArgName$, timeout, metadata=None, "
                 "with_call=False, protocol_options=None):\n");
      {
        IndentScope raii_method_indent(out);
        PrintAllComments(method);
        out->Print("raise NotImplementedError()\n");
      }
      if (!method->server_streaming()) {
        out->Print(method_dict, "$Method$.future = None\n");
      }
    }
  }
  return true;
}

bool PrivateGenerator::PrintBetaServerFactory(
    const grpc::string& package_qualified_service_name,
    const ServiceDescriptor* service) {
  out->Print("\n\n");
  out->Print(
      "def beta_create_$Service$_server(servicer, pool=None, "
      "pool_size=None, default_timeout=None, maximum_timeout=None):\n",
      "Service", service->name());
  {
    IndentScope raii_create_server_indent(out);
    out->Print(
        "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
        "\nIt is recommended to use the GA API (classes and functions in this\n"
        "file not marked beta) for all further purposes. This function was\n"
        "generated only to ease transition from grpcio<0.15.0 to grpcio>=0.15.0"
        "\"\"\"\n");
    StringMap method_implementation_constructors;
    StringMap input_message_modules_and_classes;
    StringMap output_message_modules_and_classes;
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      const grpc::string method_implementation_constructor =
          grpc::string(method->client_streaming() ? "stream_" : "unary_") +
          grpc::string(method->server_streaming() ? "stream_" : "unary_") +
          "inline";
      grpc::string input_message_module_and_class;
      if (!GetModuleAndMessagePath(method->input_type(),
                                   &input_message_module_and_class)) {
        return false;
      }
      grpc::string output_message_module_and_class;
      if (!GetModuleAndMessagePath(method->output_type(),
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
    for (StringMap::iterator name_and_input_module_class_pair =
             input_message_modules_and_classes.begin();
         name_and_input_module_class_pair !=
         input_message_modules_and_classes.end();
         name_and_input_module_class_pair++) {
      IndentScope raii_indent(out);
      out->Print(
          "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
          "$InputTypeModuleAndClass$.FromString,\n",
          "PackageQualifiedServiceName", package_qualified_service_name,
          "MethodName", name_and_input_module_class_pair->first,
          "InputTypeModuleAndClass", name_and_input_module_class_pair->second);
    }
    out->Print("}\n");
    out->Print("response_serializers = {\n");
    for (StringMap::iterator name_and_output_module_class_pair =
             output_message_modules_and_classes.begin();
         name_and_output_module_class_pair !=
         output_message_modules_and_classes.end();
         name_and_output_module_class_pair++) {
      IndentScope raii_indent(out);
      out->Print(
          "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
          "$OutputTypeModuleAndClass$.SerializeToString,\n",
          "PackageQualifiedServiceName", package_qualified_service_name,
          "MethodName", name_and_output_module_class_pair->first,
          "OutputTypeModuleAndClass",
          name_and_output_module_class_pair->second);
    }
    out->Print("}\n");
    out->Print("method_implementations = {\n");
    for (StringMap::iterator name_and_implementation_constructor =
             method_implementation_constructors.begin();
         name_and_implementation_constructor !=
         method_implementation_constructors.end();
         name_and_implementation_constructor++) {
      IndentScope raii_descriptions_indent(out);
      const grpc::string method_name =
          name_and_implementation_constructor->first;
      out->Print(
          "(\'$PackageQualifiedServiceName$\', \'$Method$\'): "
          "face_utilities.$Constructor$(servicer.$Method$),\n",
          "PackageQualifiedServiceName", package_qualified_service_name,
          "Method", name_and_implementation_constructor->first, "Constructor",
          name_and_implementation_constructor->second);
    }
    out->Print("}\n");
    out->Print(
        "server_options = beta_implementations.server_options("
        "request_deserializers=request_deserializers, "
        "response_serializers=response_serializers, "
        "thread_pool=pool, thread_pool_size=pool_size, "
        "default_timeout=default_timeout, "
        "maximum_timeout=maximum_timeout)\n");
    out->Print(
        "return beta_implementations.server(method_implementations, "
        "options=server_options)\n");
  }
  return true;
}

bool PrivateGenerator::PrintBetaStubFactory(
    const grpc::string& package_qualified_service_name,
    const ServiceDescriptor* service) {
  StringMap dict;
  dict["Service"] = service->name();
  out->Print("\n\n");
  out->Print(dict,
             "def beta_create_$Service$_stub(channel, host=None,"
             " metadata_transformer=None, pool=None, pool_size=None):\n");
  {
    IndentScope raii_create_server_indent(out);
    out->Print(
        "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
        "\nIt is recommended to use the GA API (classes and functions in this\n"
        "file not marked beta) for all further purposes. This function was\n"
        "generated only to ease transition from grpcio<0.15.0 to grpcio>=0.15.0"
        "\"\"\"\n");
    StringMap method_cardinalities;
    StringMap input_message_modules_and_classes;
    StringMap output_message_modules_and_classes;
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      const grpc::string method_cardinality =
          grpc::string(method->client_streaming() ? "STREAM" : "UNARY") + "_" +
          grpc::string(method->server_streaming() ? "STREAM" : "UNARY");
      grpc::string input_message_module_and_class;
      if (!GetModuleAndMessagePath(method->input_type(),
                                   &input_message_module_and_class)) {
        return false;
      }
      grpc::string output_message_module_and_class;
      if (!GetModuleAndMessagePath(method->output_type(),
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
    for (StringMap::iterator name_and_input_module_class_pair =
             input_message_modules_and_classes.begin();
         name_and_input_module_class_pair !=
         input_message_modules_and_classes.end();
         name_and_input_module_class_pair++) {
      IndentScope raii_indent(out);
      out->Print(
          "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
          "$InputTypeModuleAndClass$.SerializeToString,\n",
          "PackageQualifiedServiceName", package_qualified_service_name,
          "MethodName", name_and_input_module_class_pair->first,
          "InputTypeModuleAndClass", name_and_input_module_class_pair->second);
    }
    out->Print("}\n");
    out->Print("response_deserializers = {\n");
    for (StringMap::iterator name_and_output_module_class_pair =
             output_message_modules_and_classes.begin();
         name_and_output_module_class_pair !=
         output_message_modules_and_classes.end();
         name_and_output_module_class_pair++) {
      IndentScope raii_indent(out);
      out->Print(
          "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
          "$OutputTypeModuleAndClass$.FromString,\n",
          "PackageQualifiedServiceName", package_qualified_service_name,
          "MethodName", name_and_output_module_class_pair->first,
          "OutputTypeModuleAndClass",
          name_and_output_module_class_pair->second);
    }
    out->Print("}\n");
    out->Print("cardinalities = {\n");
    for (StringMap::iterator name_and_cardinality =
             method_cardinalities.begin();
         name_and_cardinality != method_cardinalities.end();
         name_and_cardinality++) {
      IndentScope raii_descriptions_indent(out);
      out->Print("\'$Method$\': cardinality.Cardinality.$Cardinality$,\n",
                 "Method", name_and_cardinality->first, "Cardinality",
                 name_and_cardinality->second);
    }
    out->Print("}\n");
    out->Print(
        "stub_options = beta_implementations.stub_options("
        "host=host, metadata_transformer=metadata_transformer, "
        "request_serializers=request_serializers, "
        "response_deserializers=response_deserializers, "
        "thread_pool=pool, thread_pool_size=pool_size)\n");
    out->Print(
        "return beta_implementations.dynamic_stub(channel, "
        "\'$PackageQualifiedServiceName$\', "
        "cardinalities, options=stub_options)\n",
        "PackageQualifiedServiceName", package_qualified_service_name);
  }
  return true;
}

bool PrivateGenerator::PrintStub(
    const grpc::string& package_qualified_service_name,
    const ServiceDescriptor* service) {
  out->Print("\n\n");
  out->Print("class $Service$Stub(object):\n", "Service", service->name());
  {
    IndentScope raii_class_indent(out);
    PrintAllComments(service);
    out->Print("\n");
    out->Print("def __init__(self, channel):\n");
    {
      IndentScope raii_init_indent(out);
      out->Print("\"\"\"Constructor.\n");
      out->Print("\n");
      out->Print("Args:\n");
      {
        IndentScope raii_args_indent(out);
        out->Print("channel: A grpc.Channel.\n");
      }
      out->Print("\"\"\"\n");
      for (int i = 0; i < service->method_count(); ++i) {
        const MethodDescriptor* method = service->method(i);
        grpc::string multi_callable_constructor =
            grpc::string(method->client_streaming() ? "stream" : "unary") +
            "_" + grpc::string(method->server_streaming() ? "stream" : "unary");
        grpc::string request_module_and_class;
        if (!GetModuleAndMessagePath(method->input_type(),
                                     &request_module_and_class)) {
          return false;
        }
        grpc::string response_module_and_class;
        if (!GetModuleAndMessagePath(method->output_type(),
                                     &response_module_and_class)) {
          return false;
        }
        out->Print("self.$Method$ = channel.$MultiCallableConstructor$(\n",
                   "Method", method->name(), "MultiCallableConstructor",
                   multi_callable_constructor);
        {
          IndentScope raii_first_attribute_indent(out);
          IndentScope raii_second_attribute_indent(out);
          out->Print("'/$PackageQualifiedService$/$Method$',\n",
                     "PackageQualifiedService", package_qualified_service_name,
                     "Method", method->name());
          out->Print(
              "request_serializer=$RequestModuleAndClass$.SerializeToString,\n",
              "RequestModuleAndClass", request_module_and_class);
          out->Print(
              "response_deserializer=$ResponseModuleAndClass$.FromString,\n",
              "ResponseModuleAndClass", response_module_and_class);
          out->Print(")\n");
        }
      }
    }
  }
  return true;
}

bool PrivateGenerator::PrintServicer(const ServiceDescriptor* service) {
  out->Print("\n\n");
  out->Print("class $Service$Servicer(object):\n", "Service", service->name());
  {
    IndentScope raii_class_indent(out);
    PrintAllComments(service);
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      grpc::string arg_name =
          method->client_streaming() ? "request_iterator" : "request";
      out->Print("\n");
      out->Print("def $Method$(self, $ArgName$, context):\n", "Method",
                 method->name(), "ArgName", arg_name);
      {
        IndentScope raii_method_indent(out);
        PrintAllComments(method);
        out->Print("context.set_code(grpc.StatusCode.UNIMPLEMENTED)\n");
        out->Print("context.set_details('Method not implemented!')\n");
        out->Print("raise NotImplementedError('Method not implemented!')\n");
      }
    }
  }
  return true;
}

bool PrivateGenerator::PrintAddServicerToServer(
    const grpc::string& package_qualified_service_name,
    const ServiceDescriptor* service) {
  out->Print("\n\n");
  out->Print("def add_$Service$Servicer_to_server(servicer, server):\n",
             "Service", service->name());
  {
    IndentScope raii_class_indent(out);
    out->Print("rpc_method_handlers = {\n");
    {
      IndentScope raii_dict_first_indent(out);
      IndentScope raii_dict_second_indent(out);
      for (int i = 0; i < service->method_count(); ++i) {
        const MethodDescriptor* method = service->method(i);
        grpc::string method_handler_constructor =
            grpc::string(method->client_streaming() ? "stream" : "unary") +
            "_" +
            grpc::string(method->server_streaming() ? "stream" : "unary") +
            "_rpc_method_handler";
        grpc::string request_module_and_class;
        if (!GetModuleAndMessagePath(method->input_type(),
                                     &request_module_and_class)) {
          return false;
        }
        grpc::string response_module_and_class;
        if (!GetModuleAndMessagePath(method->output_type(),
                                     &response_module_and_class)) {
          return false;
        }
        out->Print("'$Method$': grpc.$MethodHandlerConstructor$(\n", "Method",
                   method->name(), "MethodHandlerConstructor",
                   method_handler_constructor);
        {
          IndentScope raii_call_first_indent(out);
          IndentScope raii_call_second_indent(out);
          out->Print("servicer.$Method$,\n", "Method", method->name());
          out->Print(
              "request_deserializer=$RequestModuleAndClass$.FromString,\n",
              "RequestModuleAndClass", request_module_and_class);
          out->Print(
              "response_serializer=$ResponseModuleAndClass$.SerializeToString,"
              "\n",
              "ResponseModuleAndClass", response_module_and_class);
        }
        out->Print("),\n");
      }
    }
    out->Print("}\n");
    out->Print("generic_handler = grpc.method_handlers_generic_handler(\n");
    {
      IndentScope raii_call_first_indent(out);
      IndentScope raii_call_second_indent(out);
      out->Print("'$PackageQualifiedServiceName$', rpc_method_handlers)\n",
                 "PackageQualifiedServiceName", package_qualified_service_name);
    }
    out->Print("server.add_generic_rpc_handlers((generic_handler,))\n");
  }
  return true;
}

bool PrivateGenerator::PrintBetaPreamble() {
  out->Print("from $Package$ import implementations as beta_implementations\n",
             "Package", config.beta_package_root);
  out->Print("from $Package$ import interfaces as beta_interfaces\n", "Package",
             config.beta_package_root);
  return true;
}

bool PrivateGenerator::PrintPreamble() {
  out->Print("import $Package$\n", "Package", config.grpc_package_root);
  out->Print("from grpc.framework.common import cardinality\n");
  out->Print(
      "from grpc.framework.interfaces.face import utilities as "
      "face_utilities\n");
  if (generate_in_pb2_grpc) {
    out->Print("\n");
    StringPairSet imports_set;
    for (int i = 0; i < file->service_count(); ++i) {
      const ServiceDescriptor* service = file->service(i);
      for (int j = 0; j < service->method_count(); ++j) {
        const MethodDescriptor* method = service->method(j);
        const Descriptor* types[2] = {method->input_type(),
                                      method->output_type()};
        for (int k = 0; k < 2; ++k) {
          const Descriptor* type = types[k];
          grpc::string type_file_name = type->file()->name();
          grpc::string module_name = ModuleName(type_file_name);
          grpc::string module_alias = ModuleAlias(type_file_name);
          imports_set.insert(std::make_tuple(module_name, module_alias));
        }
      }
    }
    for (StringPairSet::iterator it = imports_set.begin();
         it != imports_set.end(); ++it) {
      out->Print("import $ModuleName$ as $ModuleAlias$\n", "ModuleName",
                 std::get<0>(*it), "ModuleAlias", std::get<1>(*it));
    }
  }
  return true;
}

bool PrivateGenerator::PrintGAServices() {
  grpc::string package = file->package();
  if (!package.empty()) {
    package = package.append(".");
  }
  for (int i = 0; i < file->service_count(); ++i) {
    const ServiceDescriptor* service = file->service(i);
    grpc::string package_qualified_service_name = package + service->name();
    if (!(PrintStub(package_qualified_service_name, service) &&
          PrintServicer(service) &&
          PrintAddServicerToServer(package_qualified_service_name, service))) {
      return false;
    }
  }
  return true;
}

bool PrivateGenerator::PrintBetaServices() {
  grpc::string package = file->package();
  if (!package.empty()) {
    package = package.append(".");
  }
  for (int i = 0; i < file->service_count(); ++i) {
    const ServiceDescriptor* service = file->service(i);
    grpc::string package_qualified_service_name = package + service->name();
    if (!(PrintBetaServicer(service) && PrintBetaStub(service) &&
          PrintBetaServerFactory(package_qualified_service_name, service) &&
          PrintBetaStubFactory(package_qualified_service_name, service))) {
      return false;
    }
  }
  return true;
}

pair<bool, grpc::string> PrivateGenerator::GetGrpcServices() {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    StringOutputStream output_stream(&output);
    Printer out_printer(&output_stream, '$');
    out = &out_printer;

    if (generate_in_pb2_grpc) {
      out->Print(
          "# Generated by the gRPC Python protocol compiler plugin. "
          "DO NOT EDIT!\n");
      if (!PrintPreamble()) {
        return make_pair(false, "");
      }
      if (!PrintGAServices()) {
        return make_pair(false, "");
      }
    } else {
      out->Print("try:\n");
      {
        IndentScope raii_dict_try_indent(out);
        out->Print(
            "# THESE ELEMENTS WILL BE DEPRECATED.\n"
            "# Please use the generated *_pb2_grpc.py files instead.\n");
        if (!PrintPreamble()) {
          return make_pair(false, "");
        }
        if (!PrintBetaPreamble()) {
          return make_pair(false, "");
        }
        if (!PrintGAServices()) {
          return make_pair(false, "");
        }
        if (!PrintBetaServices()) {
          return make_pair(false, "");
        }
      }
      out->Print("except ImportError:\n");
      {
        IndentScope raii_dict_except_indent(out);
        out->Print("pass");
      }
    }
  }
  return make_pair(true, std::move(output));
}

}  // namespace

GeneratorConfiguration::GeneratorConfiguration()
    : grpc_package_root("grpc"), beta_package_root("grpc.beta") {}

PythonGrpcGenerator::PythonGrpcGenerator(const GeneratorConfiguration& config)
    : config_(config) {}

PythonGrpcGenerator::~PythonGrpcGenerator() {}

static bool GenerateGrpc(GeneratorContext* context, PrivateGenerator& generator,
                         grpc::string file_name, bool generate_in_pb2_grpc) {
  bool success;
  std::unique_ptr<ZeroCopyOutputStream> output;
  std::unique_ptr<CodedOutputStream> coded_output;
  grpc::string grpc_code;

  if (generate_in_pb2_grpc) {
    output.reset(context->Open(file_name));
    generator.generate_in_pb2_grpc = true;
  } else {
    output.reset(context->OpenForInsert(file_name, "module_scope"));
    generator.generate_in_pb2_grpc = false;
  }

  coded_output.reset(new CodedOutputStream(output.get()));
  tie(success, grpc_code) = generator.GetGrpcServices();

  if (success) {
    coded_output->WriteRaw(grpc_code.data(), grpc_code.size());
    return true;
  } else {
    return false;
  }
}

bool PythonGrpcGenerator::Generate(const FileDescriptor* file,
                                   const grpc::string& parameter,
                                   GeneratorContext* context,
                                   grpc::string* error) const {
  // Get output file name.
  grpc::string pb2_file_name;
  grpc::string pb2_grpc_file_name;
  static const int proto_suffix_length = strlen(".proto");
  if (file->name().size() > static_cast<size_t>(proto_suffix_length) &&
      file->name().find_last_of(".proto") == file->name().size() - 1) {
    grpc::string base =
        file->name().substr(0, file->name().size() - proto_suffix_length);
    pb2_file_name = base + "_pb2.py";
    pb2_grpc_file_name = base + "_pb2_grpc.py";
  } else {
    *error = "Invalid proto file name. Proto file must end with .proto";
    return false;
  }

  PrivateGenerator generator(config_, file);
  if (parameter == "grpc_2_0") {
    return GenerateGrpc(context, generator, pb2_grpc_file_name, true);
  } else if (parameter == "") {
    return GenerateGrpc(context, generator, pb2_grpc_file_name, true) &&
           GenerateGrpc(context, generator, pb2_file_name, false);
  } else {
    *error = "Invalid parameter '" + parameter + "'.";
    return false;
  }
}

}  // namespace grpc_python_generator
