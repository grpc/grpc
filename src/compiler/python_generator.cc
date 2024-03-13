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

#include "src/compiler/python_generator.h"

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


bool PrivateGenerator::PrintStub(
    const std::string& package_qualified_service_name,
    const grpc_generator::Service* service, grpc_generator::Printer* out) {
  StringMap dict;
  dict["Service"] = service->name();
  out->Print("\n\n");
  out->Print(dict, "class _$Service$Stub(object):\n");
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
          out->Print("_registered_method=True)\n");
        }
      }
    }
  }
  return true;
}

bool PrivateGenerator::PrintBony(
    const grpc_generator::File* file, grpc_generator::Printer* out) {
  out->Print("\n\n");
  out->Print("class Bony(object):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print("\n");
    out->Print("def __init__(self, api_key):\n");
    {
      IndentScope raii_init_indent(out);
      out->Print("channel = get_channel_from_api_key(api_key)\n");
      out->Print("self.chat = _ChatStub(channel)\n");
      out->Print("self.embeddings = _EmbeddingsStub(channel)\n");
    }
  }
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
    out->Print("\nfrom bony_helpers import get_channel_from_api_key \n");
  }
  return true;
}

bool PrivateGenerator::PrintGAServices(grpc_generator::Printer* out) {
  std::string package = file->package();
  if (!package.empty()) {
    package = package.append(".");
  }
  PrintBony(file, out);
  for (int i = 0; i < file->service_count(); ++i) {
    auto service = file->service(i);
    std::string package_qualified_service_name = package + service->name();
    if (!(PrintStub(package_qualified_service_name, service.get(), out))) {
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
