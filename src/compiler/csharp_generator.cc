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

#include <cctype>
#include <map>
#include <sstream>
#include <vector>

#include "src/compiler/config.h"
#include "src/compiler/csharp_generator.h"
#include "src/compiler/csharp_generator_helpers.h"

using google::protobuf::compiler::csharp::GetClassName;
using google::protobuf::compiler::csharp::GetFileNamespace;
using google::protobuf::compiler::csharp::GetReflectionClassName;
using grpc::protobuf::Descriptor;
using grpc::protobuf::FileDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::io::Printer;
using grpc::protobuf::io::StringOutputStream;
using grpc_generator::GetMethodType;
using grpc_generator::METHODTYPE_BIDI_STREAMING;
using grpc_generator::METHODTYPE_CLIENT_STREAMING;
using grpc_generator::METHODTYPE_NO_STREAMING;
using grpc_generator::METHODTYPE_SERVER_STREAMING;
using grpc_generator::MethodType;
using grpc_generator::StringReplace;
using std::map;
using std::vector;

namespace grpc_csharp_generator {
namespace {

// This function is a massaged version of
// https://github.com/google/protobuf/blob/master/src/google/protobuf/compiler/csharp/csharp_doc_comment.cc
// Currently, we cannot easily reuse the functionality as
// google/protobuf/compiler/csharp/csharp_doc_comment.h is not a public header.
// TODO(jtattermusch): reuse the functionality from google/protobuf.
bool GenerateDocCommentBodyImpl(grpc::protobuf::io::Printer* printer,
                                grpc::protobuf::SourceLocation location) {
  grpc::string comments = location.leading_comments.empty()
                              ? location.trailing_comments
                              : location.leading_comments;
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
      printer->Print("///$line$\n", "line", *it);
    }
  }
  printer->Print("/// </summary>\n");
  return true;
}

template <typename DescriptorType>
bool GenerateDocCommentBody(grpc::protobuf::io::Printer* printer,
                            const DescriptorType* descriptor) {
  grpc::protobuf::SourceLocation location;
  if (!descriptor->GetSourceLocation(&location)) {
    return false;
  }
  return GenerateDocCommentBodyImpl(printer, location);
}

void GenerateDocCommentServerMethod(grpc::protobuf::io::Printer* printer,
                                    const MethodDescriptor* method) {
  if (GenerateDocCommentBody(printer, method)) {
    if (method->client_streaming()) {
      printer->Print(
          "/// <param name=\"requestStream\">Used for reading requests from "
          "the client.</param>\n");
    } else {
      printer->Print(
          "/// <param name=\"request\">The request received from the "
          "client.</param>\n");
    }
    if (method->server_streaming()) {
      printer->Print(
          "/// <param name=\"responseStream\">Used for sending responses back "
          "to the client.</param>\n");
    }
    printer->Print(
        "/// <param name=\"context\">The context of the server-side call "
        "handler being invoked.</param>\n");
    if (method->server_streaming()) {
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

void GenerateDocCommentClientMethod(grpc::protobuf::io::Printer* printer,
                                    const MethodDescriptor* method,
                                    bool is_sync, bool use_call_options) {
  if (GenerateDocCommentBody(printer, method)) {
    if (!method->client_streaming()) {
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

std::string GetServiceClassName(const ServiceDescriptor* service) {
  return service->name();
}

std::string GetClientClassName(const ServiceDescriptor* service) {
  return service->name() + "Client";
}

std::string GetServerClassName(const ServiceDescriptor* service) {
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

std::string GetMarshallerFieldName(const Descriptor* message) {
  return "__Marshaller_" + message->name();
}

std::string GetMethodFieldName(const MethodDescriptor* method) {
  return "__Method_" + method->name();
}

std::string GetMethodRequestParamMaybe(const MethodDescriptor* method,
                                       bool invocation_param = false) {
  if (method->client_streaming()) {
    return "";
  }
  if (invocation_param) {
    return "request, ";
  }
  return GetClassName(method->input_type()) + " request, ";
}

std::string GetAccessLevel(bool internal_access) {
  return internal_access ? "internal" : "public";
}

std::string GetMethodReturnTypeClient(const MethodDescriptor* method) {
  switch (GetMethodType(method)) {
    case METHODTYPE_NO_STREAMING:
      return "grpc::AsyncUnaryCall<" + GetClassName(method->output_type()) +
             ">";
    case METHODTYPE_CLIENT_STREAMING:
      return "grpc::AsyncClientStreamingCall<" +
             GetClassName(method->input_type()) + ", " +
             GetClassName(method->output_type()) + ">";
    case METHODTYPE_SERVER_STREAMING:
      return "grpc::AsyncServerStreamingCall<" +
             GetClassName(method->output_type()) + ">";
    case METHODTYPE_BIDI_STREAMING:
      return "grpc::AsyncDuplexStreamingCall<" +
             GetClassName(method->input_type()) + ", " +
             GetClassName(method->output_type()) + ">";
  }
  GOOGLE_LOG(FATAL) << "Can't get here.";
  return "";
}

std::string GetMethodRequestParamServer(const MethodDescriptor* method) {
  switch (GetMethodType(method)) {
    case METHODTYPE_NO_STREAMING:
    case METHODTYPE_SERVER_STREAMING:
      return GetClassName(method->input_type()) + " request";
    case METHODTYPE_CLIENT_STREAMING:
    case METHODTYPE_BIDI_STREAMING:
      return "grpc::IAsyncStreamReader<" + GetClassName(method->input_type()) +
             "> requestStream";
  }
  GOOGLE_LOG(FATAL) << "Can't get here.";
  return "";
}

std::string GetMethodReturnTypeServer(const MethodDescriptor* method) {
  switch (GetMethodType(method)) {
    case METHODTYPE_NO_STREAMING:
    case METHODTYPE_CLIENT_STREAMING:
      return "global::System.Threading.Tasks.Task<" +
             GetClassName(method->output_type()) + ">";
    case METHODTYPE_SERVER_STREAMING:
    case METHODTYPE_BIDI_STREAMING:
      return "global::System.Threading.Tasks.Task";
  }
  GOOGLE_LOG(FATAL) << "Can't get here.";
  return "";
}

std::string GetMethodResponseStreamMaybe(const MethodDescriptor* method) {
  switch (GetMethodType(method)) {
    case METHODTYPE_NO_STREAMING:
    case METHODTYPE_CLIENT_STREAMING:
      return "";
    case METHODTYPE_SERVER_STREAMING:
    case METHODTYPE_BIDI_STREAMING:
      return ", grpc::IServerStreamWriter<" +
             GetClassName(method->output_type()) + "> responseStream";
  }
  GOOGLE_LOG(FATAL) << "Can't get here.";
  return "";
}

// Gets vector of all messages used as input or output types.
std::vector<const Descriptor*> GetUsedMessages(
    const ServiceDescriptor* service) {
  std::set<const Descriptor*> descriptor_set;
  std::vector<const Descriptor*>
      result;  // vector is to maintain stable ordering
  for (int i = 0; i < service->method_count(); i++) {
    const MethodDescriptor* method = service->method(i);
    if (descriptor_set.find(method->input_type()) == descriptor_set.end()) {
      descriptor_set.insert(method->input_type());
      result.push_back(method->input_type());
    }
    if (descriptor_set.find(method->output_type()) == descriptor_set.end()) {
      descriptor_set.insert(method->output_type());
      result.push_back(method->output_type());
    }
  }
  return result;
}

void GenerateMarshallerFields(Printer* out, const ServiceDescriptor* service) {
  std::vector<const Descriptor*> used_messages = GetUsedMessages(service);
  for (size_t i = 0; i < used_messages.size(); i++) {
    const Descriptor* message = used_messages[i];
    out->Print(
        "static readonly grpc::Marshaller<$type$> $fieldname$ = "
        "grpc::Marshallers.Create((arg) => "
        "global::Google.Protobuf.MessageExtensions.ToByteArray(arg), "
        "$type$.Parser.ParseFrom);\n",
        "fieldname", GetMarshallerFieldName(message), "type",
        GetClassName(message));
  }
  out->Print("\n");
}

void GenerateStaticMethodField(Printer* out, const MethodDescriptor* method) {
  out->Print(
      "static readonly grpc::Method<$request$, $response$> $fieldname$ = new "
      "grpc::Method<$request$, $response$>(\n",
      "fieldname", GetMethodFieldName(method), "request",
      GetClassName(method->input_type()), "response",
      GetClassName(method->output_type()));
  out->Indent();
  out->Indent();
  out->Print("$methodtype$,\n", "methodtype",
             GetCSharpMethodType(GetMethodType(method)));
  out->Print("$servicenamefield$,\n", "servicenamefield",
             GetServiceNameFieldName());
  out->Print("\"$methodname$\",\n", "methodname", method->name());
  out->Print("$requestmarshaller$,\n", "requestmarshaller",
             GetMarshallerFieldName(method->input_type()));
  out->Print("$responsemarshaller$);\n", "responsemarshaller",
             GetMarshallerFieldName(method->output_type()));
  out->Print("\n");
  out->Outdent();
  out->Outdent();
}

void GenerateServiceDescriptorProperty(Printer* out,
                                       const ServiceDescriptor* service) {
  std::ostringstream index;
  index << service->index();
  out->Print("/// <summary>Service descriptor</summary>\n");
  out->Print(
      "public static global::Google.Protobuf.Reflection.ServiceDescriptor "
      "Descriptor\n");
  out->Print("{\n");
  out->Print("  get { return $umbrella$.Descriptor.Services[$index$]; }\n",
             "umbrella", GetReflectionClassName(service->file()), "index",
             index.str());
  out->Print("}\n");
  out->Print("\n");
}

void GenerateServerClass(Printer* out, const ServiceDescriptor* service) {
  out->Print(
      "/// <summary>Base class for server-side implementations of "
      "$servicename$</summary>\n",
      "servicename", GetServiceClassName(service));
  out->Print("public abstract partial class $name$\n", "name",
             GetServerClassName(service));
  out->Print("{\n");
  out->Indent();
  for (int i = 0; i < service->method_count(); i++) {
    const MethodDescriptor* method = service->method(i);
    GenerateDocCommentServerMethod(out, method);
    out->Print(
        "public virtual $returntype$ "
        "$methodname$($request$$response_stream_maybe$, "
        "grpc::ServerCallContext context)\n",
        "methodname", method->name(), "returntype",
        GetMethodReturnTypeServer(method), "request",
        GetMethodRequestParamServer(method), "response_stream_maybe",
        GetMethodResponseStreamMaybe(method));
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

void GenerateClientStub(Printer* out, const ServiceDescriptor* service) {
  out->Print("/// <summary>Client for $servicename$</summary>\n", "servicename",
             GetServiceClassName(service));
  out->Print("public partial class $name$ : grpc::ClientBase<$name$>\n", "name",
             GetClientClassName(service));
  out->Print("{\n");
  out->Indent();

  // constructors
  out->Print(
      "/// <summary>Creates a new client for $servicename$</summary>\n"
      "/// <param name=\"channel\">The channel to use to make remote "
      "calls.</param>\n",
      "servicename", GetServiceClassName(service));
  out->Print("public $name$(grpc::Channel channel) : base(channel)\n", "name",
             GetClientClassName(service));
  out->Print("{\n");
  out->Print("}\n");
  out->Print(
      "/// <summary>Creates a new client for $servicename$ that uses a custom "
      "<c>CallInvoker</c>.</summary>\n"
      "/// <param name=\"callInvoker\">The callInvoker to use to make remote "
      "calls.</param>\n",
      "servicename", GetServiceClassName(service));
  out->Print(
      "public $name$(grpc::CallInvoker callInvoker) : base(callInvoker)\n",
      "name", GetClientClassName(service));
  out->Print("{\n");
  out->Print("}\n");
  out->Print(
      "/// <summary>Protected parameterless constructor to allow creation"
      " of test doubles.</summary>\n");
  out->Print("protected $name$() : base()\n", "name",
             GetClientClassName(service));
  out->Print("{\n");
  out->Print("}\n");
  out->Print(
      "/// <summary>Protected constructor to allow creation of configured "
      "clients.</summary>\n"
      "/// <param name=\"configuration\">The client configuration.</param>\n");
  out->Print(
      "protected $name$(ClientBaseConfiguration configuration)"
      " : base(configuration)\n",
      "name", GetClientClassName(service));
  out->Print("{\n");
  out->Print("}\n\n");

  for (int i = 0; i < service->method_count(); i++) {
    const MethodDescriptor* method = service->method(i);
    MethodType method_type = GetMethodType(method);

    if (method_type == METHODTYPE_NO_STREAMING) {
      // unary calls have an extra synchronous stub method
      GenerateDocCommentClientMethod(out, method, true, false);
      out->Print(
          "public virtual $response$ $methodname$($request$ request, "
          "grpc::Metadata "
          "headers = null, DateTime? deadline = null, CancellationToken "
          "cancellationToken = default(CancellationToken))\n",
          "methodname", method->name(), "request",
          GetClassName(method->input_type()), "response",
          GetClassName(method->output_type()));
      out->Print("{\n");
      out->Indent();
      out->Print(
          "return $methodname$(request, new grpc::CallOptions(headers, "
          "deadline, "
          "cancellationToken));\n",
          "methodname", method->name());
      out->Outdent();
      out->Print("}\n");

      // overload taking CallOptions as a param
      GenerateDocCommentClientMethod(out, method, true, true);
      out->Print(
          "public virtual $response$ $methodname$($request$ request, "
          "grpc::CallOptions options)\n",
          "methodname", method->name(), "request",
          GetClassName(method->input_type()), "response",
          GetClassName(method->output_type()));
      out->Print("{\n");
      out->Indent();
      out->Print(
          "return CallInvoker.BlockingUnaryCall($methodfield$, null, options, "
          "request);\n",
          "methodfield", GetMethodFieldName(method));
      out->Outdent();
      out->Print("}\n");
    }

    std::string method_name = method->name();
    if (method_type == METHODTYPE_NO_STREAMING) {
      method_name += "Async";  // prevent name clash with synchronous method.
    }
    GenerateDocCommentClientMethod(out, method, false, false);
    out->Print(
        "public virtual $returntype$ "
        "$methodname$($request_maybe$grpc::Metadata "
        "headers = null, DateTime? deadline = null, CancellationToken "
        "cancellationToken = default(CancellationToken))\n",
        "methodname", method_name, "request_maybe",
        GetMethodRequestParamMaybe(method), "returntype",
        GetMethodReturnTypeClient(method));
    out->Print("{\n");
    out->Indent();

    out->Print(
        "return $methodname$($request_maybe$new grpc::CallOptions(headers, "
        "deadline, "
        "cancellationToken));\n",
        "methodname", method_name, "request_maybe",
        GetMethodRequestParamMaybe(method, true));
    out->Outdent();
    out->Print("}\n");

    // overload taking CallOptions as a param
    GenerateDocCommentClientMethod(out, method, false, true);
    out->Print(
        "public virtual $returntype$ "
        "$methodname$($request_maybe$grpc::CallOptions "
        "options)\n",
        "methodname", method_name, "request_maybe",
        GetMethodRequestParamMaybe(method), "returntype",
        GetMethodReturnTypeClient(method));
    out->Print("{\n");
    out->Indent();
    switch (GetMethodType(method)) {
      case METHODTYPE_NO_STREAMING:
        out->Print(
            "return CallInvoker.AsyncUnaryCall($methodfield$, null, options, "
            "request);\n",
            "methodfield", GetMethodFieldName(method));
        break;
      case METHODTYPE_CLIENT_STREAMING:
        out->Print(
            "return CallInvoker.AsyncClientStreamingCall($methodfield$, null, "
            "options);\n",
            "methodfield", GetMethodFieldName(method));
        break;
      case METHODTYPE_SERVER_STREAMING:
        out->Print(
            "return CallInvoker.AsyncServerStreamingCall($methodfield$, null, "
            "options, request);\n",
            "methodfield", GetMethodFieldName(method));
        break;
      case METHODTYPE_BIDI_STREAMING:
        out->Print(
            "return CallInvoker.AsyncDuplexStreamingCall($methodfield$, null, "
            "options);\n",
            "methodfield", GetMethodFieldName(method));
        break;
      default:
        GOOGLE_LOG(FATAL) << "Can't get here.";
    }
    out->Outdent();
    out->Print("}\n");
  }

  // override NewInstance method
  out->Print(
      "/// <summary>Creates a new instance of client from given "
      "<c>ClientBaseConfiguration</c>.</summary>\n");
  out->Print(
      "protected override $name$ NewInstance(ClientBaseConfiguration "
      "configuration)\n",
      "name", GetClientClassName(service));
  out->Print("{\n");
  out->Indent();
  out->Print("return new $name$(configuration);\n", "name",
             GetClientClassName(service));
  out->Outdent();
  out->Print("}\n");

  out->Outdent();
  out->Print("}\n");
  out->Print("\n");
}

void GenerateBindServiceMethod(Printer* out, const ServiceDescriptor* service) {
  out->Print(
      "/// <summary>Creates service definition that can be registered with a "
      "server</summary>\n");
  out->Print(
      "/// <param name=\"serviceImpl\">An object implementing the server-side"
      " handling logic.</param>\n");
  out->Print(
      "public static grpc::ServerServiceDefinition BindService($implclass$ "
      "serviceImpl)\n",
      "implclass", GetServerClassName(service));
  out->Print("{\n");
  out->Indent();

  out->Print("return grpc::ServerServiceDefinition.CreateBuilder()\n");
  out->Indent();
  out->Indent();
  for (int i = 0; i < service->method_count(); i++) {
    const MethodDescriptor* method = service->method(i);
    out->Print(".AddMethod($methodfield$, serviceImpl.$methodname$)",
               "methodfield", GetMethodFieldName(method), "methodname",
               method->name());
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

void GenerateService(Printer* out, const ServiceDescriptor* service,
                     bool generate_client, bool generate_server,
                     bool internal_access) {
  GenerateDocCommentBody(out, service);
  out->Print("$access_level$ static partial class $classname$\n",
             "access_level", GetAccessLevel(internal_access), "classname",
             GetServiceClassName(service));
  out->Print("{\n");
  out->Indent();
  out->Print("static readonly string $servicenamefield$ = \"$servicename$\";\n",
             "servicenamefield", GetServiceNameFieldName(), "servicename",
             service->full_name());
  out->Print("\n");

  GenerateMarshallerFields(out, service);
  for (int i = 0; i < service->method_count(); i++) {
    GenerateStaticMethodField(out, service->method(i));
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

grpc::string GetServices(const FileDescriptor* file, bool generate_client,
                         bool generate_server, bool internal_access) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.

    StringOutputStream output_stream(&output);
    Printer out(&output_stream, '$');

    // Don't write out any output if there no services, to avoid empty service
    // files being generated for proto files that don't declare any.
    if (file->service_count() == 0) {
      return output;
    }

    // Write out a file header.
    out.Print("// Generated by the protocol buffer compiler.  DO NOT EDIT!\n");
    out.Print("// source: $filename$\n", "filename", file->name());

    // use C++ style as there are no file-level XML comments in .NET
    grpc::string leading_comments = GetCsharpComments(file, true);
    if (!leading_comments.empty()) {
      out.Print("// Original file comments:\n");
      out.PrintRaw(leading_comments.c_str());
    }

    out.Print("#pragma warning disable 1591\n");
    out.Print("#region Designer generated code\n");
    out.Print("\n");
    out.Print("using System;\n");
    out.Print("using System.Threading;\n");
    out.Print("using System.Threading.Tasks;\n");
    out.Print("using grpc = global::Grpc.Core;\n");
    out.Print("\n");

    out.Print("namespace $namespace$ {\n", "namespace", GetFileNamespace(file));
    out.Indent();
    for (int i = 0; i < file->service_count(); i++) {
      GenerateService(&out, file->service(i), generate_client, generate_server,
                      internal_access);
    }
    out.Outdent();
    out.Print("}\n");
    out.Print("#endregion\n");
  }
  return output;
}

}  // namespace grpc_csharp_generator
