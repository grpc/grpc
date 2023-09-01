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

#include "src/compiler/csharp_generator.h"

#include <cctype>
#include <map>
#include <sstream>
#include <vector>

#include "src/compiler/config.h"
#include "src/compiler/csharp_generator_helpers.h"

using grpc::protobuf::Descriptor;
using grpc::protobuf::FileDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::io::Printer;
using grpc::protobuf::io::StringOutputStream;
using grpc_generator::StringReplace;
using std::vector;

namespace grpc_csharp_generator {
namespace {

// This function is a massaged version of
// https://github.com/protocolbuffers/protobuf/blob/master/src/google/protobuf/compiler/csharp/csharp_doc_comment.cc
// Currently, we cannot easily reuse the functionality as
// google/protobuf/compiler/csharp/csharp_doc_comment.h is not a public header.
// TODO(jtattermusch): reuse the functionality from google/protobuf.
bool GenerateDocCommentBodyImpl(grpc::protobuf::io::Printer* printer,
                                grpc::protobuf::SourceLocation location) {
  std::string comments = location.leading_comments.empty()
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

  std::vector<std::string> lines;
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
  for (std::vector<std::string>::iterator it = lines.begin(); it != lines.end();
       ++it) {
    std::string line = *it;
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

void GenerateGeneratedCodeAttribute(grpc::protobuf::io::Printer* printer) {
  // Mark the code as generated using the [GeneratedCode] attribute.
  // We don't provide plugin version info in attribute the because:
  // * the version information is not readily available from the plugin's code.
  // * it would cause a lot of churn in the pre-generated code
  //   in this repository every time the version is updated.
  printer->Print(
      "[global::System.CodeDom.Compiler.GeneratedCode(\"grpc_csharp_plugin\", "
      "null)]\n");
}

void GenerateObsoleteAttribute(grpc::protobuf::io::Printer* printer,
                               bool is_deprecated) {
  // Mark the code deprecated using the [ObsoleteAttribute] attribute.
  if (is_deprecated) {
    printer->Print("[global::System.ObsoleteAttribute]\n");
  }
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

std::string GetCSharpMethodType(const MethodDescriptor* method) {
  if (method->client_streaming()) {
    if (method->server_streaming()) {
      return "grpc::MethodType.DuplexStreaming";
    } else {
      return "grpc::MethodType.ClientStreaming";
    }
  } else {
    if (method->server_streaming()) {
      return "grpc::MethodType.ServerStreaming";
    } else {
      return "grpc::MethodType.Unary";
    }
  }
}

std::string GetCSharpServerMethodType(const MethodDescriptor* method) {
  if (method->client_streaming()) {
    if (method->server_streaming()) {
      return "grpc::DuplexStreamingServerMethod";
    } else {
      return "grpc::ClientStreamingServerMethod";
    }
  } else {
    if (method->server_streaming()) {
      return "grpc::ServerStreamingServerMethod";
    } else {
      return "grpc::UnaryServerMethod";
    }
  }
}

std::string GetServiceNameFieldName() { return "__ServiceName"; }

std::string GetMarshallerFieldName(const Descriptor* message) {
  return "__Marshaller_" +
         grpc_generator::StringReplace(message->full_name(), ".", "_", true);
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
  return GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->input_type()) + " request, ";
}

std::string GetAccessLevel(bool internal_access) {
  return internal_access ? "internal" : "public";
}

std::string GetMethodReturnTypeClient(const MethodDescriptor* method) {
  if (method->client_streaming()) {
    if (method->server_streaming()) {
      return "grpc::AsyncDuplexStreamingCall<" +
             GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->input_type()) + ", " +
             GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->output_type()) + ">";
    } else {
      return "grpc::AsyncClientStreamingCall<" +
             GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->input_type()) + ", " +
             GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->output_type()) + ">";
    }
  } else {
    if (method->server_streaming()) {
      return "grpc::AsyncServerStreamingCall<" +
             GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->output_type()) + ">";
    } else {
      return "grpc::AsyncUnaryCall<" +
             GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->output_type()) + ">";
    }
  }
}

std::string GetMethodRequestParamServer(const MethodDescriptor* method) {
  if (method->client_streaming()) {
    return "grpc::IAsyncStreamReader<" +
           GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->input_type()) +
           "> requestStream";
  }
  return GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->input_type()) + " request";
}

std::string GetMethodReturnTypeServer(const MethodDescriptor* method) {
  if (method->server_streaming()) {
    return "global::System.Threading.Tasks.Task";
  }
  return "global::System.Threading.Tasks.Task<" +
         GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->output_type()) + ">";
}

std::string GetMethodResponseStreamMaybe(const MethodDescriptor* method) {
  if (method->server_streaming()) {
    return ", grpc::IServerStreamWriter<" +
           GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->output_type()) +
           "> responseStream";
  }
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
  if (used_messages.size() != 0) {
    // Generate static helper methods for serialization/deserialization
    GenerateGeneratedCodeAttribute(out);
    out->Print(
        "static void __Helper_SerializeMessage("
        "global::Google.Protobuf.IMessage message, "
        "grpc::SerializationContext context)\n"
        "{\n");
    out->Indent();
    out->Print(
        "#if !GRPC_DISABLE_PROTOBUF_BUFFER_SERIALIZATION\n"
        "if (message is global::Google.Protobuf.IBufferMessage)\n"
        "{\n");
    out->Indent();
    out->Print(
        "context.SetPayloadLength(message.CalculateSize());\n"
        "global::Google.Protobuf.MessageExtensions.WriteTo(message, "
        "context.GetBufferWriter());\n"
        "context.Complete();\n"
        "return;\n");
    out->Outdent();
    out->Print(
        "}\n"
        "#endif\n");
    out->Print(
        "context.Complete("
        "global::Google.Protobuf.MessageExtensions.ToByteArray(message));\n");
    out->Outdent();
    out->Print("}\n\n");

    GenerateGeneratedCodeAttribute(out);
    out->Print(
        "static class __Helper_MessageCache<T>\n"
        "{\n");
    out->Indent();
    out->Print(
        "public static readonly bool IsBufferMessage = "
        "global::System.Reflection.IntrospectionExtensions.GetTypeInfo(typeof("
        "global::Google.Protobuf.IBufferMessage)).IsAssignableFrom(typeof(T));"
        "\n");
    out->Outdent();
    out->Print("}\n\n");

    GenerateGeneratedCodeAttribute(out);
    out->Print(
        "static T __Helper_DeserializeMessage<T>("
        "grpc::DeserializationContext context, "
        "global::Google.Protobuf.MessageParser<T> parser) "
        "where T : global::Google.Protobuf.IMessage<T>\n"
        "{\n");
    out->Indent();
    out->Print(
        "#if !GRPC_DISABLE_PROTOBUF_BUFFER_SERIALIZATION\n"
        "if (__Helper_MessageCache<T>.IsBufferMessage)\n"
        "{\n");
    out->Indent();
    out->Print(
        "return parser.ParseFrom(context.PayloadAsReadOnlySequence());\n");
    out->Outdent();
    out->Print(
        "}\n"
        "#endif\n");
    out->Print("return parser.ParseFrom(context.PayloadAsNewBuffer());\n");
    out->Outdent();
    out->Print("}\n\n");
  }

  for (size_t i = 0; i < used_messages.size(); i++) {
    const Descriptor* message = used_messages[i];
    GenerateGeneratedCodeAttribute(out);
    out->Print(
        "static readonly grpc::Marshaller<$type$> $fieldname$ = "
        "grpc::Marshallers.Create(__Helper_SerializeMessage, "
        "context => __Helper_DeserializeMessage(context, $type$.Parser));\n",
        "fieldname", GetMarshallerFieldName(message), "type",
        GRPC_CUSTOM_CSHARP_GETCLASSNAME(message));
  }
  out->Print("\n");
}

void GenerateStaticMethodField(Printer* out, const MethodDescriptor* method) {
  GenerateGeneratedCodeAttribute(out);
  out->Print(
      "static readonly grpc::Method<$request$, $response$> $fieldname$ = new "
      "grpc::Method<$request$, $response$>(\n",
      "fieldname", GetMethodFieldName(method), "request",
      GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->input_type()), "response",
      GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->output_type()));
  out->Indent();
  out->Indent();
  out->Print("$methodtype$,\n", "methodtype", GetCSharpMethodType(method));
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
             "umbrella",
             GRPC_CUSTOM_CSHARP_GETREFLECTIONCLASSNAME(service->file()),
             "index", index.str());
  out->Print("}\n");
  out->Print("\n");
}

void GenerateServerClass(Printer* out, const ServiceDescriptor* service) {
  out->Print(
      "/// <summary>Base class for server-side implementations of "
      "$servicename$</summary>\n",
      "servicename", GetServiceClassName(service));
  GenerateObsoleteAttribute(out, service->options().deprecated());
  out->Print(
      "[grpc::BindServiceMethod(typeof($classname$), "
      "\"BindService\")]\n",
      "classname", GetServiceClassName(service));
  out->Print("public abstract partial class $name$\n", "name",
             GetServerClassName(service));
  out->Print("{\n");
  out->Indent();
  for (int i = 0; i < service->method_count(); i++) {
    const MethodDescriptor* method = service->method(i);
    GenerateDocCommentServerMethod(out, method);
    GenerateObsoleteAttribute(out, method->options().deprecated());
    GenerateGeneratedCodeAttribute(out);
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
  GenerateObsoleteAttribute(out, service->options().deprecated());
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
  GenerateGeneratedCodeAttribute(out);
  out->Print("public $name$(grpc::ChannelBase channel) : base(channel)\n",
             "name", GetClientClassName(service));
  out->Print("{\n");
  out->Print("}\n");
  out->Print(
      "/// <summary>Creates a new client for $servicename$ that uses a custom "
      "<c>CallInvoker</c>.</summary>\n"
      "/// <param name=\"callInvoker\">The callInvoker to use to make remote "
      "calls.</param>\n",
      "servicename", GetServiceClassName(service));
  GenerateGeneratedCodeAttribute(out);
  out->Print(
      "public $name$(grpc::CallInvoker callInvoker) : base(callInvoker)\n",
      "name", GetClientClassName(service));
  out->Print("{\n");
  out->Print("}\n");
  out->Print(
      "/// <summary>Protected parameterless constructor to allow creation"
      " of test doubles.</summary>\n");
  GenerateGeneratedCodeAttribute(out);
  out->Print("protected $name$() : base()\n", "name",
             GetClientClassName(service));
  out->Print("{\n");
  out->Print("}\n");
  out->Print(
      "/// <summary>Protected constructor to allow creation of configured "
      "clients.</summary>\n"
      "/// <param name=\"configuration\">The client configuration.</param>\n");
  GenerateGeneratedCodeAttribute(out);
  out->Print(
      "protected $name$(ClientBaseConfiguration configuration)"
      " : base(configuration)\n",
      "name", GetClientClassName(service));
  out->Print("{\n");
  out->Print("}\n\n");

  for (int i = 0; i < service->method_count(); i++) {
    const MethodDescriptor* method = service->method(i);
    const bool is_deprecated = method->options().deprecated();
    if (!method->client_streaming() && !method->server_streaming()) {
      // unary calls have an extra synchronous stub method
      GenerateDocCommentClientMethod(out, method, true, false);
      GenerateObsoleteAttribute(out, is_deprecated);
      GenerateGeneratedCodeAttribute(out);
      out->Print(
          "public virtual $response$ $methodname$($request$ request, "
          "grpc::Metadata "
          "headers = null, global::System.DateTime? deadline = null, "
          "global::System.Threading.CancellationToken "
          "cancellationToken = "
          "default(global::System.Threading.CancellationToken))\n",
          "methodname", method->name(), "request",
          GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->input_type()), "response",
          GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->output_type()));
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
      GenerateObsoleteAttribute(out, is_deprecated);
      GenerateGeneratedCodeAttribute(out);
      out->Print(
          "public virtual $response$ $methodname$($request$ request, "
          "grpc::CallOptions options)\n",
          "methodname", method->name(), "request",
          GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->input_type()), "response",
          GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->output_type()));
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
    if (!method->client_streaming() && !method->server_streaming()) {
      method_name += "Async";  // prevent name clash with synchronous method.
    }
    GenerateDocCommentClientMethod(out, method, false, false);
    GenerateObsoleteAttribute(out, is_deprecated);
    GenerateGeneratedCodeAttribute(out);
    out->Print(
        "public virtual $returntype$ "
        "$methodname$($request_maybe$grpc::Metadata "
        "headers = null, global::System.DateTime? deadline = null, "
        "global::System.Threading.CancellationToken "
        "cancellationToken = "
        "default(global::System.Threading.CancellationToken))\n",
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
    GenerateObsoleteAttribute(out, is_deprecated);
    GenerateGeneratedCodeAttribute(out);
    out->Print(
        "public virtual $returntype$ "
        "$methodname$($request_maybe$grpc::CallOptions "
        "options)\n",
        "methodname", method_name, "request_maybe",
        GetMethodRequestParamMaybe(method), "returntype",
        GetMethodReturnTypeClient(method));
    out->Print("{\n");
    out->Indent();
    if (!method->client_streaming() && !method->server_streaming()) {
      // Non-Streaming
      out->Print(
          "return CallInvoker.AsyncUnaryCall($methodfield$, null, options, "
          "request);\n",
          "methodfield", GetMethodFieldName(method));
    } else if (method->client_streaming() && !method->server_streaming()) {
      // Client Streaming Only
      out->Print(
          "return CallInvoker.AsyncClientStreamingCall($methodfield$, null, "
          "options);\n",
          "methodfield", GetMethodFieldName(method));
    } else if (!method->client_streaming() && method->server_streaming()) {
      // Server Streaming Only
      out->Print(
          "return CallInvoker.AsyncServerStreamingCall($methodfield$, null, "
          "options, request);\n",
          "methodfield", GetMethodFieldName(method));
    } else {
      // Bi-Directional Streaming
      out->Print(
          "return CallInvoker.AsyncDuplexStreamingCall($methodfield$, null, "
          "options);\n",
          "methodfield", GetMethodFieldName(method));
    }
    out->Outdent();
    out->Print("}\n");
  }

  // override NewInstance method
  out->Print(
      "/// <summary>Creates a new instance of client from given "
      "<c>ClientBaseConfiguration</c>.</summary>\n");
  GenerateGeneratedCodeAttribute(out);
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
  GenerateGeneratedCodeAttribute(out);
  out->Print(
      "public static grpc::ServerServiceDefinition BindService($implclass$ "
      "serviceImpl)\n",
      "implclass", GetServerClassName(service));
  out->Print("{\n");
  out->Indent();

  out->Print("return grpc::ServerServiceDefinition.CreateBuilder()");
  out->Indent();
  out->Indent();
  for (int i = 0; i < service->method_count(); i++) {
    const MethodDescriptor* method = service->method(i);
    out->Print("\n.AddMethod($methodfield$, serviceImpl.$methodname$)",
               "methodfield", GetMethodFieldName(method), "methodname",
               method->name());
  }
  out->Print(".Build();\n");
  out->Outdent();
  out->Outdent();

  out->Outdent();
  out->Print("}\n");
  out->Print("\n");
}

void GenerateBindServiceWithBinderMethod(Printer* out,
                                         const ServiceDescriptor* service) {
  out->Print(
      "/// <summary>Register service method with a service "
      "binder with or without implementation. Useful when customizing the "
      "service binding logic.\n"
      "/// Note: this method is part of an experimental API that can change or "
      "be "
      "removed without any prior notice.</summary>\n");
  out->Print(
      "/// <param name=\"serviceBinder\">Service methods will be bound by "
      "calling <c>AddMethod</c> on this object."
      "</param>\n");
  out->Print(
      "/// <param name=\"serviceImpl\">An object implementing the server-side"
      " handling logic.</param>\n");
  GenerateGeneratedCodeAttribute(out);
  out->Print(
      "public static void BindService(grpc::ServiceBinderBase serviceBinder, "
      "$implclass$ "
      "serviceImpl)\n",
      "implclass", GetServerClassName(service));
  out->Print("{\n");
  out->Indent();

  for (int i = 0; i < service->method_count(); i++) {
    const MethodDescriptor* method = service->method(i);
    out->Print(
        "serviceBinder.AddMethod($methodfield$, serviceImpl == null ? null : "
        "new $servermethodtype$<$inputtype$, $outputtype$>("
        "serviceImpl.$methodname$));\n",
        "methodfield", GetMethodFieldName(method), "servermethodtype",
        GetCSharpServerMethodType(method), "inputtype",
        GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->input_type()), "outputtype",
        GRPC_CUSTOM_CSHARP_GETCLASSNAME(method->output_type()), "methodname",
        method->name());
  }

  out->Outdent();
  out->Print("}\n");
  out->Print("\n");
}

void GenerateService(Printer* out, const ServiceDescriptor* service,
                     bool generate_client, bool generate_server,
                     bool internal_access) {
  GenerateDocCommentBody(out, service);

  GenerateObsoleteAttribute(out, service->options().deprecated());
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
    GenerateBindServiceWithBinderMethod(out, service);
  }

  out->Outdent();
  out->Print("}\n");
}

}  // anonymous namespace

std::string GetServices(const FileDescriptor* file, bool generate_client,
                        bool generate_server, bool internal_access) {
  std::string output;
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
    out.Print("// <auto-generated>\n");
    out.Print(
        "//     Generated by the protocol buffer compiler.  DO NOT EDIT!\n");
    out.Print("//     source: $filename$\n", "filename", file->name());
    out.Print("// </auto-generated>\n");

    // use C++ style as there are no file-level XML comments in .NET
    std::string leading_comments = GetCsharpComments(file, true);
    if (!leading_comments.empty()) {
      out.Print("// Original file comments:\n");
      out.PrintRaw(leading_comments.c_str());
    }

    out.Print("#pragma warning disable 0414, 1591, 8981, 0612\n");

    out.Print("#region Designer generated code\n");
    out.Print("\n");
    out.Print("using grpc = global::Grpc.Core;\n");
    out.Print("\n");

    std::string file_namespace = GRPC_CUSTOM_CSHARP_GETFILENAMESPACE(file);
    if (file_namespace != "") {
      out.Print("namespace $namespace$ {\n", "namespace", file_namespace);
      out.Indent();
    }
    for (int i = 0; i < file->service_count(); i++) {
      GenerateService(&out, file->service(i), generate_client, generate_server,
                      internal_access);
    }
    if (file_namespace != "") {
      out.Outdent();
      out.Print("}\n");
    }
    out.Print("#endregion\n");
  }
  return output;
}

}  // namespace grpc_csharp_generator
