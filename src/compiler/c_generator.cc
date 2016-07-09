/*
 *
 * Copyright 2016, Google Inc.
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
#include <algorithm>

#include "src/compiler/c_generator.h"
#include "src/compiler/c_generator_helpers.h"
#include "src/compiler/cpp_generator.h"
#include "src/compiler/cpp_generator_helpers.h"

/*
 * C Generator
 * Contains methods for printing comments, service headers, service implementations, etc.
 */
namespace grpc_c_generator {

namespace {

template <class T>
grpc::string as_string(T x) {
  std::ostringstream out;
  out << x;
  return out.str();
}

grpc::string FilenameIdentifier(const grpc::string &filename) {
  grpc::string result;
  for (unsigned i = 0; i < filename.size(); i++) {
    char c = filename[i];
    if (isalnum(c)) {
      result.push_back(c);
    } else {
      static char hex[] = "0123456789abcdef";
      result.push_back('_');
      result.push_back(hex[(c >> 4) & 0xf]);
      result.push_back(hex[c & 0xf]);
    }
  }
  return result;
}

}  // namespace

using grpc::protobuf::FileDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::Descriptor;
using grpc::protobuf::io::StringOutputStream;

using grpc_cpp_generator::Parameters;
using grpc_cpp_generator::File;
using grpc_cpp_generator::Method;
using grpc_cpp_generator::Service;
using grpc_cpp_generator::Printer;

template<class T, size_t N>
T *array_end(T (&array)[N]) { return array + N; }

grpc::string Join(std::vector<grpc::string> lines, grpc::string delim) {
  std::ostringstream imploded;
  std::copy(lines.begin(), lines.end(), std::ostream_iterator<grpc::string>(imploded, delim.c_str()));
  return imploded.str();
}

grpc::string BlockifyComments(grpc::string input) {
  constexpr int kMaxCharactersPerLine = 90;
  std::vector<grpc::string> lines = grpc_generator::tokenize(input, "\n");
  // kill trailing new line
  if (lines[lines.size() - 1] == "") lines.pop_back();
  for (grpc::string& str : lines) {
    grpc_generator::StripPrefix(&str, "//");
    str.append(std::max(0UL, kMaxCharactersPerLine - str.size()), ' ');
    str = "/* " + str + " */";
  }
  return Join(lines, "\n");
}

// Prints a list of header paths as include directives
void PrintIncludes(Printer *printer, const std::vector<grpc::string>& headers, const Parameters &params) {
  std::map<grpc::string, grpc::string> vars;

  vars["l"] = params.use_system_headers ? '<' : '"';
  vars["r"] = params.use_system_headers ? '>' : '"';

  auto& s = params.grpc_search_path;
  if (!s.empty()) {
    vars["l"] += s;
    if (s[s.size()-1] != '/') {
      vars["l"] += '/';
    }
  }

  for (auto i = headers.begin(); i != headers.end(); i++) {
    vars["h"] = *i;
    printer->Print(vars, "#include $l$$h$$r$\n");
  }
}

// Prints declaration of a single client method
void PrintHeaderClientMethod(Printer *printer,
                             const Method *method,
                             std::map<grpc::string, grpc::string> *vars) {
  (*vars)["Method"] = method->name();
  (*vars)["Request"] = method->input_type_name();
  (*vars)["Response"] = method->output_type_name();

  if (method->NoStreaming()) {

    printer->Print(
      *vars,
R"(GRPC_status $CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        const $CPrefix$$Request$ request,
        $CPrefix$$Response$ *response);
)");
    printer->Print(
      *vars,
R"(GRPC_status $CPrefix$$Service$_$Method$_Async(
        GRPC_client_context *const context,
        GRPC_completion_queue *cq,
        const $CPrefix$$Request$ request);

void HLW_Greeter_SayHello_Finish(
        GRPC_client_async_response_reader *reader,
        HLW_HelloResponse *response,        /* pointer to store RPC response */
        void *tag);
/* call GRPC_completion_queue_next on the cq to wait for result */
)");

  } else if (method->ClientOnlyStreaming()) {

    printer->Print(
      *vars,
R"(GRPC_status $CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        const $CPrefix$$Request$ request,
        $CPrefix$$Response$ *response);
)");

  } else if (method->ServerOnlyStreaming()) {

    printer->Print(
      *vars,
R"(GRPC_status $CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        const $CPrefix$$Request$ request,
        $CPrefix$$Response$ *response);
)");

  } else if (method->BidiStreaming()) {

    printer->Print(
      *vars,
R"(GRPC_status $CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        const $CPrefix$$Request$ request,
        $CPrefix$$Response$ *response);
)");

  }
}

// Prints declaration of a single service
void PrintHeaderService(Printer *printer,
                        const Service *service,
                        std::map<grpc::string, grpc::string> *vars) {
  (*vars)["Service"] = service->name();

  printer->Print(*vars, BlockifyComments("Service declaration for " + service->name() + "\n").c_str());
  printer->Print(BlockifyComments(service->GetLeadingComments()).c_str());

  // Client side
  for (int i = 0; i < service->method_count(); ++i) {
    printer->Print(BlockifyComments(service->method(i)->GetLeadingComments()).c_str());
    PrintHeaderClientMethod(printer, service->method(i).get(), vars);
    printer->Print(BlockifyComments(service->method(i)->GetTrailingComments()).c_str());
  }
  printer->Print("\n\n");

  // Server side - TBD

  printer->Print(BlockifyComments(service->GetTrailingComments()).c_str());
}

// Prints implementation of a single client method
void PrintSourceClientMethod(Printer *printer,
                             const Method *method,
                             std::map<grpc::string, grpc::string> *vars) {
  (*vars)["Method"] = method->name();
  (*vars)["Request"] = method->input_type_name();
  (*vars)["Response"] = method->output_type_name();
  if (method->NoStreaming()) {
    printer->Print(*vars,
                   "::grpc::Status $ns$$Service$::Stub::$Method$("
                     "::grpc::ClientContext* context, "
                     "const $Request$& request, $Response$* response) {\n");
    printer->Print(*vars,
                   "  return ::grpc::BlockingUnaryCall(channel_.get(), "
                     "rpcmethod_$Method$_, "
                     "context, request, response);\n"
                     "}\n\n");
    printer->Print(
      *vars,
      "::grpc::ClientAsyncResponseReader< $Response$>* "
        "$ns$$Service$::Stub::Async$Method$Raw(::grpc::ClientContext* context, "
        "const $Request$& request, "
        "::grpc::CompletionQueue* cq) {\n");
    printer->Print(*vars,
                   "  return new "
                     "::grpc::ClientAsyncResponseReader< $Response$>("
                     "channel_.get(), cq, "
                     "rpcmethod_$Method$_, "
                     "context, request);\n"
                     "}\n\n");
  } else if (method->ClientOnlyStreaming()) {
    printer->Print(*vars,
                   "::grpc::ClientWriter< $Request$>* "
                     "$ns$$Service$::Stub::$Method$Raw("
                     "::grpc::ClientContext* context, $Response$* response) {\n");
    printer->Print(*vars,
                   "  return new ::grpc::ClientWriter< $Request$>("
                     "channel_.get(), "
                     "rpcmethod_$Method$_, "
                     "context, response);\n"
                     "}\n\n");
    printer->Print(*vars,
                   "::grpc::ClientAsyncWriter< $Request$>* "
                     "$ns$$Service$::Stub::Async$Method$Raw("
                     "::grpc::ClientContext* context, $Response$* response, "
                     "::grpc::CompletionQueue* cq, void* tag) {\n");
    printer->Print(*vars,
                   "  return new ::grpc::ClientAsyncWriter< $Request$>("
                     "channel_.get(), cq, "
                     "rpcmethod_$Method$_, "
                     "context, response, tag);\n"
                     "}\n\n");
  } else if (method->ServerOnlyStreaming()) {
    printer->Print(
      *vars,
      "::grpc::ClientReader< $Response$>* "
        "$ns$$Service$::Stub::$Method$Raw("
        "::grpc::ClientContext* context, const $Request$& request) {\n");
    printer->Print(*vars,
                   "  return new ::grpc::ClientReader< $Response$>("
                     "channel_.get(), "
                     "rpcmethod_$Method$_, "
                     "context, request);\n"
                     "}\n\n");
    printer->Print(*vars,
                   "::grpc::ClientAsyncReader< $Response$>* "
                     "$ns$$Service$::Stub::Async$Method$Raw("
                     "::grpc::ClientContext* context, const $Request$& request, "
                     "::grpc::CompletionQueue* cq, void* tag) {\n");
    printer->Print(*vars,
                   "  return new ::grpc::ClientAsyncReader< $Response$>("
                     "channel_.get(), cq, "
                     "rpcmethod_$Method$_, "
                     "context, request, tag);\n"
                     "}\n\n");
  } else if (method->BidiStreaming()) {
    printer->Print(
      *vars,
      "::grpc::ClientReaderWriter< $Request$, $Response$>* "
        "$ns$$Service$::Stub::$Method$Raw(::grpc::ClientContext* context) {\n");
    printer->Print(*vars,
                   "  return new ::grpc::ClientReaderWriter< "
                     "$Request$, $Response$>("
                     "channel_.get(), "
                     "rpcmethod_$Method$_, "
                     "context);\n"
                     "}\n\n");
    printer->Print(
      *vars,
      "::grpc::ClientAsyncReaderWriter< $Request$, $Response$>* "
        "$ns$$Service$::Stub::Async$Method$Raw(::grpc::ClientContext* context, "
        "::grpc::CompletionQueue* cq, void* tag) {\n");
    printer->Print(*vars,
                   "  return new "
                     "::grpc::ClientAsyncReaderWriter< $Request$, $Response$>("
                     "channel_.get(), cq, "
                     "rpcmethod_$Method$_, "
                     "context, tag);\n"
                     "}\n\n");
  }
}

void PrintSourceServerMethod(Printer *printer,
                             const Method *method,
                             std::map<grpc::string, grpc::string> *vars) {
  // TBD
}

// Prints implementation of all methods in a service
void PrintSourceService(Printer *printer,
                        const Service *service,
                        std::map<grpc::string, grpc::string> *vars) {
  (*vars)["Service"] = service->name();

  printer->Print(*vars,
                 "static const char* $prefix$$Service$_method_names[] = {\n");
  for (int i = 0; i < service->method_count(); ++i) {
    (*vars)["Method"] = service->method(i).get()->name();
    printer->Print(*vars, "  \"/$Package$$Service$/$Method$\",\n");
  }
  printer->Print(*vars, "};\n\n");

  printer->Print(*vars,
                 "std::unique_ptr< $ns$$Service$::Stub> $ns$$Service$::NewStub("
                   "const std::shared_ptr< ::grpc::ChannelInterface>& channel, "
                   "const ::grpc::StubOptions& options) {\n"
                   "  std::unique_ptr< $ns$$Service$::Stub> stub(new "
                   "$ns$$Service$::Stub(channel));\n"
                   "  return stub;\n"
                   "}\n\n");
  printer->Print(*vars,
                 "$ns$$Service$::Stub::Stub(const std::shared_ptr< "
                   "::grpc::ChannelInterface>& channel)\n");
  printer->Indent();
  printer->Print(": channel_(channel)");
  for (int i = 0; i < service->method_count(); ++i) {
    auto method = service->method(i);
    (*vars)["Method"] = method->name();
    (*vars)["Idx"] = as_string(i);
    if (method->NoStreaming()) {
      (*vars)["StreamingType"] = "NORMAL_RPC";
    } else if (method->ClientOnlyStreaming()) {
      (*vars)["StreamingType"] = "CLIENT_STREAMING";
    } else if (method->ServerOnlyStreaming()) {
      (*vars)["StreamingType"] = "SERVER_STREAMING";
    } else {
      (*vars)["StreamingType"] = "BIDI_STREAMING";
    }
    printer->Print(*vars,
                   ", rpcmethod_$Method$_("
                     "$prefix$$Service$_method_names[$Idx$], "
                     "::grpc::RpcMethod::$StreamingType$, "
                     "channel"
                     ")\n");
  }
  printer->Print("{}\n\n");
  printer->Outdent();

  for (int i = 0; i < service->method_count(); ++i) {
    (*vars)["Idx"] = as_string(i);
    PrintSourceClientMethod(printer, service->method(i).get(), vars);
  }

  printer->Print(*vars, "$ns$$Service$::Service::Service() {\n");
  printer->Indent();
  printer->Print(*vars, "(void)$prefix$$Service$_method_names;\n");
  for (int i = 0; i < service->method_count(); ++i) {
    auto method = service->method(i);
    (*vars)["Idx"] = as_string(i);
    (*vars)["Method"] = method->name();
    (*vars)["Request"] = method->input_type_name();
    (*vars)["Response"] = method->output_type_name();
    if (method->NoStreaming()) {
      printer->Print(
        *vars,
        "AddMethod(new ::grpc::RpcServiceMethod(\n"
          "    $prefix$$Service$_method_names[$Idx$],\n"
          "    ::grpc::RpcMethod::NORMAL_RPC,\n"
          "    new ::grpc::RpcMethodHandler< $ns$$Service$::Service, "
          "$Request$, "
          "$Response$>(\n"
          "        std::mem_fn(&$ns$$Service$::Service::$Method$), this)));\n");
    } else if (method->ClientOnlyStreaming()) {
      printer->Print(
        *vars,
        "AddMethod(new ::grpc::RpcServiceMethod(\n"
          "    $prefix$$Service$_method_names[$Idx$],\n"
          "    ::grpc::RpcMethod::CLIENT_STREAMING,\n"
          "    new ::grpc::ClientStreamingHandler< "
          "$ns$$Service$::Service, $Request$, $Response$>(\n"
          "        std::mem_fn(&$ns$$Service$::Service::$Method$), this)));\n");
    } else if (method->ServerOnlyStreaming()) {
      printer->Print(
        *vars,
        "AddMethod(new ::grpc::RpcServiceMethod(\n"
          "    $prefix$$Service$_method_names[$Idx$],\n"
          "    ::grpc::RpcMethod::SERVER_STREAMING,\n"
          "    new ::grpc::ServerStreamingHandler< "
          "$ns$$Service$::Service, $Request$, $Response$>(\n"
          "        std::mem_fn(&$ns$$Service$::Service::$Method$), this)));\n");
    } else if (method->BidiStreaming()) {
      printer->Print(
        *vars,
        "AddMethod(new ::grpc::RpcServiceMethod(\n"
          "    $prefix$$Service$_method_names[$Idx$],\n"
          "    ::grpc::RpcMethod::BIDI_STREAMING,\n"
          "    new ::grpc::BidiStreamingHandler< "
          "$ns$$Service$::Service, $Request$, $Response$>(\n"
          "        std::mem_fn(&$ns$$Service$::Service::$Method$), this)));\n");
    }
  }
  printer->Outdent();
  printer->Print(*vars, "}\n\n");
  printer->Print(*vars,
                 "$ns$$Service$::Service::~Service() {\n"
                   "}\n\n");
  for (int i = 0; i < service->method_count(); ++i) {
    (*vars)["Idx"] = as_string(i);
    PrintSourceServerMethod(printer, service->method(i).get(), vars);
  }
}

//
// PUBLIC
//

grpc::string GetHeaderServices(File *file,
                               const Parameters &params) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    auto printer = file->CreatePrinter(&output);
    std::map<grpc::string, grpc::string> vars;
    // Package string is empty or ends with a dot. It is used to fully qualify
    // method names.
    vars["Package"] = file->package();
    if (!file->package().empty()) {
      vars["Package"].append(".");
    }

    for (int i = 0; i < file->service_count(); ++i) {
      PrintHeaderService(printer.get(), file->service(i).get(), &vars);
      printer->Print("\n");
    }

  }
  return output;
}

grpc::string GetHeaderEpilogue(File *file, const Parameters & /*params*/) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    auto printer = file->CreatePrinter(&output);
    std::map<grpc::string, grpc::string> vars;

    vars["filename"] = file->filename();
    vars["filename_identifier"] = FilenameIdentifier(file->filename());

    if (!file->package().empty()) {
      std::vector<grpc::string> parts = file->package_parts();

      for (auto part = parts.rbegin(); part != parts.rend(); part++) {
        vars["part"] = *part;
      }
      printer->Print(vars, "\n");
    }

    printer->Print(vars, "\n");
    printer->Print(vars, "#endif  /* GRPC_C_$filename_identifier$__INCLUDED */\n");

    printer->Print(file->GetTrailingComments().c_str());
  }
  return output;
}

grpc::string GetSourcePrologue(File *file, const Parameters & /*params*/) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    auto printer = file->CreatePrinter(&output);
    std::map<grpc::string, grpc::string> vars;

    vars["filename"] = file->filename();
    vars["filename_base"] = file->filename_without_ext();
    vars["message_header_ext"] = file->message_header_ext();
    vars["service_header_ext"] = file->service_header_ext();

    printer->Print(vars, BlockifyComments(
R"(
// Generated by the gRPC protobuf plugin.
// If you make any local change, they will be lost.
)").c_str());

    grpc::string filename;
    {
      auto printer_filename = file->CreatePrinter(&filename);
      printer_filename->Print(vars, "// source: $filename$");
    }
    printer->Print(vars, BlockifyComments(filename).c_str());

    printer->Print(vars, "#include \"$filename_base$$message_header_ext$\"\n");
    printer->Print(vars, "#include \"$filename_base$$service_header_ext$\"\n");

    printer->Print(vars, file->additional_headers().c_str());
    printer->Print(vars, "\n");
  }
  return output;
}

grpc::string GetSourceIncludes(File *file,
                               const Parameters &params) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    auto printer = file->CreatePrinter(&output);
    std::map<grpc::string, grpc::string> vars;

    static const char *headers_strs[] = {
      "grpc_c/status_code.h",
      "grpc_c/grpc_c.h",
      "grpc_c/channel.h",
      "grpc_c/unary_blocking_call.h",
      "grpc_c/unary_async_call.h",
      "grpc_c/client_streaming_blocking_call.h",
      "grpc_c/server_streaming_blocking_call.h",
      "grpc_c/bidi_streaming_blocking_call.h",
      "grpc_c/context.h"
    };
    std::vector<grpc::string> headers(headers_strs, array_end(headers_strs));
    PrintIncludes(printer.get(), headers, params);

    printer->Print(vars, "\n");
  }
  return output;
}

grpc::string GetSourceEpilogue(File *file, const Parameters & /*params*/) {
  grpc::string temp;

  temp.append("/* END */\n");

  return temp;
}

grpc::string GetHeaderPrologue(File *file, const Parameters & /*params*/) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    auto printer = file->CreatePrinter(&output);
    std::map<grpc::string, grpc::string> vars;

    vars["filename"] = file->filename();
    vars["filename_identifier"] = FilenameIdentifier(file->filename());
    vars["filename_base"] = file->filename_without_ext();
    vars["message_header_ext"] = file->message_header_ext();

    printer->Print(vars, BlockifyComments(
      R"(
// Generated by the gRPC protobuf plugin.
// If you make any local change, they will be lost.
)").c_str());

    grpc::string filename;
    {
      auto printer_filename = file->CreatePrinter(&filename);
      printer_filename->Print(vars, "// source: $filename$");
    }
    printer->Print(vars, BlockifyComments(filename).c_str());

    grpc::string leading_comments = file->GetLeadingComments();
    if (!leading_comments.empty()) {
      printer->Print(vars, BlockifyComments("// Original file comments:\n").c_str());
      printer->Print(BlockifyComments(leading_comments).c_str());
    }
    printer->Print(vars, "#ifndef GRPC_C_$filename_identifier$__INCLUDED\n");
    printer->Print(vars, "#define GRPC_C_$filename_identifier$__INCLUDED\n");
    printer->Print(vars, "\n");
    printer->Print(vars, "#include \"$filename_base$$message_header_ext$\"\n");
    printer->Print(vars, "\n");
  }
  return output;
}

grpc::string GetHeaderIncludes(File *file,
                               const Parameters &params) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    auto printer = file->CreatePrinter(&output);
    std::map<grpc::string, grpc::string> vars;

    static const char *headers_strs[] = {
      "grpc_c/status_code.h",
      "grpc_c/grpc_c.h",
      "grpc_c/context.h"
    };
    std::vector<grpc::string> headers(headers_strs, array_end(headers_strs));
    PrintIncludes(printer.get(), headers, params);
    printer->Print(vars, "\n");
  }
  return output;
}

grpc::string GetSourceServices(File *file,
                               const Parameters &params) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    auto printer = file->CreatePrinter(&output);
    std::map<grpc::string, grpc::string> vars;
    // Package string is empty or ends with a dot. It is used to fully qualify
    // method names.
    vars["Package"] = file->package();
    // TODO(yifeit): hook this up to C prefix
    vars["CPrefix"] = "";
    if (!file->package().empty()) {
      vars["Package"].append(".");
    }

    for (int i = 0; i < file->service_count(); ++i) {
      PrintSourceService(printer.get(), file->service(i).get(), &vars);
      printer->Print("\n");
    }
  }
  return output;
}

} // namespace grpc_c_generator
