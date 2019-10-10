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

#include <map>
#include <set>
#include <sstream>

#include "src/compiler/config.h"
#include "src/compiler/objective_c_generator.h"
#include "src/compiler/objective_c_generator_helpers.h"

#include <google/protobuf/compiler/objectivec/objectivec_helpers.h>

using ::google::protobuf::compiler::objectivec::ClassName;
using ::grpc::protobuf::FileDescriptor;
using ::grpc::protobuf::FileDescriptor;
using ::grpc::protobuf::MethodDescriptor;
using ::grpc::protobuf::ServiceDescriptor;
using ::grpc::protobuf::io::Printer;
using ::std::map;
using ::std::set;

namespace grpc_objective_c_generator {
namespace {

void PrintProtoRpcDeclarationAsPragma(
    Printer* printer, const MethodDescriptor* method,
    map< ::grpc::string, ::grpc::string> vars) {
  vars["client_stream"] = method->client_streaming() ? "stream " : "";
  vars["server_stream"] = method->server_streaming() ? "stream " : "";

  printer->Print(vars,
                 "#pragma mark $method_name$($client_stream$$request_type$)"
                 " returns ($server_stream$$response_type$)\n\n");
}

template <typename DescriptorType>
static void PrintAllComments(const DescriptorType* desc, Printer* printer,
                             bool deprecated = false) {
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
  printer->Print("/**\n");
  for (auto it = comments.begin(); it != comments.end(); ++it) {
    printer->Print(" * ");
    size_t start_pos = it->find_first_not_of(' ');
    if (start_pos != grpc::string::npos) {
      printer->PrintRaw(it->c_str() + start_pos);
    }
    printer->Print("\n");
  }
  if (deprecated) {
    printer->Print(" *\n");
    printer->Print(
        " * This method belongs to a set of APIs that have been deprecated. "
        "Using"
        " the v2 API is recommended.\n");
  }
  printer->Print(" */\n");
}

void PrintMethodSignature(Printer* printer, const MethodDescriptor* method,
                          const map< ::grpc::string, ::grpc::string>& vars) {
  // Print comment
  PrintAllComments(method, printer, true);

  printer->Print(vars, "- ($return_type$)$method_name$With");
  if (method->client_streaming()) {
    printer->Print("RequestsWriter:(GRXWriter *)requestWriter");
  } else {
    printer->Print(vars, "Request:($request_class$ *)request");
  }

  // TODO(jcanizales): Put this on a new line and align colons.
  if (method->server_streaming()) {
    printer->Print(vars,
                   " eventHandler:(void(^)(BOOL done, "
                   "$response_class$ *_Nullable response, NSError *_Nullable "
                   "error))eventHandler");
  } else {
    printer->Print(vars,
                   " handler:(void(^)($response_class$ *_Nullable response, "
                   "NSError *_Nullable error))handler");
  }
}

void PrintSimpleSignature(Printer* printer, const MethodDescriptor* method,
                          map< ::grpc::string, ::grpc::string> vars) {
  vars["method_name"] =
      grpc_generator::LowercaseFirstLetter(vars["method_name"]);
  vars["return_type"] = "void";
  PrintMethodSignature(printer, method, vars);
}

void PrintAdvancedSignature(Printer* printer, const MethodDescriptor* method,
                            map< ::grpc::string, ::grpc::string> vars) {
  vars["method_name"] = "RPCTo" + vars["method_name"];
  vars["return_type"] = "GRPCProtoCall *";
  PrintMethodSignature(printer, method, vars);
}

void PrintV2Signature(Printer* printer, const MethodDescriptor* method,
                      map< ::grpc::string, ::grpc::string> vars) {
  if (method->client_streaming()) {
    vars["return_type"] = "GRPCStreamingProtoCall *";
  } else {
    vars["return_type"] = "GRPCUnaryProtoCall *";
  }
  vars["method_name"] =
      grpc_generator::LowercaseFirstLetter(vars["method_name"]);

  PrintAllComments(method, printer);

  printer->Print(vars, "- ($return_type$)$method_name$With");
  if (method->client_streaming()) {
    printer->Print("ResponseHandler:(id<GRPCProtoResponseHandler>)handler");
  } else {
    printer->Print(vars,
                   "Message:($request_class$ *)message "
                   "responseHandler:(id<GRPCProtoResponseHandler>)handler");
  }
  printer->Print(" callOptions:(GRPCCallOptions *_Nullable)callOptions");
}

inline map< ::grpc::string, ::grpc::string> GetMethodVars(
    const MethodDescriptor* method) {
  map< ::grpc::string, ::grpc::string> res;
  res["method_name"] = method->name();
  res["request_type"] = method->input_type()->name();
  res["response_type"] = method->output_type()->name();
  res["request_class"] = ClassName(method->input_type());
  res["response_class"] = ClassName(method->output_type());
  return res;
}

void PrintMethodDeclarations(Printer* printer, const MethodDescriptor* method) {
  map< ::grpc::string, ::grpc::string> vars = GetMethodVars(method);

  PrintProtoRpcDeclarationAsPragma(printer, method, vars);

  PrintSimpleSignature(printer, method, vars);
  printer->Print(";\n\n");
  PrintAdvancedSignature(printer, method, vars);
  printer->Print(";\n\n\n");
}

void PrintV2MethodDeclarations(Printer* printer,
                               const MethodDescriptor* method) {
  map< ::grpc::string, ::grpc::string> vars = GetMethodVars(method);

  PrintProtoRpcDeclarationAsPragma(printer, method, vars);

  PrintV2Signature(printer, method, vars);
  printer->Print(";\n\n");
}

void PrintSimpleImplementation(Printer* printer, const MethodDescriptor* method,
                               map< ::grpc::string, ::grpc::string> vars) {
  printer->Print("{\n");
  printer->Print(vars, "  [[self RPCTo$method_name$With");
  if (method->client_streaming()) {
    printer->Print("RequestsWriter:requestWriter");
  } else {
    printer->Print("Request:request");
  }
  if (method->server_streaming()) {
    printer->Print(" eventHandler:eventHandler] start];\n");
  } else {
    printer->Print(" handler:handler] start];\n");
  }
  printer->Print("}\n");
}

void PrintAdvancedImplementation(Printer* printer,
                                 const MethodDescriptor* method,
                                 map< ::grpc::string, ::grpc::string> vars) {
  printer->Print("{\n");
  printer->Print(vars, "  return [self RPCToMethod:@\"$method_name$\"\n");

  printer->Print("            requestsWriter:");
  if (method->client_streaming()) {
    printer->Print("requestWriter\n");
  } else {
    printer->Print("[GRXWriter writerWithValue:request]\n");
  }

  printer->Print(vars, "             responseClass:[$response_class$ class]\n");

  printer->Print("        responsesWriteable:[GRXWriteable ");
  if (method->server_streaming()) {
    printer->Print("writeableWithEventHandler:eventHandler]];\n");
  } else {
    printer->Print("writeableWithSingleHandler:handler]];\n");
  }

  printer->Print("}\n");
}

void PrintV2Implementation(Printer* printer, const MethodDescriptor* method,
                           map< ::grpc::string, ::grpc::string> vars) {
  printer->Print(" {\n");
  if (method->client_streaming()) {
    printer->Print(vars, "  return [self RPCToMethod:@\"$method_name$\"\n");
    printer->Print("           responseHandler:handler\n");
    printer->Print("               callOptions:callOptions\n");
    printer->Print(
        vars, "             responseClass:[$response_class$ class]];\n}\n\n");
  } else {
    printer->Print(vars, "  return [self RPCToMethod:@\"$method_name$\"\n");
    printer->Print("                   message:message\n");
    printer->Print("           responseHandler:handler\n");
    printer->Print("               callOptions:callOptions\n");
    printer->Print(
        vars, "             responseClass:[$response_class$ class]];\n}\n\n");
  }
}

void PrintMethodImplementations(Printer* printer,
                                const MethodDescriptor* method,
                                const Parameters& generator_params) {
  map< ::grpc::string, ::grpc::string> vars = GetMethodVars(method);

  PrintProtoRpcDeclarationAsPragma(printer, method, vars);

  if (!generator_params.no_v1_compatibility) {
    // TODO(jcanizales): Print documentation from the method.
    PrintSimpleSignature(printer, method, vars);
    PrintSimpleImplementation(printer, method, vars);

    printer->Print("// Returns a not-yet-started RPC object.\n");
    PrintAdvancedSignature(printer, method, vars);
    PrintAdvancedImplementation(printer, method, vars);
  }

  PrintV2Signature(printer, method, vars);
  PrintV2Implementation(printer, method, vars);
}

}  // namespace

::grpc::string GetAllMessageClasses(const FileDescriptor* file) {
  ::grpc::string output;
  set< ::grpc::string> classes;
  for (int i = 0; i < file->service_count(); i++) {
    const auto service = file->service(i);
    for (int i = 0; i < service->method_count(); i++) {
      const auto method = service->method(i);
      classes.insert(ClassName(method->input_type()));
      classes.insert(ClassName(method->output_type()));
    }
  }
  for (auto one_class : classes) {
    output += "@class " + one_class + ";\n";
  }

  return output;
}

::grpc::string GetProtocol(const ServiceDescriptor* service,
                           const Parameters& generator_params) {
  ::grpc::string output;

  if (generator_params.no_v1_compatibility) return output;

  // Scope the output stream so it closes and finalizes output to the string.
  grpc::protobuf::io::StringOutputStream output_stream(&output);
  Printer printer(&output_stream, '$');

  map< ::grpc::string, ::grpc::string> vars = {
      {"service_class", ServiceClassName(service)}};

  printer.Print(vars,
                "/**\n"
                " * The methods in this protocol belong to a set of old APIs "
                "that have been deprecated. They do not\n"
                " * recognize call options provided in the initializer. Using "
                "the v2 protocol is recommended.\n"
                " */\n");
  printer.Print(vars, "@protocol $service_class$ <NSObject>\n\n");
  for (int i = 0; i < service->method_count(); i++) {
    PrintMethodDeclarations(&printer, service->method(i));
  }
  printer.Print("@end\n\n");

  return output;
}

::grpc::string GetV2Protocol(const ServiceDescriptor* service) {
  ::grpc::string output;

  // Scope the output stream so it closes and finalizes output to the string.
  grpc::protobuf::io::StringOutputStream output_stream(&output);
  Printer printer(&output_stream, '$');

  map< ::grpc::string, ::grpc::string> vars = {
      {"service_class", ServiceClassName(service) + "2"}};

  printer.Print(vars, "@protocol $service_class$ <NSObject>\n\n");
  for (int i = 0; i < service->method_count(); i++) {
    PrintV2MethodDeclarations(&printer, service->method(i));
  }
  printer.Print("@end\n\n");

  return output;
}

::grpc::string GetInterface(const ServiceDescriptor* service,
                            const Parameters& generator_params) {
  ::grpc::string output;

  // Scope the output stream so it closes and finalizes output to the string.
  grpc::protobuf::io::StringOutputStream output_stream(&output);
  Printer printer(&output_stream, '$');

  map< ::grpc::string, ::grpc::string> vars = {
      {"service_class", ServiceClassName(service)}};

  printer.Print(vars,
                "/**\n"
                " * Basic service implementation, over gRPC, that only does\n"
                " * marshalling and parsing.\n"
                " */\n");
  printer.Print(vars,
                "@interface $service_class$ :"
                " GRPCProtoService<$service_class$2");
  if (!generator_params.no_v1_compatibility) {
    printer.Print(vars, ", $service_class$");
  }
  printer.Print(">\n");
  printer.Print(
      "- (instancetype)initWithHost:(NSString *)host "
      "callOptions:(GRPCCallOptions "
      "*_Nullable)callOptions"
      " NS_DESIGNATED_INITIALIZER;\n");
  printer.Print(
      "+ (instancetype)serviceWithHost:(NSString *)host "
      "callOptions:(GRPCCallOptions *_Nullable)callOptions;\n");
  if (!generator_params.no_v1_compatibility) {
    printer.Print(
        "// The following methods belong to a set of old APIs that have been "
        "deprecated.\n");
    printer.Print("- (instancetype)initWithHost:(NSString *)host;\n");
    printer.Print("+ (instancetype)serviceWithHost:(NSString *)host;\n");
  }
  printer.Print("@end\n");

  return output;
}

::grpc::string GetSource(const ServiceDescriptor* service,
                         const Parameters& generator_params) {
  ::grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    grpc::protobuf::io::StringOutputStream output_stream(&output);
    Printer printer(&output_stream, '$');

    map< ::grpc::string, ::grpc::string> vars = {
        {"service_name", service->name()},
        {"service_class", ServiceClassName(service)},
        {"package", service->file()->package()}};

    printer.Print(vars,
                  "@implementation $service_class$\n\n"
                  "#pragma clang diagnostic push\n"
                  "#pragma clang diagnostic ignored "
                  "\"-Wobjc-designated-initializers\"\n\n"
                  "// Designated initializer\n"
                  "- (instancetype)initWithHost:(NSString *)host "
                  "callOptions:(GRPCCallOptions *_Nullable)callOptions {\n"
                  "  return [super initWithHost:host\n"
                  "                 packageName:@\"$package$\"\n"
                  "                 serviceName:@\"$service_name$\"\n"
                  "                 callOptions:callOptions];\n"
                  "}\n\n");
    if (!generator_params.no_v1_compatibility) {
      printer.Print(vars,
                    "- (instancetype)initWithHost:(NSString *)host {\n"
                    "  return [super initWithHost:host\n"
                    "                 packageName:@\"$package$\"\n"
                    "                 serviceName:@\"$service_name$\"];\n"
                    "}\n\n");
    }
    printer.Print("#pragma clang diagnostic pop\n\n");

    if (!generator_params.no_v1_compatibility) {
      printer.Print(
          "// Override superclass initializer to disallow different"
          " package and service names.\n"
          "- (instancetype)initWithHost:(NSString *)host\n"
          "                 packageName:(NSString *)packageName\n"
          "                 serviceName:(NSString *)serviceName {\n"
          "  return [self initWithHost:host];\n"
          "}\n\n");
    }
    printer.Print(
        "- (instancetype)initWithHost:(NSString *)host\n"
        "                 packageName:(NSString *)packageName\n"
        "                 serviceName:(NSString *)serviceName\n"
        "                 callOptions:(GRPCCallOptions *)callOptions {\n"
        "  return [self initWithHost:host callOptions:callOptions];\n"
        "}\n\n");

    printer.Print("#pragma mark - Class Methods\n\n");
    if (!generator_params.no_v1_compatibility) {
      printer.Print(
          "+ (instancetype)serviceWithHost:(NSString *)host {\n"
          "  return [[self alloc] initWithHost:host];\n"
          "}\n\n");
    }
    printer.Print(
        "+ (instancetype)serviceWithHost:(NSString *)host "
        "callOptions:(GRPCCallOptions *_Nullable)callOptions {\n"
        "  return [[self alloc] initWithHost:host callOptions:callOptions];\n"
        "}\n\n");

    printer.Print("#pragma mark - Method Implementations\n\n");

    for (int i = 0; i < service->method_count(); i++) {
      PrintMethodImplementations(&printer, service->method(i),
                                 generator_params);
    }

    printer.Print("@end\n");
  }
  return output;
}

}  // namespace grpc_objective_c_generator
