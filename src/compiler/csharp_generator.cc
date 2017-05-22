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

#include <cctype>
#include <map>
#include <sstream>
#include <vector>

#include "src/compiler/config.h"
#include "src/compiler/csharp_generator.h"
#include "src/compiler/csharp_generator_helpers.h"

using google::protobuf::compiler::csharp::GetFileNamespace;
using google::protobuf::compiler::csharp::GetClassName;
using google::protobuf::compiler::csharp::GetReflectionClassName;
using grpc::protobuf::FileDescriptor;
using grpc::protobuf::Descriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::io::Printer;
using grpc_generator::Printer;
using grpc_generator::File;
using grpc_generator::Method;
using grpc_generator::MethodType;
using grpc_generator::GetMethodType;
using grpc_generator::METHODTYPE_NO_STREAMING;
using grpc_generator::METHODTYPE_CLIENT_STREAMING;
using grpc_generator::METHODTYPE_SERVER_STREAMING;
using grpc_generator::METHODTYPE_BIDI_STREAMING;
using grpc_generator::StringReplace;
using std::map;
using std::vector;

namespace grpc_csharp_generator {
namespace {
static const std::string COMMENT_PREFIX = "//";
// This function is a massaged version of
// https://github.com/google/protobuf/blob/master/src/google/protobuf/compiler/csharp/csharp_doc_comment.cc
// Currently, we cannot easily reuse the functionality as
// google/protobuf/compiler/csharp/csharp_doc_comment.h is not a public header.
// TODO(jtattermusch): reuse the functionality from google/protobuf.
bool GenerateDocCommentBody(grpc_generator::Printer *printer,
                            const grpc_generator::CommentHolder *holder) {
  grpc::string comments = holder->GetLeadingComments(COMMENT_PREFIX).empty()
    ? holder->GetTrailingComments(COMMENT_PREFIX)
    : holder->GetLeadingComments(COMMENT_PREFIX);
  if (comments.empty()) {
    return false;
  }
  // XML escaping... no need for apostrophes etc as the whole text is going to
  // be a child
  // node of a summary element, not part of an attribute.
  comments = grpc_generator::StringReplace(comments, "&", "&amp;", true);
  comments = grpc_generator::StringReplace(comments, "<", "&lt;", true);

  std::vector<grpc::string> lines;
  grpc_generator::Split(comments, '\n', &lines);
  // TODO: We really should work out which part to put in the summary and which
  // to put in the remarks...
  // but that needs to be part of a bigger effort to understand the markdown
  // better anyway.
  printer->Print("/// <summary>\n");
  bool last_was_empty = false;
  // We squash multiple blank lines down to one, and remove any trailing blank
  // lines. We need
  // to preserve the blank lines themselves, as this is relevant in the
  // markdown.
  // Note that we can't remove leading or trailing whitespace as *that's*
  // relevant in markdown too.
  // (We don't skip "just whitespace" lines, either.)
  for (std::vector<grpc::string>::iterator it = lines.begin();
       it != lines.end(); ++it) {
    grpc::string line = *it;
    if (line.empty()) {
      last_was_empty = true;
    } else {
      if (last_was_empty) {
        printer->Print("///\n");
      }
      last_was_empty = false;
      std::map<grpc::string, grpc::string> vars;
      vars["line"] = *it;
      printer->Print(vars, "///$line$\n");
    }
  }
  printer->Print("/// </summary>\n");
  return true;
}

void GenerateDocCommentServerMethod(grpc_generator::Printer *printer,
                                    const grpc_generator::Method *method) {
  if (GenerateDocCommentBody(printer, method)) {
    if (method->ClientStreaming()) {
      printer->Print(
          "/// <param name=\"requestStream\">Used for reading requests from "
          "the client.</param>\n");
    } else {
      printer->Print(
          "/// <param name=\"request\">The request received from the "
          "client.</param>\n");
    }
    if (method->ServerStreaming()) {
      printer->Print(
          "/// <param name=\"responseStream\">Used for sending responses back "
          "to the client.</param>\n");
    }
    printer->Print(
        "/// <param name=\"context\">The context of the server-side call "
        "handler being invoked.</param>\n");
    if (method->ServerStreaming()) {
      printer->Print(
          "/// <returns>A task indicating completion of the "
          "handler.</returns>\n");
    } else {
      printer->Print(
          "/// <returns>The response to send back to the client (wrapped by a "
          "task).</returns>\n");
    }
  }
}

void GenerateDocCommentClientMethod(grpc_generator::Printer *printer,
                                    const grpc_generator::Method *method,
                                    bool is_sync, bool use_call_options) {
  if (GenerateDocCommentBody(printer, method)) {
    if (!method->ClientStreaming()) {
      printer->Print(
          "/// <param name=\"request\">The request to send to the "
          "server.</param>\n");
    }
    if (!use_call_options) {
      printer->Print(
          "/// <param name=\"headers\">The initial metadata to send with the "
          "call. This parameter is optional.</param>\n");
      printer->Print(
          "/// <param name=\"deadline\">An optional deadline for the call. The "
          "call will be cancelled if deadline is hit.</param>\n");
      printer->Print(
          "/// <param name=\"cancellationToken\">An optional token for "
          "canceling the call.</param>\n");
    } else {
      printer->Print(
          "/// <param name=\"options\">The options for the call.</param>\n");
    }
    if (is_sync) {
      printer->Print(
          "/// <returns>The response received from the server.</returns>\n");
    } else {
      printer->Print("/// <returns>The call object.</returns>\n");
    }
  }
}

std::string GetServiceClassName(const grpc_generator::Service *service) {
  return service->name();
}

std::string GetClientClassName(const grpc_generator::Service *service) {
  return service->name() + "Client";
}

std::string GetServerClassName(const grpc_generator::Service *service) {
  return service->name() + "Base";
}

std::string GetCSharpMethodType(MethodType method_type) {
  switch (method_type) {
    case METHODTYPE_NO_STREAMING:
      return "grpc::MethodType.Unary";
    case METHODTYPE_CLIENT_STREAMING:
      return "grpc::MethodType.ClientStreaming";
    case METHODTYPE_SERVER_STREAMING:
      return "grpc::MethodType.ServerStreaming";
    case METHODTYPE_BIDI_STREAMING:
      return "grpc::MethodType.DuplexStreaming";
  }
  GOOGLE_LOG(FATAL) << "Can't get here.";
  return "";
}

std::string GetServiceNameFieldName() { return "__ServiceName"; }

std::string GetMarshallerFieldName(const grpc::string message) {
  return "__Marshaller_" + message;
}

std::string GetMethodFieldName(const grpc_generator::Method *method) {
  return "__Method_" + method->name();
}

std::string GetMethodRequestParamMaybe(const grpc_generator::Method *method,
                                       bool invocation_param = false) {
  if (method->ClientStreaming()) {
    return "";
  }
  if (invocation_param) {
    return "request, ";
  }
  return method->input_type_name() + " request, ";
}

std::string GetAccessLevel(bool internal_access) {
  return internal_access ? "internal" : "public";
}

std::string GetMethodReturnTypeClient(const grpc_generator::Method *method) {
  std::string input_type = method->input_type_name();
  std::string output_type = method->output_type_name();
  switch (GetMethodType(method)) {
    case METHODTYPE_NO_STREAMING:
      return "grpc::AsyncUnaryCall<" + output_type + ">";
    case METHODTYPE_CLIENT_STREAMING:
      return "grpc::AsyncClientStreamingCall<" +
             input_type + ", " + output_type + ">";
    case METHODTYPE_SERVER_STREAMING:
      return "grpc::AsyncServerStreamingCall<" + output_type + ">";
    case METHODTYPE_BIDI_STREAMING:
      return "grpc::AsyncDuplexStreamingCall<" +
             input_type + ", " + output_type + ">";
  }
  GOOGLE_LOG(FATAL) << "Can't get here.";
  return "";
}

std::string GetMethodRequestParamServer(const grpc_generator::Method *method) {
  switch (GetMethodType(method)) {
    case METHODTYPE_NO_STREAMING:
    case METHODTYPE_SERVER_STREAMING:
      return method->input_type_name() + " request";
    case METHODTYPE_CLIENT_STREAMING:
    case METHODTYPE_BIDI_STREAMING:
      return "grpc::IAsyncStreamReader<" + method->input_type_name() +
             "> requestStream";
  }
  GOOGLE_LOG(FATAL) << "Can't get here.";
  return "";
}

std::string GetMethodReturnTypeServer(const grpc_generator::Method *method) {
  switch (GetMethodType(method)) {
    case METHODTYPE_NO_STREAMING:
    case METHODTYPE_CLIENT_STREAMING:
      return "global::System.Threading.Tasks.Task<" +
             method->output_type_name() + ">";
    case METHODTYPE_SERVER_STREAMING:
    case METHODTYPE_BIDI_STREAMING:
      return "global::System.Threading.Tasks.Task";
  }
  GOOGLE_LOG(FATAL) << "Can't get here.";
  return "";
}

std::string GetMethodResponseStreamMaybe(const grpc_generator::Method *method) {
  switch (GetMethodType(method)) {
    case METHODTYPE_NO_STREAMING:
    case METHODTYPE_CLIENT_STREAMING:
      return "";
    case METHODTYPE_SERVER_STREAMING:
    case METHODTYPE_BIDI_STREAMING:
      return ", grpc::IServerStreamWriter<" +
             method->output_type_name() + "> responseStream";
  }
  GOOGLE_LOG(FATAL) << "Can't get here.";
  return "";
}

// Gets vector of all messages used as input or output types.
std::vector<grpc::string> GetUsedMessages(
      const grpc_generator::Service *service) {
  std::set<grpc::string> descriptor_set;
  std::vector<grpc::string> result;  // vector is to maintain stable ordering
  for (int i = 0; i < service->method_count(); i++) {
    const grpc_generator::Method *method = service->method(i).get();
    if (descriptor_set.find(method->input_type_name()) == descriptor_set.end()) {
      descriptor_set.insert(method->input_type_name());
      result.push_back(method->input_type_name());
    }
    if (descriptor_set.find(method->output_type_name()) == descriptor_set.end()) {
      descriptor_set.insert(method->output_type_name());
      result.push_back(method->output_type_name());
    }
  }
  return result;
}


void GenerateMarshallerFields(Printer *out, const grpc_generator::Service *service) {
  std::vector<grpc::string> used_messages = GetUsedMessages(service);
  for (size_t i = 0; i < used_messages.size(); i++) {
    grpc::string message = used_messages[i];
    std::map<grpc::string, grpc::string> vars;
    vars["fieldname"] = GetMarshallerFieldName(message);
    vars["type"] = message;
    out->Print(
      vars,
      "static readonly grpc::Marshaller<$type$> $fieldname$ = "
      "grpc::Marshallers.Create((arg) => "
      "global::Google.Protobuf.MessageExtensions.ToByteArray(arg), " //ERR ref to pb
      "$type$.Parser.ParseFrom);\n"); 
  }
  out->Print("\n");
}

void GenerateStaticMethodField(Printer *out, const grpc_generator::Method *method) {

  std::map<grpc::string, grpc::string> vars;
  vars["fieldname"] = GetMethodFieldName(method);
  vars["request"] = method->input_type_name();
  vars["response"] = method->output_type_name();
  out->Print(
    vars,
      "static readonly grpc::Method<$request$, $response$> $fieldname$ = new "
      "grpc::Method<$request$, $response$>(\n");
  out->Indent();
  out->Indent();
  vars.clear();
  vars["methodtype"] = GetCSharpMethodType(GetMethodType(method));
  vars["servicenamefield"] = GetServiceNameFieldName();
  vars["methodname"] = method->name();
  vars["requestmarshaller"] = GetMarshallerFieldName(method->input_type_name());
  vars["output_type"] = GetMarshallerFieldName(method->output_type_name());

  out->Print(vars, "$methodtype$,\n");
  out->Print(vars, "$servicenamefield$,\n");
  out->Print(vars, "\"$methodname$\",\n");
  out->Print(vars, "$requestmarshaller$,\n");
  out->Print(vars, "$responsemarshaller$);\n");
  out->Print("\n");
  out->Outdent();
  out->Outdent();
}


void GenerateServiceDescriptorProperty(Printer *out,
                                       const grpc_generator::Service *service) {
  std::ostringstream index;
  index << -1; //ERR service->index();
  out->Print("/// <summary>Service descriptor</summary>\n");
  out->Print(
      "public static global::Google.Protobuf.Reflection.ServiceDescriptor "//ERR ref to pb
      "Descriptor\n");
  out->Print("{\n");
  std::map<grpc::string, grpc::string> vars;
  vars["umbrella"] = service->name();
  vars["index"] = index.str();
  out->Print(vars, "  get { return $umbrella$.Descriptor.Services[$index$]; }\n");
  out->Print("}\n");
  out->Print("\n");
}

void GenerateServerClass(Printer *out, const grpc_generator::Service *service) {
  std::map<grpc::string, grpc::string> vars;
  vars["servicename"] = GetServiceClassName(service);
  out->Print(
    vars, 
      "/// <summary>Base class for server-side implementations of "
      "$servicename$</summary>\n");
  out->Print(vars, "public abstract partial class $servicename$\n");
  out->Print("{\n");
  out->Indent();
  for (int i = 0; i < service->method_count(); i++) {
    const grpc_generator::Method *method = service->method(i).get();
    GenerateDocCommentServerMethod(out, method);
  vars["methodname"] = method->name();
  vars["returntype"] = GetMethodReturnTypeServer(method);
  vars["request"] = GetMethodRequestParamServer(method);
  vars["response_stream_maybe"] = GetMethodResponseStreamMaybe(method);
    out->Print(
    vars,
        "public virtual $returntype$ "
        "$methodname$($request$$response_stream_maybe$, "
        "grpc::ServerCallContext context)\n");
    out->Print("{\n");
    out->Indent();
    out->Print(
        "throw new grpc::RpcException("
        "new grpc::Status(grpc::StatusCode.Unimplemented, \"\"));\n");
    out->Outdent();
    out->Print("}\n\n");
  }
  out->Outdent();
  out->Print("}\n");
  out->Print("\n");
}

void GenerateClientStub(Printer *out, const grpc_generator::Service *service) {
  std::map<grpc::string, grpc::string> vars;
  vars["servicename"] = GetServiceClassName(service);
  vars["name"] = GetClientClassName(service);

  out->Print(vars, "/// <summary>Client for $servicename$</summary>\n");
  out->Print(vars, "public partial class $name$ : grpc::ClientBase<$name$>\n");
  out->Print("{\n");
  out->Indent();

  // constructors
  out->Print(
    vars,
      "/// <summary>Creates a new client for $servicename$</summary>\n"
      "/// <param name=\"channel\">The channel to use to make remote "
      "calls.</param>\n");
  out->Print(vars, "public $name$(grpc::Channel channel) : base(channel)\n");
  out->Print("{\n");
  out->Print("}\n");
  out->Print(
      vars,
      "/// <summary>Creates a new client for $servicename$ that uses a custom "
      "<c>CallInvoker</c>.</summary>\n"
      "/// <param name=\"callInvoker\">The callInvoker to use to make remote "
      "calls.</param>\n");
  out->Print(
      vars, 
      "public $name$(grpc::CallInvoker callInvoker) : base(callInvoker)\n");
  out->Print("{\n");
  out->Print("}\n");
  out->Print(
      "/// <summary>Protected parameterless constructor to allow creation"
      " of test doubles.</summary>\n");
  out->Print(vars, "protected $name$() : base()\n");
  out->Print("{\n");
  out->Print("}\n");
  out->Print(
      "/// <summary>Protected constructor to allow creation of configured "
      "clients.</summary>\n"
      "/// <param name=\"configuration\">The client configuration.</param>\n");
  out->Print(
    vars,
    "protected $name$(ClientBaseConfiguration configuration)"
    " : base(configuration)\n");
  out->Print("{\n");
  out->Print("}\n\n");

  for (int i = 0; i < service->method_count(); i++) {
    const grpc_generator::Method *method = service->method(i).get();
    MethodType method_type = GetMethodType(method);

    if (method_type == METHODTYPE_NO_STREAMING) {
      // unary calls have an extra synchronous stub method
      GenerateDocCommentClientMethod(out, method, true, false);
      vars["methodname"] = method->name();
      vars["request"] = method->input_type_name();
      vars["response"] = method->output_type_name();
      vars["methodfield"] = GetMethodFieldName(method);
      out->Print(
          vars,
          "public virtual $response$ $methodname$($request$ request, "
          "grpc::Metadata "
          "headers = null, DateTime? deadline = null, CancellationToken "
          "cancellationToken = default(CancellationToken))\n");
      out->Print("{\n");
      out->Indent();
      out->Print(
          vars,
          "return $methodname$(request, new grpc::CallOptions(headers, "
          "deadline, "
          "cancellationToken));\n");
      out->Outdent();
      out->Print("}\n");

      // overload taking CallOptions as a param
      GenerateDocCommentClientMethod(out, method, true, true);
      out->Print(
          vars,
          "public virtual $response$ $methodname$($request$ request, "
          "grpc::CallOptions options)\n");
      out->Print("{\n");
      out->Indent();
      out->Print(
          vars,
          "return CallInvoker.BlockingUnaryCall($methodfield$, null, options, "
          "request);\n");
      out->Outdent();
      out->Print("}\n");
    }

    std::string method_name = method->name();
    if (method_type == METHODTYPE_NO_STREAMING) {
      method_name += "Async";  // prevent name clash with synchronous method.
    }
    vars["methodname"] = method_name;
    vars["request_maybe"] = GetMethodRequestParamMaybe(method);
    vars["returntype"] = GetMethodReturnTypeClient(method);

    GenerateDocCommentClientMethod(out, method, false, false);
    out->Print(
        vars,
        "public virtual $returntype$ "
        "$methodname$($request_maybe$grpc::Metadata "
        "headers = null, DateTime? deadline = null, CancellationToken "
        "cancellationToken = default(CancellationToken))\n");
    out->Print("{\n");
    out->Indent();

    vars["request_maybe"] = GetMethodRequestParamMaybe(method, true);
    out->Print(
        vars,
        "return $methodname$($request_maybe$new grpc::CallOptions(headers, "
        "deadline, "
        "cancellationToken));\n");
    out->Outdent();
    out->Print("}\n");

    // overload taking CallOptions as a param
    GenerateDocCommentClientMethod(out, method, false, true);
    vars["request_maybe"] = GetMethodRequestParamMaybe(method);
    out->Print(
        vars,
        "public virtual $returntype$ "
        "$methodname$($request_maybe$grpc::CallOptions "
        "options)\n");
    out->Print("{\n");
    out->Indent();
    vars["methodfield"] = GetMethodFieldName(method);
    switch (GetMethodType(method)) {
      case METHODTYPE_NO_STREAMING:
        out->Print(
            vars,
            "return CallInvoker.AsyncUnaryCall($methodfield$, null, options, "
            "request);\n");
        break;
      case METHODTYPE_CLIENT_STREAMING:
        out->Print(
            vars,
            "return CallInvoker.AsyncClientStreamingCall($methodfield$, null, "
            "options);\n");
        break;
      case METHODTYPE_SERVER_STREAMING:
        out->Print(
            vars,
            "return CallInvoker.AsyncServerStreamingCall($methodfield$, null, "
            "options, request);\n");
        break;
      case METHODTYPE_BIDI_STREAMING:
        out->Print(
            vars,
            "return CallInvoker.AsyncDuplexStreamingCall($methodfield$, null, "
            "options);\n");
        break;
      default:
        GOOGLE_LOG(FATAL) << "Can't get here.";
    }
    out->Outdent();
    out->Print("}\n");
  }
  vars.clear();
  vars["name"] = GetClientClassName(service);

  // override NewInstance method
  out->Print(
      "/// <summary>Creates a new instance of client from given "
      "<c>ClientBaseConfiguration</c>.</summary>\n");
  out->Print(
      vars, 
      "protected override $name$ NewInstance(ClientBaseConfiguration "
      "configuration)\n");
  out->Print("{\n");
  out->Indent();
  out->Print(vars,"return new $name$(configuration);\n");
  out->Outdent();
  out->Print("}\n");

  out->Outdent();
  out->Print("}\n");
  out->Print("\n");
}

void GenerateBindServiceMethod(Printer *out, const grpc_generator::Service *service) {
  std::map<grpc::string, grpc::string> vars;
  vars["implclass"] = GetServerClassName(service);
  out->Print(
      "/// <summary>Creates service definition that can be registered with a "
      "server</summary>\n");
  out->Print(
      "/// <param name=\"serviceImpl\">An object implementing the server-side"
      " handling logic.</param>\n");
  out->Print(
      vars,
      "public static grpc::ServerServiceDefinition BindService($implclass$ "
      "serviceImpl)\n");
  out->Print("{\n");
  out->Indent();

  out->Print("return grpc::ServerServiceDefinition.CreateBuilder()\n");
  out->Indent();
  out->Indent();
  for (int i = 0; i < service->method_count(); i++) {
    const grpc_generator::Method *method = service->method(i).get();
    vars["methodname"] = method->name();
    out->Print(vars, ".AddMethod($methodfield$, serviceImpl.$methodname$)");
    if (i == service->method_count() - 1) {
      out->Print(".Build();");
    }
    out->Print("\n");
  }
  out->Outdent();
  out->Outdent();

  out->Outdent();
  out->Print("}\n");
  out->Print("\n");
}

void GenerateService(Printer *out, const grpc_generator::Service *service,
                     bool generate_client, bool generate_server,
                     bool internal_access) {
  GenerateDocCommentBody(out, service);
  std::map<grpc::string, grpc::string> vars;
  vars["access_level"] = GetAccessLevel(internal_access);
  vars["classname"] = GetServiceClassName(service);
  vars["servicenamefield"] = GetServiceNameFieldName();
  vars["servicename"] = service->name();
  out->Print(vars, "$access_level$ static partial class $classname$\n");
  out->Print("{\n");
  out->Indent();
  out->Print(vars,"static readonly string $servicenamefield$ = \"$servicename$\";\n");
  out->Print("\n");

  GenerateMarshallerFields(out, service);
  for (int i = 0; i < service->method_count(); i++) {
    GenerateStaticMethodField(out, service->method(i).get());
  }
  GenerateServiceDescriptorProperty(out, service);

  if (generate_server) {
    GenerateServerClass(out, service);
  }
  if (generate_client) {
    GenerateClientStub(out, service);
  }
  if (generate_server) {
    GenerateBindServiceMethod(out, service);
  }

  out->Outdent();
  out->Print("}\n");
}

}  // anonymous namespace

grpc::string GetServices(const grpc_generator::File *file, bool generate_client,
                         bool generate_server, bool internal_access) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    grpc_generator::Printer *printer = file->CreatePrinter(&output).get();

    // Don't write out any output if there no services, to avoid empty service
    // files being generated for proto files that don't declare any.
    if (file->service_count() == 0) {
      return output;
    }

    std::map<grpc::string, grpc::string> vars;
    vars["filename"] = file->filename();

    // Write out a file header.
    printer->Print("// Generated by the grpc compiler.  DO NOT EDIT!\n");
    printer->Print(vars, "// source: $filename$\n");

    // use C++ style as there are no file-level XML comments in .NET
    grpc::string leading_comments = GetCsharpComments(file, true);
    if (!leading_comments.empty()) {
      printer->Print("// Original file comments:\n");
      printer->Print(leading_comments.c_str());
    }

    printer->Print("#region Designer generated code\n");
    printer->Print("\n");
    printer->Print("using System;\n");
    printer->Print("using System.Threading;\n");
    printer->Print("using System.Threading.Tasks;\n");
    printer->Print("using grpc = global::Grpc.Core;\n");
    printer->Print("\n");

    vars["namespace"] = file->package();
    printer->Print(vars,"namespace $namespace$ {\n");
    printer->Indent();
    for (int i = 0; i < file->service_count(); i++) {
      GenerateService(printer, file->service(i).get(), generate_client, generate_server,
                      internal_access);
    }
    printer->Outdent();
    printer->Print("}\n");
    printer->Print("#endregion\n");
  }
  return output;
}

}  // namespace grpc_csharp_generator
