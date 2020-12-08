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
#include "src/compiler/protobuf_plugin.h"
#include "src/compiler/python_generator.h"
#include "src/compiler/python_generator_helpers.h"
#include "src/compiler/python_private_generator.h"

using grpc::protobuf::FileDescriptor;
using grpc::protobuf::compiler::GeneratorContext;
using grpc::protobuf::io::CodedOutputStream;
using grpc::protobuf::io::ZeroCopyOutputStream;
using std::make_pair;
using std::map;
using std::pair;
using std::replace;
using std::set;
using std::tuple;
using std::vector;

namespace grpc_python_generator {

std::string generator_file_name;

namespace {

typedef map<std::string, std::string> StringMap;
typedef vector<std::string> StringVector;
typedef tuple<std::string, std::string> StringPair;
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
  explicit IndentScope(grpc_generator::Printer* printer) : printer_(printer) {
    // NOTE(rbellevi): Two-space tabs are hard-coded in the protocol compiler.
    // Doubling our indents and outdents guarantees compliance with PEP8.
    printer_->Indent();
    printer_->Indent();
  }

  ~IndentScope() {
    printer_->Outdent();
    printer_->Outdent();
  }

 private:
  grpc_generator::Printer* printer_;
};

PrivateGenerator::PrivateGenerator(const GeneratorConfiguration& config,
                                   const grpc_generator::File* file)
    : config(config), file(file) {}

void PrivateGenerator::PrintAllComments(StringVector comments,
                                        grpc_generator::Printer* out) {
  if (comments.empty()) {
    // Python requires code structures like class and def to have
    // a body, even if it is just "pass" or a docstring.  We need
    // to ensure not to generate empty bodies. We could do something
    // smarter and more sophisticated, but at the moment, if there is
    // no docstring to print, we simply emit "pass" to ensure validity
    // of the generated code.
    out->Print(
        "\"\"\"Missing associated documentation comment in .proto "
        "file.\"\"\"\n");
    return;
  }
  out->Print("\"\"\"");
  for (StringVector::iterator it = comments.begin(); it != comments.end();
       ++it) {
    size_t start_pos = it->find_first_not_of(' ');
    if (start_pos != std::string::npos) {
      out->PrintRaw(it->c_str() + start_pos);
    }
    out->Print("\n");
  }
  out->Print("\"\"\"\n");
}

bool PrivateGenerator::PrintBetaServicer(const grpc_generator::Service* service,
                                         grpc_generator::Printer* out) {
  StringMap service_dict;
  service_dict["Service"] = service->name();
  out->Print("\n\n");
  out->Print(service_dict, "class Beta$Service$Servicer(object):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print(
        "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
        "\nIt is recommended to use the GA API (classes and functions in this\n"
        "file not marked beta) for all further purposes. This class was "
        "generated\n"
        "only to ease transition from grpcio<0.15.0 to "
        "grpcio>=0.15.0.\"\"\"\n");
    StringVector service_comments = service->GetAllComments();
    PrintAllComments(service_comments, out);
    for (int i = 0; i < service->method_count(); ++i) {
      auto method = service->method(i);
      std::string arg_name =
          method->ClientStreaming() ? "request_iterator" : "request";
      StringMap method_dict;
      method_dict["Method"] = method->name();
      method_dict["ArgName"] = arg_name;
      out->Print(method_dict, "def $Method$(self, $ArgName$, context):\n");
      {
        IndentScope raii_method_indent(out);
        StringVector method_comments = method->GetAllComments();
        PrintAllComments(method_comments, out);
        out->Print("context.code(beta_interfaces.StatusCode.UNIMPLEMENTED)\n");
      }
    }
  }
  return true;
}

bool PrivateGenerator::PrintBetaStub(const grpc_generator::Service* service,
                                     grpc_generator::Printer* out) {
  StringMap service_dict;
  service_dict["Service"] = service->name();
  out->Print("\n\n");
  out->Print(service_dict, "class Beta$Service$Stub(object):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print(
        "\"\"\"The Beta API is deprecated for 0.15.0 and later.\n"
        "\nIt is recommended to use the GA API (classes and functions in this\n"
        "file not marked beta) for all further purposes. This class was "
        "generated\n"
        "only to ease transition from grpcio<0.15.0 to "
        "grpcio>=0.15.0.\"\"\"\n");
    StringVector service_comments = service->GetAllComments();
    PrintAllComments(service_comments, out);
    for (int i = 0; i < service->method_count(); ++i) {
      auto method = service->method(i);
      std::string arg_name =
          method->ClientStreaming() ? "request_iterator" : "request";
      StringMap method_dict;
      method_dict["Method"] = method->name();
      method_dict["ArgName"] = arg_name;
      out->Print(method_dict,
                 "def $Method$(self, $ArgName$, timeout, metadata=None, "
                 "with_call=False, protocol_options=None):\n");
      {
        IndentScope raii_method_indent(out);
        StringVector method_comments = method->GetAllComments();
        PrintAllComments(method_comments, out);
        out->Print("raise NotImplementedError()\n");
      }
      if (!method->ServerStreaming()) {
        out->Print(method_dict, "$Method$.future = None\n");
      }
    }
  }
  return true;
}

bool PrivateGenerator::PrintBetaServerFactory(
    const std::string& package_qualified_service_name,
    const grpc_generator::Service* service, grpc_generator::Printer* out) {
  StringMap service_dict;
  service_dict["Service"] = service->name();
  out->Print("\n\n");
  out->Print(service_dict,
             "def beta_create_$Service$_server(servicer, pool=None, "
             "pool_size=None, default_timeout=None, maximum_timeout=None):\n");
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
      auto method = service->method(i);
      const std::string method_implementation_constructor =
          std::string(method->ClientStreaming() ? "stream_" : "unary_") +
          std::string(method->ServerStreaming() ? "stream_" : "unary_") +
          "inline";
      std::string input_message_module_and_class;
      if (!method->get_module_and_message_path_input(
              &input_message_module_and_class, generator_file_name,
              generate_in_pb2_grpc, config.import_prefix,
              config.prefixes_to_filter)) {
        return false;
      }
      std::string output_message_module_and_class;
      if (!method->get_module_and_message_path_output(
              &output_message_module_and_class, generator_file_name,
              generate_in_pb2_grpc, config.import_prefix,
              config.prefixes_to_filter)) {
        return false;
      }
      method_implementation_constructors.insert(
          make_pair(method->name(), method_implementation_constructor));
      input_message_modules_and_classes.insert(
          make_pair(method->name(), input_message_module_and_class));
      output_message_modules_and_classes.insert(
          make_pair(method->name(), output_message_module_and_class));
    }
    StringMap method_dict;
    method_dict["PackageQualifiedServiceName"] = package_qualified_service_name;
    out->Print("request_deserializers = {\n");
    for (StringMap::iterator name_and_input_module_class_pair =
             input_message_modules_and_classes.begin();
         name_and_input_module_class_pair !=
         input_message_modules_and_classes.end();
         name_and_input_module_class_pair++) {
      method_dict["MethodName"] = name_and_input_module_class_pair->first;
      method_dict["InputTypeModuleAndClass"] =
          name_and_input_module_class_pair->second;
      IndentScope raii_indent(out);
      out->Print(method_dict,
                 "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
                 "$InputTypeModuleAndClass$.FromString,\n");
    }
    out->Print("}\n");
    out->Print("response_serializers = {\n");
    for (StringMap::iterator name_and_output_module_class_pair =
             output_message_modules_and_classes.begin();
         name_and_output_module_class_pair !=
         output_message_modules_and_classes.end();
         name_and_output_module_class_pair++) {
      method_dict["MethodName"] = name_and_output_module_class_pair->first;
      method_dict["OutputTypeModuleAndClass"] =
          name_and_output_module_class_pair->second;
      IndentScope raii_indent(out);
      out->Print(method_dict,
                 "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
                 "$OutputTypeModuleAndClass$.SerializeToString,\n");
    }
    out->Print("}\n");
    out->Print("method_implementations = {\n");
    for (StringMap::iterator name_and_implementation_constructor =
             method_implementation_constructors.begin();
         name_and_implementation_constructor !=
         method_implementation_constructors.end();
         name_and_implementation_constructor++) {
      method_dict["Method"] = name_and_implementation_constructor->first;
      method_dict["Constructor"] = name_and_implementation_constructor->second;
      IndentScope raii_descriptions_indent(out);
      const std::string method_name =
          name_and_implementation_constructor->first;
      out->Print(method_dict,
                 "(\'$PackageQualifiedServiceName$\', \'$Method$\'): "
                 "face_utilities.$Constructor$(servicer.$Method$),\n");
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
    const std::string& package_qualified_service_name,
    const grpc_generator::Service* service, grpc_generator::Printer* out) {
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
      auto method = service->method(i);
      const std::string method_cardinality =
          std::string(method->ClientStreaming() ? "STREAM" : "UNARY") + "_" +
          std::string(method->ServerStreaming() ? "STREAM" : "UNARY");
      std::string input_message_module_and_class;
      if (!method->get_module_and_message_path_input(
              &input_message_module_and_class, generator_file_name,
              generate_in_pb2_grpc, config.import_prefix,
              config.prefixes_to_filter)) {
        return false;
      }
      std::string output_message_module_and_class;
      if (!method->get_module_and_message_path_output(
              &output_message_module_and_class, generator_file_name,
              generate_in_pb2_grpc, config.import_prefix,
              config.prefixes_to_filter)) {
        return false;
      }
      method_cardinalities.insert(
          make_pair(method->name(), method_cardinality));
      input_message_modules_and_classes.insert(
          make_pair(method->name(), input_message_module_and_class));
      output_message_modules_and_classes.insert(
          make_pair(method->name(), output_message_module_and_class));
    }
    StringMap method_dict;
    method_dict["PackageQualifiedServiceName"] = package_qualified_service_name;
    out->Print("request_serializers = {\n");
    for (StringMap::iterator name_and_input_module_class_pair =
             input_message_modules_and_classes.begin();
         name_and_input_module_class_pair !=
         input_message_modules_and_classes.end();
         name_and_input_module_class_pair++) {
      method_dict["MethodName"] = name_and_input_module_class_pair->first;
      method_dict["InputTypeModuleAndClass"] =
          name_and_input_module_class_pair->second;
      IndentScope raii_indent(out);
      out->Print(method_dict,
                 "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
                 "$InputTypeModuleAndClass$.SerializeToString,\n");
    }
    out->Print("}\n");
    out->Print("response_deserializers = {\n");
    for (StringMap::iterator name_and_output_module_class_pair =
             output_message_modules_and_classes.begin();
         name_and_output_module_class_pair !=
         output_message_modules_and_classes.end();
         name_and_output_module_class_pair++) {
      method_dict["MethodName"] = name_and_output_module_class_pair->first;
      method_dict["OutputTypeModuleAndClass"] =
          name_and_output_module_class_pair->second;
      IndentScope raii_indent(out);
      out->Print(method_dict,
                 "(\'$PackageQualifiedServiceName$\', \'$MethodName$\'): "
                 "$OutputTypeModuleAndClass$.FromString,\n");
    }
    out->Print("}\n");
    out->Print("cardinalities = {\n");
    for (StringMap::iterator name_and_cardinality =
             method_cardinalities.begin();
         name_and_cardinality != method_cardinalities.end();
         name_and_cardinality++) {
      method_dict["Method"] = name_and_cardinality->first;
      method_dict["Cardinality"] = name_and_cardinality->second;
      IndentScope raii_descriptions_indent(out);
      out->Print(method_dict,
                 "\'$Method$\': cardinality.Cardinality.$Cardinality$,\n");
    }
    out->Print("}\n");
    out->Print(
        "stub_options = beta_implementations.stub_options("
        "host=host, metadata_transformer=metadata_transformer, "
        "request_serializers=request_serializers, "
        "response_deserializers=response_deserializers, "
        "thread_pool=pool, thread_pool_size=pool_size)\n");
    out->Print(method_dict,
               "return beta_implementations.dynamic_stub(channel, "
               "\'$PackageQualifiedServiceName$\', "
               "cardinalities, options=stub_options)\n");
  }
  return true;
}

bool PrivateGenerator::PrintStub(
    const std::string& package_qualified_service_name,
    const grpc_generator::Service* service, grpc_generator::Printer* out) {
  StringMap dict;
  dict["Service"] = service->name();
  out->Print("\n\n");
  out->Print(dict, "class $Service$Stub(object):\n");
  {
    IndentScope raii_class_indent(out);
    StringVector service_comments = service->GetAllComments();
    PrintAllComments(service_comments, out);
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
        auto method = service->method(i);
        std::string multi_callable_constructor =
            std::string(method->ClientStreaming() ? "stream" : "unary") + "_" +
            std::string(method->ServerStreaming() ? "stream" : "unary");
        std::string request_module_and_class;
        if (!method->get_module_and_message_path_input(
                &request_module_and_class, generator_file_name,
                generate_in_pb2_grpc, config.import_prefix,
                config.prefixes_to_filter)) {
          return false;
        }
        std::string response_module_and_class;
        if (!method->get_module_and_message_path_output(
                &response_module_and_class, generator_file_name,
                generate_in_pb2_grpc, config.import_prefix,
                config.prefixes_to_filter)) {
          return false;
        }
        StringMap method_dict;
        method_dict["Method"] = method->name();
        method_dict["MultiCallableConstructor"] = multi_callable_constructor;
        out->Print(method_dict,
                   "self.$Method$ = channel.$MultiCallableConstructor$(\n");
        {
          method_dict["PackageQualifiedService"] =
              package_qualified_service_name;
          method_dict["RequestModuleAndClass"] = request_module_and_class;
          method_dict["ResponseModuleAndClass"] = response_module_and_class;
          IndentScope raii_first_attribute_indent(out);
          IndentScope raii_second_attribute_indent(out);
          out->Print(method_dict, "'/$PackageQualifiedService$/$Method$',\n");
          out->Print(method_dict,
                     "request_serializer=$RequestModuleAndClass$."
                     "SerializeToString,\n");
          out->Print(
              method_dict,
              "response_deserializer=$ResponseModuleAndClass$.FromString,\n");
          out->Print(")\n");
        }
      }
    }
  }
  return true;
}

bool PrivateGenerator::PrintServicer(const grpc_generator::Service* service,
                                     grpc_generator::Printer* out) {
  StringMap service_dict;
  service_dict["Service"] = service->name();
  out->Print("\n\n");
  out->Print(service_dict, "class $Service$Servicer(object):\n");
  {
    IndentScope raii_class_indent(out);
    StringVector service_comments = service->GetAllComments();
    PrintAllComments(service_comments, out);
    for (int i = 0; i < service->method_count(); ++i) {
      auto method = service->method(i);
      std::string arg_name =
          method->ClientStreaming() ? "request_iterator" : "request";
      StringMap method_dict;
      method_dict["Method"] = method->name();
      method_dict["ArgName"] = arg_name;
      out->Print("\n");
      out->Print(method_dict, "def $Method$(self, $ArgName$, context):\n");
      {
        IndentScope raii_method_indent(out);
        StringVector method_comments = method->GetAllComments();
        PrintAllComments(method_comments, out);
        out->Print("context.set_code(grpc.StatusCode.UNIMPLEMENTED)\n");
        out->Print("context.set_details('Method not implemented!')\n");
        out->Print("raise NotImplementedError('Method not implemented!')\n");
      }
    }
  }
  return true;
}

bool PrivateGenerator::PrintAddServicerToServer(
    const std::string& package_qualified_service_name,
    const grpc_generator::Service* service, grpc_generator::Printer* out) {
  StringMap service_dict;
  service_dict["Service"] = service->name();
  out->Print("\n\n");
  out->Print(service_dict,
             "def add_$Service$Servicer_to_server(servicer, server):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print("rpc_method_handlers = {\n");
    {
      IndentScope raii_dict_first_indent(out);
      IndentScope raii_dict_second_indent(out);
      for (int i = 0; i < service->method_count(); ++i) {
        auto method = service->method(i);
        std::string method_handler_constructor =
            std::string(method->ClientStreaming() ? "stream" : "unary") + "_" +
            std::string(method->ServerStreaming() ? "stream" : "unary") +
            "_rpc_method_handler";
        std::string request_module_and_class;
        if (!method->get_module_and_message_path_input(
                &request_module_and_class, generator_file_name,
                generate_in_pb2_grpc, config.import_prefix,
                config.prefixes_to_filter)) {
          return false;
        }
        std::string response_module_and_class;
        if (!method->get_module_and_message_path_output(
                &response_module_and_class, generator_file_name,
                generate_in_pb2_grpc, config.import_prefix,
                config.prefixes_to_filter)) {
          return false;
        }
        StringMap method_dict;
        method_dict["Method"] = method->name();
        method_dict["MethodHandlerConstructor"] = method_handler_constructor;
        method_dict["RequestModuleAndClass"] = request_module_and_class;
        method_dict["ResponseModuleAndClass"] = response_module_and_class;
        out->Print(method_dict,
                   "'$Method$': grpc.$MethodHandlerConstructor$(\n");
        {
          IndentScope raii_call_first_indent(out);
          IndentScope raii_call_second_indent(out);
          out->Print(method_dict, "servicer.$Method$,\n");
          out->Print(
              method_dict,
              "request_deserializer=$RequestModuleAndClass$.FromString,\n");
          out->Print(
              method_dict,
              "response_serializer=$ResponseModuleAndClass$.SerializeToString,"
              "\n");
        }
        out->Print("),\n");
      }
    }
    StringMap method_dict;
    method_dict["PackageQualifiedServiceName"] = package_qualified_service_name;
    out->Print("}\n");
    out->Print("generic_handler = grpc.method_handlers_generic_handler(\n");
    {
      IndentScope raii_call_first_indent(out);
      IndentScope raii_call_second_indent(out);
      out->Print(method_dict,
                 "'$PackageQualifiedServiceName$', rpc_method_handlers)\n");
    }
    out->Print("server.add_generic_rpc_handlers((generic_handler,))\n");
  }
  return true;
}

/* Prints out a service class used as a container for static methods pertaining
 * to a class. This class has the exact name of service written in the ".proto"
 * file, with no suffixes. Since this class merely acts as a namespace, it
 * should never be instantiated.
 */
bool PrivateGenerator::PrintServiceClass(
    const std::string& package_qualified_service_name,
    const grpc_generator::Service* service, grpc_generator::Printer* out) {
  StringMap dict;
  dict["Service"] = service->name();
  out->Print("\n\n");
  out->Print(" # This class is part of an EXPERIMENTAL API.\n");
  out->Print(dict, "class $Service$(object):\n");
  {
    IndentScope class_indent(out);
    StringVector service_comments = service->GetAllComments();
    PrintAllComments(service_comments, out);
    for (int i = 0; i < service->method_count(); ++i) {
      const auto& method = service->method(i);
      std::string request_module_and_class;
      if (!method->get_module_and_message_path_input(
              &request_module_and_class, generator_file_name,
              generate_in_pb2_grpc, config.import_prefix,
              config.prefixes_to_filter)) {
        return false;
      }
      std::string response_module_and_class;
      if (!method->get_module_and_message_path_output(
              &response_module_and_class, generator_file_name,
              generate_in_pb2_grpc, config.import_prefix,
              config.prefixes_to_filter)) {
        return false;
      }
      out->Print("\n");
      StringMap method_dict;
      method_dict["Method"] = method->name();
      out->Print("@staticmethod\n");
      out->Print(method_dict, "def $Method$(");
      std::string request_parameter(
          method->ClientStreaming() ? "request_iterator" : "request");
      StringMap args_dict;
      args_dict["RequestParameter"] = request_parameter;
      {
        IndentScope args_indent(out);
        IndentScope args_double_indent(out);
        out->Print(args_dict, "$RequestParameter$,\n");
        out->Print("target,\n");
        out->Print("options=(),\n");
        out->Print("channel_credentials=None,\n");
        out->Print("call_credentials=None,\n");
        out->Print("insecure=False,\n");
        out->Print("compression=None,\n");
        out->Print("wait_for_ready=None,\n");
        out->Print("timeout=None,\n");
        out->Print("metadata=None):\n");
      }
      {
        IndentScope method_indent(out);
        std::string arity_method_name =
            std::string(method->ClientStreaming() ? "stream" : "unary") + "_" +
            std::string(method->ServerStreaming() ? "stream" : "unary");
        args_dict["ArityMethodName"] = arity_method_name;
        args_dict["PackageQualifiedService"] = package_qualified_service_name;
        args_dict["Method"] = method->name();
        out->Print(args_dict,
                   "return "
                   "grpc.experimental.$ArityMethodName$($RequestParameter$, "
                   "target, '/$PackageQualifiedService$/$Method$',\n");
        {
          IndentScope continuation_indent(out);
          StringMap serializer_dict;
          serializer_dict["RequestModuleAndClass"] = request_module_and_class;
          serializer_dict["ResponseModuleAndClass"] = response_module_and_class;
          out->Print(serializer_dict,
                     "$RequestModuleAndClass$.SerializeToString,\n");
          out->Print(serializer_dict, "$ResponseModuleAndClass$.FromString,\n");
          out->Print("options, channel_credentials,\n");
          out->Print(
              "insecure, call_credentials, compression, wait_for_ready, "
              "timeout, metadata)\n");
        }
      }
    }
  }
  // TODO(rbellevi): Add methods pertinent to the server side as well.
  return true;
}

bool PrivateGenerator::PrintBetaPreamble(grpc_generator::Printer* out) {
  StringMap var;
  var["Package"] = config.beta_package_root;
  out->Print(var,
             "from $Package$ import implementations as beta_implementations\n");
  out->Print(var, "from $Package$ import interfaces as beta_interfaces\n");
  out->Print("from grpc.framework.common import cardinality\n");
  out->Print(
      "from grpc.framework.interfaces.face import utilities as "
      "face_utilities\n");
  return true;
}

bool PrivateGenerator::PrintPreamble(grpc_generator::Printer* out) {
  StringMap var;
  var["Package"] = config.grpc_package_root;
  out->Print(var, "import $Package$\n");
  if (generate_in_pb2_grpc) {
    out->Print("\n");
    StringPairSet imports_set;
    for (int i = 0; i < file->service_count(); ++i) {
      auto service = file->service(i);
      for (int j = 0; j < service->method_count(); ++j) {
        auto method = service.get()->method(j);

        std::string input_type_file_name = method->get_input_type_name();
        std::string input_module_name =
            ModuleName(input_type_file_name, config.import_prefix,
                       config.prefixes_to_filter);
        std::string input_module_alias =
            ModuleAlias(input_type_file_name, config.import_prefix,
                        config.prefixes_to_filter);
        imports_set.insert(
            std::make_tuple(input_module_name, input_module_alias));

        std::string output_type_file_name = method->get_output_type_name();
        std::string output_module_name =
            ModuleName(output_type_file_name, config.import_prefix,
                       config.prefixes_to_filter);
        std::string output_module_alias =
            ModuleAlias(output_type_file_name, config.import_prefix,
                        config.prefixes_to_filter);
        imports_set.insert(
            std::make_tuple(output_module_name, output_module_alias));
      }
    }

    for (StringPairSet::iterator it = imports_set.begin();
         it != imports_set.end(); ++it) {
      auto module_name = std::get<0>(*it);
      var["ModuleAlias"] = std::get<1>(*it);
      const size_t last_dot_pos = module_name.rfind('.');
      if (last_dot_pos == std::string::npos) {
        var["ImportStatement"] = "import " + module_name;
      } else {
        var["ImportStatement"] = "from " + module_name.substr(0, last_dot_pos) +
                                 " import " +
                                 module_name.substr(last_dot_pos + 1);
      }
      out->Print(var, "$ImportStatement$ as $ModuleAlias$\n");
    }
  }
  return true;
}

bool PrivateGenerator::PrintGAServices(grpc_generator::Printer* out) {
  std::string package = file->package();
  if (!package.empty()) {
    package = package.append(".");
  }
  for (int i = 0; i < file->service_count(); ++i) {
    auto service = file->service(i);
    std::string package_qualified_service_name = package + service->name();
    if (!(PrintStub(package_qualified_service_name, service.get(), out) &&
          PrintServicer(service.get(), out) &&
          PrintAddServicerToServer(package_qualified_service_name,
                                   service.get(), out) &&
          PrintServiceClass(package_qualified_service_name, service.get(),
                            out))) {
      return false;
    }
  }
  return true;
}

bool PrivateGenerator::PrintBetaServices(grpc_generator::Printer* out) {
  std::string package = file->package();
  if (!package.empty()) {
    package = package.append(".");
  }
  for (int i = 0; i < file->service_count(); ++i) {
    auto service = file->service(i);
    std::string package_qualified_service_name = package + service->name();
    if (!(PrintBetaServicer(service.get(), out) &&
          PrintBetaStub(service.get(), out) &&
          PrintBetaServerFactory(package_qualified_service_name, service.get(),
                                 out) &&
          PrintBetaStubFactory(package_qualified_service_name, service.get(),
                               out))) {
      return false;
    }
  }
  return true;
}

pair<bool, std::string> PrivateGenerator::GetGrpcServices() {
  std::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    auto out = file->CreatePrinter(&output);
    if (generate_in_pb2_grpc) {
      out->Print(
          "# Generated by the gRPC Python protocol compiler plugin. "
          "DO NOT EDIT!\n\"\"\""
          "Client and server classes corresponding to protobuf-defined "
          "services.\"\"\"\n");
      if (!PrintPreamble(out.get())) {
        return make_pair(false, "");
      }
      if (!PrintGAServices(out.get())) {
        return make_pair(false, "");
      }
    } else {
      out->Print("try:\n");
      {
        IndentScope raii_dict_try_indent(out.get());
        out->Print(
            "# THESE ELEMENTS WILL BE DEPRECATED.\n"
            "# Please use the generated *_pb2_grpc.py files instead.\n");
        if (!PrintPreamble(out.get())) {
          return make_pair(false, "");
        }
        if (!PrintBetaPreamble(out.get())) {
          return make_pair(false, "");
        }
        if (!PrintGAServices(out.get())) {
          return make_pair(false, "");
        }
        if (!PrintBetaServices(out.get())) {
          return make_pair(false, "");
        }
      }
      out->Print("except ImportError:\n");
      {
        IndentScope raii_dict_except_indent(out.get());
        out->Print("pass");
      }
    }
  }
  return make_pair(true, std::move(output));
}

}  // namespace

GeneratorConfiguration::GeneratorConfiguration()
    : grpc_package_root("grpc"),
      beta_package_root("grpc.beta"),
      import_prefix("") {}

PythonGrpcGenerator::PythonGrpcGenerator(const GeneratorConfiguration& config)
    : config_(config) {}

PythonGrpcGenerator::~PythonGrpcGenerator() {}

static bool GenerateGrpc(GeneratorContext* context, PrivateGenerator& generator,
                         std::string file_name, bool generate_in_pb2_grpc) {
  bool success;
  std::unique_ptr<ZeroCopyOutputStream> output;
  std::unique_ptr<CodedOutputStream> coded_output;
  std::string grpc_code;

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

static bool ParseParameters(const std::string& parameter,
                            std::string* grpc_version,
                            std::vector<std::string>* strip_prefixes,
                            std::string* error) {
  std::vector<std::string> comma_delimited_parameters;
  grpc_python_generator::Split(parameter, ',', &comma_delimited_parameters);
  if (comma_delimited_parameters.size() == 1 &&
      comma_delimited_parameters[0].empty()) {
    *grpc_version = "grpc_2_0";
  } else if (comma_delimited_parameters.size() == 1) {
    *grpc_version = comma_delimited_parameters[0];
  } else if (comma_delimited_parameters.size() == 2) {
    *grpc_version = comma_delimited_parameters[0];
    std::copy(comma_delimited_parameters.begin() + 1,
              comma_delimited_parameters.end(),
              std::back_inserter(*strip_prefixes));
  } else {
    *error = "--grpc_python_out received too many comma-delimited parameters.";
    return false;
  }
  return true;
}

uint64_t PythonGrpcGenerator::GetSupportedFeatures() const {
  return FEATURE_PROTO3_OPTIONAL;
}

bool PythonGrpcGenerator::Generate(const FileDescriptor* file,
                                   const std::string& parameter,
                                   GeneratorContext* context,
                                   std::string* error) const {
  // Get output file name.
  std::string pb2_file_name;
  std::string pb2_grpc_file_name;
  static const int proto_suffix_length = strlen(".proto");
  if (file->name().size() > static_cast<size_t>(proto_suffix_length) &&
      file->name().find_last_of(".proto") == file->name().size() - 1) {
    std::string base =
        file->name().substr(0, file->name().size() - proto_suffix_length);
    std::replace(base.begin(), base.end(), '-', '_');
    pb2_file_name = base + "_pb2.py";
    pb2_grpc_file_name = base + "_pb2_grpc.py";
  } else {
    *error = "Invalid proto file name. Proto file must end with .proto";
    return false;
  }
  generator_file_name = file->name();

  ProtoBufFile pbfile(file);
  std::string grpc_version;
  GeneratorConfiguration extended_config(config_);
  bool success = ParseParameters(parameter, &grpc_version,
                                 &(extended_config.prefixes_to_filter), error);
  PrivateGenerator generator(extended_config, &pbfile);
  if (!success) return false;
  if (grpc_version == "grpc_2_0") {
    return GenerateGrpc(context, generator, pb2_grpc_file_name, true);
  } else if (grpc_version == "grpc_1_0") {
    return GenerateGrpc(context, generator, pb2_grpc_file_name, true) &&
           GenerateGrpc(context, generator, pb2_file_name, false);
  } else {
    *error = "Invalid grpc version '" + grpc_version + "'.";
    return false;
  }
}

}  // namespace grpc_python_generator
