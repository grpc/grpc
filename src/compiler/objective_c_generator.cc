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

void PrintSimpleBlockSignature(grpc::protobuf::io::Printer *printer,
                               const grpc::protobuf::MethodDescriptor *method,
                               std::map<grpc::string, grpc::string> *vars) {
  (*vars)["method_name"] = method->name();
  (*vars)["request_type"] = PrefixedName(method->input_type());
  (*vars)["response_type"] = PrefixedName(method->output_type());

  if (method->server_streaming()) {
    printer->Print("// When the response stream finishes, the handler is "
                   "called with nil for both arguments.\n\n");
  } else {
    printer->Print("// The handler is only called once.\n\n");
  }
  printer->Print(vars, "- (id<GRXLiveSource>)$method_name$WithRequest:"
                 "($request_type$)request completionHandler:(void(^)"
                 "($response_type$ *, NSError *))handler");
}

void PrintSimpleDelegateSignature(grpc::protobuf::io::Printer *printer,
                                  const grpc::protobuf::MethodDescriptor *method,
                                  std::map<grpc::string, grpc::string> *vars) {
  (*vars)["method_name"] = method->name();
  (*vars)["request_type"] = PrefixedName(method->input_type());

  printer->Print(vars, "- (id<GRXLiveSource>)$method_name$WithRequest:"
                 "($request_type$)request delegate:(id<GRXSink>)delegate");
}

void PrintAdvancedSignature(grpc::protobuf::io::Printer *printer,
                            const grpc::protobuf::MethodDescriptor *method,
                            std::map<grpc::string, grpc::string> *vars) {
  (*vars)["method_name"] = method->name();
  printer->Print(vars, "- (GRXSource *)$method_name$WithRequest:"
                 "(id<GRXSource>)request");
}

grpc::string GetHeader(const grpc::protobuf::ServiceDescriptor *service
                       const grpc::string message_header) {
  grpc::string output;
  grpc::protobuf::io::StringOutputStream output_stream(&output);
  grpc::protobuf::io::Printer printer(&output_stream, '$');
  std::map<grpc::string, grpc::string> vars;
  printer.Print("#import \"PBgRPCClient.h\"\n");
  printer.Print("#import \"PBStub.h\"\n");
  vars["message_header"] = message_header;
  printer.Print(&vars, "#import \"$message_header$\"\n\n");
  printer.Print("@protocol GRXSource\n");
  printer.Print("@class GRXSource\n\n");
  vars["service_name"] = service->name();
  printer.Print("@protocol $service_name$Stub <NSObject>\n\n");
  printer.Print("#pragma mark Simple block handlers\n\n");
  for (int i = 0; i < service->method_count(); i++) {
    PrintSimpleBlockSignature(&printer, service->method(i), &vars);
    printer.Print(";\n");
  }
  printer.Print("\n");
  printer.Print("#pragma mark Simple delegate handlers.\n\n");
  printer.Print("# TODO(jcanizales): Use high-level snippets to remove this duplication.");
  for (int i = 0; i < service->method_count(); i++) {
    PrintSimpleDelegateSignature(&printer, service->method(i), &vars);
    printer.Print(";\n");
  }
  printer.Print("\n");
  printer.Print("#pragma mark Advanced handlers.\n\n");
  for (int i = 0; i < service->method_count(); i++) {
    PrintAdvancedSignature(&printer, service->method(i), &vars);
    printer.Print(";\n");
  }
  printer.Print("\n");
  printer.Print("@end\n\n");
  printer.Print("// Basic stub that only does marshalling and parsing\n");
  printer.Print(&vars, "@interface $service_name$Stub :"
                " PBStub<$service_name$Stub>\n");
  printer.Print("- (instancetype)initWithHost:(NSString *)host;\n");
  printer.Print("@end\n");
  return output;
}

void PrintSourceMethodSimpleBlock(grpc::protobuf::io::Printer *printer,
                                  const grpc::protobuf::MethodDescriptor *method,
                                  std::map<grpc::string, grpc::string> *vars) {
  PrintSimpleBlockSignature(printer, method, vars);

  (*vars)["method_name"] = method->name();
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

void PrintSourceMethodSimpleDelegate(grpc::protobuf::io::Printer *printer,
                                     const grpc::protobuf::MethodDescriptor *method,
                                     std::map<grpc::string, grpc::string> *vars) {
  PrintSimpleDelegateSignature(printer, method, vars);

  (*vars)["method_name"] = method->name();
  printer->Print(" {\n");
  printer->Indent();
  printer->Print(vars, "return [[self $method_name$WithRequest:request]"
                 "connectToSink:delegate];\n");
  printer->Outdent();
  printer->Print("}\n");
}

void PrintSourceMethodAdvanced(grpc::protobuf::io::Printer *printer,
                               const grpc::protobuf::MethodDescriptor *method,
                               std::map<grpc::string, grpc::string> *vars) {
  PrintAdvancedSignature(printer, method, vars);

  (*vars)["method_name"] = method->name();
  printer->Print(" {\n");
  printer->Indent();
  printer->Print(vars, "return [self $method_name$WithRequest:request "
                 "client:[self newClient]];\n");
  printer->Outdent();
  printer->Print("}\n");
}

void PrintSourceMethodHandler(grpc::protobuf::io::Printer *printer,
                              const grpc::protobuf::MethodDescriptor *method,
                              std::map<grpc::string, grpc::string> *vars) {
  (*vars)["method_name"] = method->name();
  (*vars)["response_type"] = PrefixedName(method->output_type());
  (*vars)["caps_name"] = grpc_generator::CapitalizeFirstLetter(method->name());

  printer->Print(vars, "- (GRXSource *)$method_name$WithRequest:"
                 "(id<GRXSource>)request client:(PBgRPCClient *)client {\n");
  printer->Indent();
  printer->Print(vars,
                 "return [self responseWithMethod:$@\"$caps_name\"\n");
  printer->Print(vars,
                 "                          class:[$response_type$ class]\n");
  printer->Print("                        request:request\n");
  printer->Print("                         client:client];\n");
  printer->Outdent();
  printer->Print("}\n");
}

grpc::string GetSource(const grpc::protobuf::ServiceDescriptor *service) {
  grpc::string output;
  grpc::protobuf::io::StringOutputStream output_stream(&output);
  grpc::protobuf::io::Printer printer(&output_stream, '$');
  std::map<grpc::string, grpc::string> vars;
  vars["service_name"] = service->name();
  printer.Print(&vars, "#import \"$service_name$Stub.pb.h\"\n");
  printer.Print("#import \"PBGeneratedMessage+GRXSource.h\"\n\n");
  vars["full_name"] = service->full_name();
  printer.Print(&vars,
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
    PrintSourceMethodSimpleBlock(&printer, service->method(i), &vars);
  }
  printer.Print("\n");
  printer.Print("#pragma mark Simple delegate handlers.\n");
  for (int i = 0; i < service->method_count(); i++) {
    PrintSourceMethodSimpleDelegate(&printer, service->method(i), &vars);
  }
  printer.Print("\n");
  printer.Print("#pragma mark Advanced handlers.\n");
  for (int i = 0; i < service->method_count(); i++) {
    PrintSourceMethodAdvanced(&printer, service->method(i), &vars);
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
