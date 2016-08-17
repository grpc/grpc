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

#include <algorithm>
#include <map>

#include "src/compiler/c_generator.h"
#include "src/compiler/c_generator_helpers.h"
#include "src/compiler/cpp_generator_helpers.h"

/*
 * C Generator
 * Contains methods for printing comments, service headers, service
 * implementations, etc.
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

grpc::string Join(std::vector<grpc::string> lines, grpc::string delim) {
  std::ostringstream imploded;
  std::copy(lines.begin(), lines.end(),
            std::ostream_iterator<grpc::string>(imploded, delim.c_str()));
  return imploded.str();
}

grpc::string BlockifyComments(grpc::string input) {
  const int kMaxCharactersPerLine = 90;
  std::vector<grpc::string> lines = grpc_generator::tokenize(input, "\n");
  // kill trailing new line
  if (lines[lines.size() - 1] == "") lines.pop_back();
  for (auto itr = lines.begin(); itr != lines.end(); itr++) {
    grpc_generator::StripPrefix(&*itr, "//");
    (*itr).append(std::max(size_t(0), kMaxCharactersPerLine - (*itr).size()),
                  ' ');
    *itr = "/* " + *itr + " */";
  }
  return Join(lines, "\n");
}

template <class T, size_t N>
T *array_end(T (&array)[N]) {
  return array + N;
}

}  // namespace

using grpc::protobuf::FileDescriptor;
using grpc::protobuf::ServiceDescriptor;
using grpc::protobuf::MethodDescriptor;
using grpc::protobuf::Descriptor;
using grpc::protobuf::io::StringOutputStream;

using grpc_cpp_generator::File;
using grpc_cpp_generator::Method;
using grpc_cpp_generator::Service;
using grpc_cpp_generator::Printer;

// Prints a list of header paths as include directives
void PrintIncludes(Printer *printer, const std::vector<grpc::string> &headers,
                   const Parameters &params) {
  std::map<grpc::string, grpc::string> vars;

  vars["l"] = params.use_system_headers ? '<' : '"';
  vars["r"] = params.use_system_headers ? '>' : '"';

  auto &s = params.grpc_search_path;
  if (!s.empty()) {
    vars["l"] += s;
    if (s[s.size() - 1] != '/') {
      vars["l"] += '/';
    }
  }

  for (auto i = headers.begin(); i != headers.end(); i++) {
    vars["h"] = *i;
    printer->Print(vars, "#include $l$$h$$r$\n");
  }
}

// Prints declaration of a single server method
void PrintHeaderServerMethod(Printer *printer, const Method *method,
                             std::map<grpc::string, grpc::string> *vars) {
  (*vars)["Method"] = method->name();
  (*vars)["Request"] = method->input_type_name();
  (*vars)["Response"] = method->output_type_name();

  if (method->NoStreaming()) {
    // Unary

    printer->Print(*vars,
                   "/* Async */\n"
                   "GRPC_server_async_response_writer *"
                   "$CPrefix$$Service$_$Method$_ServerRequest(\n"
                   "        GRPC_registered_service *service,\n"
                   "        GRPC_server_context *const context,\n"
                   "        $CPrefix$$Request$ *request,\n"
                   "        GRPC_incoming_notification_queue *incoming_queue,\n"
                   "        GRPC_completion_queue *processing_queue,\n"
                   "        void *tag);\n"
                   "\n");

    printer->Print(*vars,
                   "void $CPrefix$$Service$_$Method$_ServerFinish(\n"
                   "        GRPC_server_async_response_writer *writer,\n"
                   "        $CPrefix$$Response$ *response,\n"
                   "        GRPC_status_code server_status,\n"
                   "        void *tag);\n"
                   "\n");
  }

  printer->Print("\n\n");
}

// Prints declaration of a single client method
void PrintHeaderClientMethod(Printer *printer, const Method *method,
                             std::map<grpc::string, grpc::string> *vars) {
  (*vars)["Method"] = method->name();
  (*vars)["Request"] = method->input_type_name();
  (*vars)["Response"] = method->output_type_name();

  if (method->NoStreaming()) {
    // Unary

    printer->Print(*vars,
                   "/* Sync */\n"
                   "GRPC_status $CPrefix$$Service$_$Method$(\n"
                   "        GRPC_client_context *const context,\n"
                   "        const $CPrefix$$Request$ request,\n"
                   "        $CPrefix$$Response$ *response);\n"
                   "\n");

    printer->Print(
        *vars,
        "\n"
        "/* Async */\n"
        "GRPC_client_async_response_reader "
        "*$CPrefix$$Service$_$Method$_Async(\n"
        "        GRPC_client_context *const context,\n"
        "        GRPC_completion_queue *cq,\n"
        "        const $CPrefix$$Request$ request);\n"
        "\n"
        "void $CPrefix$$Service$_$Method$_Finish(\n"
        "        GRPC_client_async_response_reader *reader,\n"
        "        $CPrefix$$Response$ *response,\n"
        "        void *tag);\n"
        "/* call GRPC_completion_queue_next on the cq to wait for result */\n"
        "\n");

  } else if (method->ClientOnlyStreaming()) {
    // Client streaming

    printer->Print(
        *vars,
        "\n"
        "/* Sync */\n"
        "GRPC_client_writer *$CPrefix$$Service$_$Method$(\n"
        "        GRPC_client_context *const context,\n"
        "        $CPrefix$$Response$ *response);\n"
        "\n"
        "/* Return value of true means write succeeded */\n"
        "bool $CPrefix$$Service$_$Method$_Write(\n"
        "        GRPC_client_writer *writer,\n"
        "        $CPrefix$$Request$ request);\n"
        "\n"
        "/* Call $CPrefix$$Service$_$Method$_Terminate to close the stream and "
        "end the call */\n"
        "/* The writer is automatically freed when the request ends */\n"
        "GRPC_status $CPrefix$$Service$_$Method$_Terminate(GRPC_client_writer "
        "*writer);\n"
        "\n");

    printer->Print(
        *vars,
        "\n"
        "/* Async */\n"
        "GRPC_client_async_writer *$CPrefix$$Service$_$Method$_Async(\n"
        "        GRPC_client_context *const context,\n"
        "        GRPC_completion_queue *cq);\n"
        "\n"
        "void $CPrefix$$Service$_$Method$_Write_Async(\n"
        "        GRPC_client_async_writer *writer,\n"
        "        const $CPrefix$$Request$ request,\n"
        "        void *tag);\n"
        "\n"
        "void $CPrefix$$Service$_$Method$_Finish(\n"
        "        GRPC_client_async_writer *writer,\n"
        "        $CPrefix$$Response$ *response,\n"
        "        void *tag);\n"
        "/* Call GRPC_completion_queue_next on the cq to wait for result.   "
        "*/\n"
        "/* The writer object is automatically freed when the request ends. "
        "*/\n"
        "\n");

  } else if (method->ServerOnlyStreaming()) {
    // Server streaming

    printer->Print(
        *vars,
        "\n"
        "/* Sync */\n"
        "GRPC_client_reader *$CPrefix$$Service$_$Method$(\n"
        "        GRPC_client_context *const context,\n"
        "        $CPrefix$$Request$ request);\n"
        "\n"
        "/* Return value of true means read succeeded */\n"
        "bool $CPrefix$$Service$_$Method$_Read(\n"
        "        GRPC_client_reader *reader,\n"
        "        $CPrefix$$Response$ *response);\n"
        "\n"
        "/* Call $CPrefix$$Service$_$Method$_Terminate to close the stream and "
        "end the call */\n"
        "/* The reader is automatically freed when the request ends */\n"
        "GRPC_status $CPrefix$$Service$_$Method$_Terminate(GRPC_client_reader "
        "*reader);\n"
        "\n");
    printer->Print(
        *vars,
        "\n"
        "/* Async */\n"
        "GRPC_client_async_reader *$CPrefix$$Service$_$Method$_Async(\n"
        "        GRPC_client_context *const context,\n"
        "        GRPC_completion_queue *cq,\n"
        "        const $CPrefix$$Request$ request);\n"
        "\n"
        "void $CPrefix$$Service$_$Method$_Read_Async(\n"
        "        GRPC_client_async_reader *reader,\n"
        "        $CPrefix$$Response$ *response,\n"
        "        void *tag);\n"
        "\n"
        "void $CPrefix$$Service$_$Method$_Finish(\n"
        "        GRPC_client_async_reader *reader,\n"
        "        void *tag);\n"
        "/* call GRPC_completion_queue_next on the cq to wait for result */\n"
        "/* the reader object is automatically freed when the request ends */\n"
        "\n");

  } else if (method->BidiStreaming()) {
    // Bidi

    printer->Print(
        *vars,
        "\n"
        "/* Sync */\n"
        "GRPC_client_reader_writer *$CPrefix$$Service$_$Method$(\n"
        "        GRPC_client_context *const context);\n"
        "\n"
        "bool $CPrefix$$Service$_$Method$_Read(\n"
        "        GRPC_client_reader_writer *reader_writer,\n"
        "        $CPrefix$$Response$ *response);\n"
        "\n"
        "bool $CPrefix$$Service$_$Method$_Write(\n"
        "        GRPC_client_reader_writer *reader_writer,\n"
        "        $CPrefix$$Request$ request);\n"
        "\n"
        "/* Signals to the server that we are no longer sending request items "
        "*/\n"
        "bool "
        "$CPrefix$$Service$_$Method$_Writes_Done(GRPC_client_reader_writer "
        "*reader_writer);\n"
        "\n"
        "/* Ends the call. The reader_writer object is automatically freed */\n"
        "GRPC_status "
        "$CPrefix$$Service$_$Method$_Terminate(GRPC_client_reader_writer "
        "*reader_writer);\n"
        "\n");

    printer->Print(
        *vars,
        "\n"
        "/* Async */\n"
        "GRPC_client_async_reader_writer *$CPrefix$$Service$_$Method$_Async(\n"
        "        GRPC_client_context *const context);\n"
        "\n"
        "void $CPrefix$$Service$_$Method$_Read_Async(\n"
        "        GRPC_client_async_reader_writer *reader_writer,\n"
        "        $CPrefix$$Response$ *response,\n"
        "        void *tag);\n"
        "\n"
        "void $CPrefix$$Service$_$Method$_Write_Async(\n"
        "        GRPC_client_async_reader_writer *reader_writer,\n"
        "        $CPrefix$$Request$ request,\n"
        "        void *tag);\n"
        "\n"
        "void $CPrefix$$Service$_$Method$_Finish(\n"
        "        GRPC_client_async_reader_writer *reader_writer,\n"
        "        void *tag);\n"
        "/* call GRPC_completion_queue_next on the cq to wait for result */\n"
        "/* the reader-writer object is automatically freed when the request "
        "ends */\n"
        "\n");
  }

  printer->Print("\n\n");
}

void PrintHeaderServiceDeclaration(Printer *printer, const Service *service,
                                   std::map<grpc::string, grpc::string> *vars) {
  // Register method
  printer->Print(*vars,
                 "/* Call this to handle this service in the server */\n"
                 "GRPC_registered_service "
                 "*$CPrefix$$Service$_Register(GRPC_server *server);\n\n");
}

// Prints declaration of a single service
void PrintHeaderService(Printer *printer, const Service *service,
                        std::map<grpc::string, grpc::string> *vars) {
  (*vars)["Service"] = service->name();

  printer->Print(*vars, BlockifyComments("Service metadata for " +
                                         service->name() + "\n\n")
                            .c_str());
  PrintHeaderServiceDeclaration(printer, service, vars);

  printer->Print(*vars, BlockifyComments("Service declaration for " +
                                         service->name() + "\n")
                            .c_str());
  printer->Print(BlockifyComments(service->GetLeadingComments()).c_str());

  // Client side
  printer->Print("/* Client */\n");
  for (int i = 0; i < service->method_count(); ++i) {
    printer->Print(
        BlockifyComments(service->method(i)->GetLeadingComments()).c_str());
    PrintHeaderClientMethod(printer, service->method(i).get(), vars);
    printer->Print(
        BlockifyComments(service->method(i)->GetTrailingComments()).c_str());
  }
  printer->Print("\n\n");

  // Server side
  printer->Print("/* Server */\n");
  for (int i = 0; i < service->method_count(); ++i) {
    printer->Print(
        BlockifyComments(service->method(i)->GetLeadingComments()).c_str());
    PrintHeaderServerMethod(printer, service->method(i).get(), vars);
    printer->Print(
        BlockifyComments(service->method(i)->GetTrailingComments()).c_str());
  }
  printer->Print("\n\n");

  printer->Print(BlockifyComments(service->GetTrailingComments()).c_str());
}

void PrintSourceServerMethod(Printer *printer, const Method *method,
                             std::map<grpc::string, grpc::string> *vars) {
  (*vars)["Method"] = method->name();
  (*vars)["Request"] = method->input_type_name();
  (*vars)["Response"] = method->output_type_name();

  if (method->NoStreaming()) {
    // Unary

    printer->Print(
        *vars,
        "GRPC_server_async_response_writer *"
        "$CPrefix$$Service$_$Method$_ServerRequest(\n"
        "        GRPC_registered_service *service,\n"
        "        GRPC_server_context *const context,\n"
        "        $CPrefix$$Request$ *request,\n"
        "        GRPC_incoming_notification_queue *incoming_queue,\n"
        "        GRPC_completion_queue *processing_queue,\n"
        "        void *tag) {\n"
        "  GRPC_context_set_serialization_impl((GRPC_context *) context,\n"
        "        (grpc_serialization_impl) { "
        "GRPC_C_RESOLVE_SERIALIZER($CPrefix$$Request$), "
        "GRPC_C_RESOLVE_DESERIALIZER($CPrefix$$Response$) });\n"
        "  return GRPC_unary_async_server_request(\n"
        "        service,\n"
        "        GRPC_METHOD_INDEX_$CPrefix$$Service$_$Method$,\n"
        "        context,\n"
        "        request,\n"
        "        incoming_queue,\n"
        "        processing_queue,\n"
        "        tag);\n"
        "}\n"
        "\n");

    printer->Print(
        *vars,
        "void $CPrefix$$Service$_$Method$_ServerFinish(\n"
        "        GRPC_server_async_response_writer *writer,\n"
        "        $CPrefix$$Response$ *response,\n"
        "        GRPC_status_code server_status,\n"
        "        void *tag) {\n"
        "  const GRPC_message response_msg = { response, sizeof(*response) };\n"
        "  GRPC_unary_async_server_finish(\n"
        "        writer,\n"
        "        response_msg,\n"
        "        server_status,\n"
        "        tag);\n"
        "}\n"
        "\n");
  }
}

void PrintSourceServiceDeclaration(Printer *printer, const Service *service,
                                   std::map<grpc::string, grpc::string> *vars) {
  for (int i = 0; i < service->method_count(); i++) {
    auto method = service->method(i);

    (*vars)["Method"] = method->name();

    if (method->NoStreaming()) {
      (*vars)["MethodEnum"] = "GRPC_NORMAL_RPC";
    } else if (method->ClientOnlyStreaming()) {
      (*vars)["MethodEnum"] = "GRPC_CLIENT_STREAMING";
    } else if (method->ServerOnlyStreaming()) {
      (*vars)["MethodEnum"] = "GRPC_SERVER_STREAMING";
    } else if (method->BidiStreaming()) {
      (*vars)["MethodEnum"] = "GRPC_BIDI_STREAMING";
    }

    printer->Print(*vars,
                   "GRPC_method GRPC_method_$CPrefix$$Service$_$Method$ = {\n"
                   "        $MethodEnum$,\n"
                   "        \"/$Package$$Service$/$Method$\"\n"
                   "};\n"
                   "\n");
  }

  printer->Print(
      *vars, "GRPC_service_declaration GRPC_service_$CPrefix$$Service$ = {\n");

  // Insert each method definition in the service
  for (int i = 0; i < service->method_count(); i++) {
    auto method = service->method(i);
    (*vars)["Method"] = method->name();
    (*vars)["Terminator"] = i == service->method_count() - 1 ? "" : ",";
    printer->Print(
        *vars,
        "        &GRPC_method_$CPrefix$$Service$_$Method$$Terminator$\n");
  }

  printer->Print(*vars,
                 "};\n"
                 "\n");

  // Array index of each method inside the service declaration array
  printer->Print(*vars, "enum {\n");

  for (int i = 0; i < service->method_count(); i++) {
    auto inner_vars = *vars;
    auto method = service->method(i);
    inner_vars["Method"] = method->name();
    inner_vars["Index"] = std::to_string(static_cast<long long>(i));
    printer->Print(
        inner_vars,
        "        GRPC_METHOD_INDEX_$CPrefix$$Service$_$Method$ = $Index$,\n");
  }

  (*vars)["MethodCount"] =
      std::to_string(static_cast<long long>(service->method_count()));
  printer->Print(
      *vars,
      "        GRPC_METHOD_COUNT_$CPrefix$$Service$ = $MethodCount$\n"
      "};\n"
      "\n");

  printer->Print(*vars,
                 "GRPC_registered_service "
                 "*$CPrefix$$Service$_Register(GRPC_server *server) {\n"
                 "        return GRPC_server_add_service(server, "
                 "GRPC_service_$CPrefix$$Service$, "
                 "GRPC_METHOD_COUNT_$CPrefix$$Service$);\n"
                 "}\n"
                 "\n");
}

// Prints implementation of a single client method
void PrintSourceClientMethod(Printer *printer, const Method *method,
                             std::map<grpc::string, grpc::string> *vars) {
  (*vars)["Method"] = method->name();
  (*vars)["Request"] = method->input_type_name();
  (*vars)["Response"] = method->output_type_name();

  if (method->NoStreaming()) {
    // Unary
    printer->Print(
        *vars,
        "\n"
        "GRPC_status $CPrefix$$Service$_$Method$(\n"
        "        GRPC_client_context *const context,\n"
        "        const $CPrefix$$Request$ request,\n"
        "        $CPrefix$$Response$ *response) {\n"
        "  const GRPC_message request_msg = { &request, sizeof(request) };\n"
        "  GRPC_context_set_serialization_impl((GRPC_context *) context,\n"
        "        (grpc_serialization_impl) { "
        "GRPC_C_RESOLVE_SERIALIZER($CPrefix$$Request$), "
        "GRPC_C_RESOLVE_DESERIALIZER($CPrefix$$Response$) });\n"
        "  return "
        "GRPC_unary_blocking_call(GRPC_method_$CPrefix$$Service$_$Method$, "
        "context, request_msg, response);\n"
        "}\n"
        "\n");
    printer->Print(
        *vars,
        "\n"
        "/* Async */\n"
        "GRPC_client_async_response_reader "
        "*$CPrefix$$Service$_$Method$_Async(\n"
        "        GRPC_client_context *const context,\n"
        "        GRPC_completion_queue *cq,\n"
        "        const $CPrefix$$Request$ request) {\n"
        "  const GRPC_message request_msg = { &request, sizeof(request) };\n"
        "  GRPC_context_set_serialization_impl((GRPC_context *) context,\n"
        "        (grpc_serialization_impl) { "
        "GRPC_C_RESOLVE_SERIALIZER($CPrefix$$Request$), "
        "GRPC_C_RESOLVE_DESERIALIZER($CPrefix$$Response$) });\n"
        "  return GRPC_unary_async_call(cq, "
        "GRPC_method_$CPrefix$$Service$_$Method$, request_msg, context);\n"
        "}\n"
        "\n"
        "void $CPrefix$$Service$_$Method$_Finish(\n"
        "        GRPC_client_async_response_reader *reader,\n"
        "        $CPrefix$$Response$ *response,\n"
        "        void *tag) {\n"
        "  GRPC_client_async_finish(reader, response, tag);\n"
        "}\n"
        "\n");

  } else if (method->ClientOnlyStreaming()) {
    printer->Print(
        *vars,
        "\n"
        "GRPC_client_writer *$CPrefix$$Service$_$Method$(\n"
        "        GRPC_client_context *const context,\n"
        "        $CPrefix$$Response$ *response) {\n"
        "  GRPC_context_set_serialization_impl((GRPC_context *) context,\n"
        "        (grpc_serialization_impl) { "
        "GRPC_C_RESOLVE_SERIALIZER($CPrefix$$Request$), "
        "GRPC_C_RESOLVE_DESERIALIZER($CPrefix$$Response$) });\n"
        "  return "
        "GRPC_client_streaming_blocking_call(GRPC_method_$CPrefix$$Service$_$"
        "Method$, context, response);\n"
        "}\n"
        "\n"
        "bool $CPrefix$$Service$_$Method$_Write(\n"
        "        GRPC_client_writer *writer,\n"
        "        $CPrefix$$Request$ request) {\n"
        "  const GRPC_message request_msg = { &request, sizeof(request) };\n"
        "  return GRPC_client_streaming_blocking_write(writer, request_msg);\n"
        "}\n"
        "\n"
        "GRPC_status $CPrefix$$Service$_$Method$_Terminate(GRPC_client_writer "
        "*writer) {\n"
        "  return GRPC_client_writer_terminate(writer);\n"
        "}\n"
        "\n");

    printer->Print(*vars,
                   "\n"
                   "/* Async TBD */\n"
                   "\n");

  } else if (method->ServerOnlyStreaming()) {
    printer->Print(
        *vars,
        "\n"
        "GRPC_client_reader *$CPrefix$$Service$_$Method$(\n"
        "        GRPC_client_context *const context,\n"
        "        $CPrefix$$Request$ request) {\n"
        "  const GRPC_message request_msg = { &request, sizeof(request) };\n"
        "  GRPC_context_set_serialization_impl((GRPC_context *) context,\n"
        "        (grpc_serialization_impl) { "
        "GRPC_C_RESOLVE_SERIALIZER($CPrefix$$Request$), "
        "GRPC_C_RESOLVE_DESERIALIZER($CPrefix$$Response$) });\n"
        "  return "
        "GRPC_server_streaming_blocking_call(GRPC_method_$CPrefix$$Service$_$"
        "Method$, context, request_msg);\n"
        "}\n"
        "\n"
        "bool $CPrefix$$Service$_$Method$_Read(\n"
        "        GRPC_client_reader *reader,\n"
        "        $CPrefix$$Response$ *response) {\n"
        "  return GRPC_server_streaming_blocking_read(reader, response);\n"
        "}\n"
        "\n"
        "GRPC_status $CPrefix$$Service$_$Method$_Terminate(GRPC_client_reader "
        "*reader) {\n"
        "  return GRPC_client_reader_terminate(reader);\n"
        "}\n"
        "\n");
    printer->Print(*vars,
                   "\n"
                   "/* Async TBD */\n"
                   "\n");

  } else if (method->BidiStreaming()) {
    printer->Print(
        *vars,
        "\n"
        "GRPC_client_reader_writer *$CPrefix$$Service$_$Method$(\n"
        "        GRPC_client_context *const context) {\n"
        "  GRPC_context_set_serialization_impl((GRPC_context *) context,\n"
        "        (grpc_serialization_impl) { "
        "GRPC_C_RESOLVE_SERIALIZER($CPrefix$$Request$), "
        "GRPC_C_RESOLVE_DESERIALIZER($CPrefix$$Response$) });\n"
        "  return "
        "GRPC_bidi_streaming_blocking_call(GRPC_method_$CPrefix$$Service$_$"
        "Method$, context);\n"
        "}\n"
        "\n"
        "bool $CPrefix$$Service$_$Method$_Read(\n"
        "        GRPC_client_reader_writer *reader_writer,\n"
        "        $CPrefix$$Response$ *response) {\n"
        "  return GRPC_bidi_streaming_blocking_read(reader_writer, response);\n"
        "}\n"
        "\n"
        "bool $CPrefix$$Service$_$Method$_Write(\n"
        "        GRPC_client_reader_writer *reader_writer,\n"
        "        $CPrefix$$Request$ request) {\n"
        "  const GRPC_message request_msg = { &request, sizeof(request) };\n"
        "  return GRPC_bidi_streaming_blocking_write(reader_writer, "
        "request_msg);\n"
        "}\n"
        "\n"
        "bool "
        "$CPrefix$$Service$_$Method$_Writes_Done(GRPC_client_reader_writer "
        "*reader_writer) {\n"
        "  return GRPC_bidi_streaming_blocking_writes_done(reader_writer);\n"
        "}\n"
        "\n"
        "GRPC_status "
        "$CPrefix$$Service$_$Method$_Terminate(GRPC_client_reader_writer "
        "*reader_writer) {\n"
        "  return GRPC_client_reader_writer_terminate(reader_writer);\n"
        "}\n"
        "\n");
    printer->Print(*vars,
                   "\n"
                   "/* Async TBD */\n"
                   "\n");
  }
}

// Prints implementation of all methods in a service
void PrintSourceService(Printer *printer, const Service *service,
                        std::map<grpc::string, grpc::string> *vars) {
  (*vars)["Service"] = service->name();

  printer->Print(*vars, BlockifyComments("Service metadata for " +
                                         service->name() + "\n\n")
                            .c_str());
  PrintSourceServiceDeclaration(printer, service, vars);

  printer->Print(*vars, BlockifyComments("Service implementation for " +
                                         service->name() + "\n\n")
                            .c_str());
  for (int i = 0; i < service->method_count(); ++i) {
    PrintSourceClientMethod(printer, service->method(i).get(), vars);
    PrintSourceServerMethod(printer, service->method(i).get(), vars);
  }

  printer->Print("\n");
}

//
// PUBLIC
//

grpc::string GetHeaderServices(File *file, const Parameters &params) {
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
    // TODO(yifeit): hook this up to C prefix
    vars["CPrefix"] =
        grpc_cpp_generator::DotsToUnderscores(file->package()) + "_";

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
    printer->Print(vars,
                   "#endif  /* GRPC_C_$filename_identifier$__INCLUDED */\n");

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

    printer->Print(
        vars,
        BlockifyComments("\n"
                         "// Generated by the gRPC protobuf plugin.\n"
                         "// If you make any local change, they will be lost.\n"
                         "\n")
            .c_str());

    grpc::string filename;
    {
      auto printer_filename = file->CreatePrinter(&filename);
      printer_filename->Print(vars, "// source: $filename$");
    }
    printer->Print(vars, BlockifyComments(filename).c_str());

    printer->Print(vars, "/* Message header */\n");
    printer->Print(vars, "#include \"$filename_base$$message_header_ext$\"\n");
    printer->Print(vars, "/* Other message dependencies */\n");
    // include all other message headers on which this one depends
    auto deps = dynamic_cast<CFile *>(file)->dependencies();
    for (auto itr = deps.begin(); itr != deps.end(); itr++) {
      std::map<grpc::string, grpc::string> depvars(vars);
      depvars["filename_base"] = (*itr)->filename_without_ext();
      depvars["service_header_ext"] = (*itr)->service_header_ext();
      printer->Print(depvars,
                     "#include \"$filename_base$$message_header_ext$\"\n");
    }
    printer->Print(vars, "/* Service header */\n");
    printer->Print(vars, "#include \"$filename_base$$service_header_ext$\"\n");

    printer->Print(vars, file->additional_headers().c_str());
    printer->Print(vars, "\n");
  }
  return output;
}

grpc::string GetSourceIncludes(File *file, const Parameters &params) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    auto printer = file->CreatePrinter(&output);
    std::map<grpc::string, grpc::string> vars;

    static const char *headers_strs[] = {
        "grpc_c/status.h", "grpc_c/grpc_c.h", "grpc_c/channel.h",
        "grpc_c/server.h", "grpc_c/server_incoming_queue.h",
        "grpc_c/client_context.h", "grpc_c/server_context.h",
        "grpc_c/codegen/message.h", "grpc_c/codegen/method.h",
        "grpc_c/codegen/unary_blocking_call.h",
        "grpc_c/codegen/unary_async_call.h", "grpc_c/codegen/server.h",
        "grpc_c/codegen/client_streaming_blocking_call.h",
        "grpc_c/codegen/server_streaming_blocking_call.h",
        "grpc_c/codegen/bidi_streaming_blocking_call.h",
        "grpc_c/codegen/context.h",
        // Relying on Nanopb for Protobuf serialization for now
        "grpc_c/codegen/pb_compat.h", "grpc_c/declare_serializer.h"};
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

    printer->Print(
        vars,
        BlockifyComments("\n"
                         "// Generated by the gRPC protobuf plugin.\n"
                         "// If you make any local change, they will be lost.\n"
                         "\n")
            .c_str());

    grpc::string filename;
    {
      auto printer_filename = file->CreatePrinter(&filename);
      printer_filename->Print(vars, "// source: $filename$");
    }
    printer->Print(vars, BlockifyComments(filename).c_str());

    grpc::string leading_comments = file->GetLeadingComments();
    if (!leading_comments.empty()) {
      printer->Print(vars,
                     BlockifyComments("// Original file comments:\n").c_str());
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

grpc::string GetHeaderIncludes(File *file, const Parameters &params) {
  grpc::string output;
  {
    // Scope the output stream so it closes and finalizes output to the string.
    auto printer = file->CreatePrinter(&output);
    std::map<grpc::string, grpc::string> vars;

    static const char *headers_strs[] = {
        "grpc_c/grpc_c.h",           "grpc_c/status.h",
        "grpc_c/channel.h",          "grpc_c/client_context.h",
        "grpc_c/completion_queue.h", "grpc_c/server_context.h",
        "grpc_c/server.h",           "grpc_c/server_incoming_queue.h"};
    std::vector<grpc::string> headers(headers_strs, array_end(headers_strs));
    PrintIncludes(printer.get(), headers, params);
    printer->Print(vars, "\n");
  }
  return output;
}

grpc::string GetSourceServices(File *file, const Parameters &params) {
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
    // TODO(yifeit): hook this up to C prefix
    // TODO(yifeit): what if proto files in the dependency tree had different
    // packages
    // we are using the same prefix for all referenced type
    vars["CPrefix"] =
        grpc_cpp_generator::DotsToUnderscores(file->package()) + "_";

    // The following are Nanopb glue code. Putting them here since we're not
    // going to modify Nanopb.

    // We need to generate a declaration of serialization helper for every
    // nanopb message type we could use
    // in this file. The implementations will be scattered across different
    // service implementation files.
    auto messages = dynamic_cast<CFile *>(file)->messages();
    std::vector<grpc::string> all_message_names;
    for (auto itr = messages.begin(); itr != messages.end(); itr++) {
      all_message_names.push_back((*itr)->name());
    }
    for (int i = 0; i < file->service_count(); i++) {
      auto service = file->service(i);
      for (int j = 0; j < service->method_count(); j++) {
        auto method = service->method(j);
        all_message_names.push_back(method->input_type_name());
        all_message_names.push_back(method->output_type_name());
      }
    }
    std::set<grpc::string> dedupe_message_names(all_message_names.begin(),
                                                all_message_names.end());
    for (auto itr = dedupe_message_names.begin();
         itr != dedupe_message_names.end(); itr++) {
      std::map<grpc::string, grpc::string> vars_msg(vars);
      vars_msg["msgType"] = (*itr);
      printer->Print(
          vars_msg,
          "\n"
          "#ifdef $CPrefix$$msgType$_init_default\n"
          "GRPC_message $CPrefix$$msgType$_nanopb_serializer(const "
          "GRPC_message input);\n"
          "void $CPrefix$$msgType$_nanopb_deserializer(const GRPC_message "
          "input, void *output);\n"
          "#define GRPC_C_DECLARE_SERIALIZATION_$CPrefix$$msgType$ \\\n"
          "  $CPrefix$$msgType$_nanopb_serializer, "
          "$CPrefix$$msgType$_nanopb_deserializer\n"
          "#endif\n");
    }
    printer->Print("\n");

    // We need to generate a short serialization helper for every message type
    // This should be handled in protoc but there's nothing we can do at the
    // moment
    // given we're on nanopb.
    for (auto itr = messages.begin(); itr != messages.end(); itr++) {
      std::map<grpc::string, grpc::string> vars_msg(vars);
      vars_msg["msgType"] = (*itr)->name();
      printer->Print(vars_msg,
                     "\n"
                     "#ifdef $CPrefix$$msgType$_init_default\n"
                     "GRPC_message $CPrefix$$msgType$_nanopb_serializer(const "
                     "GRPC_message input) {\n"
                     "  return GRPC_pb_compat_generic_serializer(input, "
                     "$CPrefix$$msgType$_fields);\n"
                     "}\n"
                     "void $CPrefix$$msgType$_nanopb_deserializer(const "
                     "GRPC_message input, void *output) {\n"
                     "  return GRPC_pb_compat_generic_deserializer(input, "
                     "output, $CPrefix$$msgType$_fields);\n"
                     "}\n"
                     "#endif\n");
    }
    printer->Print("\n");

    // Print service implementations
    for (int i = 0; i < file->service_count(); ++i) {
      PrintSourceService(printer.get(), file->service(i).get(), &vars);
      printer->Print("\n");
    }
  }
  return output;
}

}  // namespace grpc_c_generator
