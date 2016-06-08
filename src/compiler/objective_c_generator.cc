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

#include <map>
#include <sstream>

#include "src/compiler/config.h"
#include "src/compiler/objective_c_generator.h"
#include "src/compiler/objective_c_generator_helpers.h"

#include <google/protobuf/compiler/objectivec/objectivec_helpers.h>

using ::google::protobuf::compiler::objectivec::ClassName;
using ::grpc::protobuf::io::Printer;
using ::grpc::protobuf::MethodDescriptor;
using ::grpc::protobuf::ServiceDescriptor;
using ::std::map;

namespace grpc_objective_c_generator {
namespace {

void PrintProtoRpcDeclarationAsPragma(Printer *printer,
                                      const MethodDescriptor *method,
                                      map< ::grpc::string, ::grpc::string> vars) {
  vars["client_stream"] = method->client_streaming() ? "stream " : "";
  vars["server_stream"] = method->server_streaming() ? "stream " : "";

  printer->Print(vars,
                 "#pragma mark $method_name$($client_stream$$request_type$)"
                 " returns ($server_stream$$response_type$)\n\n");
}

void PrintMethodSignature(Printer *printer, const MethodDescriptor *method,
                          const map< ::grpc::string, ::grpc::string> &vars) {
  // TODO(jcanizales): Print method comments.

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
                   "$response_class$ *_Nullable response, NSError *_Nullable error))eventHandler");
  } else {
    printer->Print(vars,
                   " handler:(void(^)($response_class$ *_Nullable response, "
                   "NSError *_Nullable error))handler");
  }
}

void PrintSimpleSignature(Printer *printer, const MethodDescriptor *method,
                          map< ::grpc::string, ::grpc::string> vars) {
  vars["method_name"] =
      grpc_generator::LowercaseFirstLetter(vars["method_name"]);
  vars["return_type"] = "void";
  PrintMethodSignature(printer, method, vars);
}

void PrintAdvancedSignature(Printer *printer, const MethodDescriptor *method,
                            map< ::grpc::string, ::grpc::string> vars) {
  vars["method_name"] = "RPCTo" + vars["method_name"];
  vars["return_type"] = "GRPCProtoCall *";
  PrintMethodSignature(printer, method, vars);
}

inline map< ::grpc::string, ::grpc::string> GetMethodVars(const MethodDescriptor *method) {
  map< ::grpc::string, ::grpc::string> res;
  res["method_name"] = method->name();
  res["request_type"] = method->input_type()->name();
  res["response_type"] = method->output_type()->name();
  res["request_class"] = ClassName(method->input_type());
  res["response_class"] = ClassName(method->output_type());
  return res;
}

void PrintMethodDeclarations(Printer *printer, const MethodDescriptor *method) {
  map< ::grpc::string, ::grpc::string> vars = GetMethodVars(method);

  PrintProtoRpcDeclarationAsPragma(printer, method, vars);

  PrintSimpleSignature(printer, method, vars);
  printer->Print(";\n\n");
  PrintAdvancedSignature(printer, method, vars);
  printer->Print(";\n\n\n");
}

void PrintSimpleImplementation(Printer *printer, const MethodDescriptor *method,
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

void PrintAdvancedImplementation(Printer *printer,
                                 const MethodDescriptor *method,
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

void PrintMethodImplementations(Printer *printer,
                                const MethodDescriptor *method) {
  map< ::grpc::string, ::grpc::string> vars = GetMethodVars(method);

  PrintProtoRpcDeclarationAsPragma(printer, method, vars);

  // TODO(jcanizales): Print documentation from the method.
  PrintSimpleSignature(printer, method, vars);
  PrintSimpleImplementation(printer, method, vars);

  printer->Print("// Returns a not-yet-started RPC object.\n");
  PrintAdvancedSignature(printer, method, vars);
  PrintAdvancedImplementation(printer, method, vars);
}

}  // namespace

::grpc::string GetHeader(const ServiceDescriptor *service) {
  ::grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    grpc::protobuf::io::StringOutputStream output_stream(&output);
    Printer printer(&output_stream, '$');

    map< ::grpc::string, ::grpc::string> vars = {{"service_class", ServiceClassName(service)}};

    printer.Print(vars, "@protocol $service_class$ <NSObject>\n\n");

    for (int i = 0; i < service->method_count(); i++) {
      PrintMethodDeclarations(&printer, service->method(i));
    }
    printer.Print("@end\n\n");

    printer.Print(
        "// Basic service implementation, over gRPC, that only does"
        " marshalling and parsing.\n");
    printer.Print(vars,
                  "@interface $service_class$ :"
                  " GRPCProtoService<$service_class$>\n");
    printer.Print(
        "- (instancetype)initWithHost:(NSString *)host"
        " NS_DESIGNATED_INITIALIZER;\n");
    printer.Print("+ (instancetype)serviceWithHost:(NSString *)host;\n");
    printer.Print("@end\n");
  }
  return output;
}

::grpc::string GetSource(const ServiceDescriptor *service) {
 ::grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    grpc::protobuf::io::StringOutputStream output_stream(&output);
    Printer printer(&output_stream, '$');

    map< ::grpc::string,::grpc::string> vars = {{"service_name", service->name()},
                                {"service_class", ServiceClassName(service)},
                                {"package", service->file()->package()}};

    printer.Print(vars,
                  "static NSString *const kPackageName = @\"$package$\";\n");
    printer.Print(
        vars, "static NSString *const kServiceName = @\"$service_name$\";\n\n");

    printer.Print(vars, "@implementation $service_class$\n\n");

    printer.Print("// Designated initializer\n");
    printer.Print("- (instancetype)initWithHost:(NSString *)host {\n");
    printer.Print(
        "  return (self = [super initWithHost:host"
        " packageName:kPackageName serviceName:kServiceName]);\n");
    printer.Print("}\n\n");
    printer.Print(
        "// Override superclass initializer to disallow different"
        " package and service names.\n");
    printer.Print("- (instancetype)initWithHost:(NSString *)host\n");
    printer.Print("                 packageName:(NSString *)packageName\n");
    printer.Print("                 serviceName:(NSString *)serviceName {\n");
    printer.Print("  return [self initWithHost:host];\n");
    printer.Print("}\n\n");
    printer.Print("+ (instancetype)serviceWithHost:(NSString *)host {\n");
    printer.Print("  return [[self alloc] initWithHost:host];\n");
    printer.Print("}\n\n\n");

    for (int i = 0; i < service->method_count(); i++) {
      PrintMethodImplementations(&printer, service->method(i));
    }

    printer.Print("@end\n");
  }
  return output;
}

}  // namespace grpc_objective_c_generator
