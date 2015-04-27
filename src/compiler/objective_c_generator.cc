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

#include "src/compiler/objective_c_generator.h"
#include "src/compiler/objective_c_generator_helpers.h"

#include "src/compiler/config.h"

#include <sstream>

namespace grpc_objective_c_generator {
namespace {

using ::grpc::protobuf::io::Printer;
using ::grpc::protobuf::MethodDescriptor;
using ::std::map;
using ::grpc::string;

void PrintProtoRpcDeclarationAsPragma(Printer *printer,
                                      const MethodDescriptor *method,
                                      map<string, string> vars) {
  vars["client_stream"] = method->client_streaming() ? "stream " : "";
  vars["server_stream"] = method->server_streaming() ? "stream " : "";

  printer->Print(vars,
      "#pragma mark $method_name$($client_stream$$request_type$)"
      " returns ($server_stream$$response_type$)\n\n");
}

void PrintMethodSignature(Printer *printer,
                          const MethodDescriptor *method,
                          const map<string, string>& vars) {
  // TODO(jcanizales): Print method comments.

  printer->Print(vars, "- ($return_type$)$method_name$With");
  if (method->client_streaming()) {
    printer->Print("RequestsWriter:(id<GRXWriter>)request");
  } else {
    printer->Print(vars, "Request:($prefix$$request_type$ *)request");
  }

  // TODO(jcanizales): Put this on a new line and align colons.
  printer->Print(" handler:(void(^)(");
  if (method->server_streaming()) {
    printer->Print("BOOL done, ");
  }
  printer->Print(vars,
      "$prefix$$response_type$ *response, NSError *error))handler");
}

void PrintSimpleSignature(Printer *printer,
                          const MethodDescriptor *method,
                          map<string, string> vars) {
  vars["method_name"] =
      grpc_generator::LowercaseFirstLetter(vars["method_name"]);
  vars["return_type"] = "void";
  PrintMethodSignature(printer, method, vars);
}

void PrintAdvancedSignature(Printer *printer,
                            const MethodDescriptor *method,
                            map<string, string> vars) {
  vars["method_name"] = "RPCTo" + vars["method_name"];
  vars["return_type"] = "ProtoRPC *";
  PrintMethodSignature(printer, method, vars);
}

void PrintMethodDeclarations(Printer *printer,
                             const MethodDescriptor *method,
                             map<string, string> vars) {
  vars["method_name"] = method->name();
  vars["request_type"] = method->input_type()->name();
  vars["response_type"] = method->output_type()->name();

  PrintProtoRpcDeclarationAsPragma(printer, method, vars);

  PrintSimpleSignature(printer, method, vars);
  printer->Print(";\n\n");
  PrintAdvancedSignature(printer, method, vars);
  printer->Print(";\n\n\n");
}

void PrintSourceMethodSimpleBlock(Printer *printer,
                                  const MethodDescriptor *method,
                                  map<string, string> vars) {
  vars["method_name"] = method->name();
  vars["request_type"] = method->input_type()->name();
  vars["response_type"] = method->output_type()->name();

  PrintSimpleSignature(printer, method, vars);

  printer->Print(" {\n");
  printer->Indent();
  printer->Print(vars, "return [[self $method_name$WithRequest:request] "
                 "connectHandler:^(id value, NSError *error) {\n");
  printer->Indent();
  printer->Print("handler(value, error);\n");
  printer->Outdent();
  printer->Print("}];\n");
  printer->Outdent();
  printer->Print("}\n");
}

void PrintSourceMethodAdvanced(Printer *printer,
                               const MethodDescriptor *method,
                               map<string, string> vars) {
  vars["method_name"] = method->name();
  vars["request_type"] = method->input_type()->name();
  vars["response_type"] = method->output_type()->name();

  PrintAdvancedSignature(printer, method, vars);

  printer->Print(" {\n");
  printer->Indent();
  printer->Print(vars, "return [self $method_name$WithRequest:request "
                 "client:[self newClient]];\n");
  printer->Outdent();
  printer->Print("}\n");
}

void PrintSourceMethodHandler(Printer *printer,
                              const MethodDescriptor *method,
                              std::map<grpc::string, grpc::string> *vars) {
  (*vars)["method_name"] = method->name();
  (*vars)["response_type"] = PrefixedName(method->output_type()->name());
  (*vars)["caps_name"] = grpc_generator::CapitalizeFirstLetter(method->name());

  printer->Print(*vars, "- (GRXSource *)$method_name$WithRequest:"
                 "(id<GRXSource>)request client:(PBgRPCClient *)client {\n");
  printer->Indent();
  printer->Print(*vars,
                 "return [self responseWithMethod:$@\"$caps_name\"\n");
  printer->Print(*vars,
                 "                          class:[$response_type$ class]\n");
  printer->Print("                        request:request\n");
  printer->Print("                         client:client];\n");
  printer->Outdent();
  printer->Print("}\n");
}

}

grpc::string GetHeader(const grpc::protobuf::ServiceDescriptor *service,
                       const string prefix) {
  grpc::string output;
  grpc::protobuf::io::StringOutputStream output_stream(&output);
  Printer printer(&output_stream, '$');
  
  printer.Print("@protocol GRXWriteable;\n");
  printer.Print("@protocol GRXWriter;\n\n");

  map<string, string> vars = {{"service_name", service->name()},
                              {"prefix",       prefix}};
  printer.Print(vars, "@protocol $prefix$$service_name$ <NSObject>\n\n");

  for (int i = 0; i < service->method_count(); i++) {
    PrintMethodDeclarations(&printer, service->method(i), vars);
  }
  printer.Print("@end\n\n");

  printer.Print("// Basic service implementation, over gRPC, that only does"
      " marshalling and parsing.\n");
  // use prefix
  printer.Print(vars, "@interface RMT$service_name$ :"
    " ProtoService<RMT$service_name$>\n");
  printer.Print("- (instancetype)initWithHost:(NSString *)host"
    " NS_DESIGNATED_INITIALIZER;\n");
  printer.Print("@end\n");
  return output;
}

grpc::string GetSource(const grpc::protobuf::ServiceDescriptor *service,
                       const string prefix) {
  grpc::string output;
  grpc::protobuf::io::StringOutputStream output_stream(&output);
  Printer printer(&output_stream, '$');

  map<string, string> vars = {{"service_name", service->name()},
                              {"prefix",       prefix}};
  printer.Print(vars, "#import \"$service_name$Stub.pb.h\"\n");
  printer.Print("#import \"PBGeneratedMessage+GRXSource.h\"\n\n");
  vars["full_name"] = service->full_name();
  printer.Print(vars,
                "static NSString *const kInterface = @\"$full_name$\";\n");
  printer.Print("@implementation $service_name$Stub\n\n");
  printer.Print("- (instancetype)initWithHost:(NSString *)host {\n");
  printer.Indent();
  printer.Print("if ((self = [super initWithHost:host "
                "interface:kInterface])) {\n");
  printer.Print("}\n");
  printer.Print("return self;\n");
  printer.Outdent();
  printer.Print("}\n\n");
  printer.Print("#pragma mark Simple block handlers.\n");
  for (int i = 0; i < service->method_count(); i++) {
    PrintSourceMethodSimpleBlock(&printer, service->method(i), vars);
  }
  printer.Print("\n");
  printer.Print("#pragma mark Advanced handlers.\n");
  for (int i = 0; i < service->method_count(); i++) {
    PrintSourceMethodAdvanced(&printer, service->method(i), vars);
  }
  printer.Print("\n");
  printer.Print("#pragma mark Handlers for subclasses "
                "(stub wrappers) to override.\n");
  for (int i = 0; i < service->method_count(); i++) {
    PrintSourceMethodHandler(&printer, service->method(i), &vars);
  }
  printer.Print("@end\n");
  return output;
}

} // namespace grpc_objective_c_generator
