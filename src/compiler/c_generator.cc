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

template<class T, size_t N>
T *array_end(T (&array)[N]) { return array + N; }

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
    // Unary

    printer->Print(
      *vars,
R"(
/* Sync */
GRPC_status $CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        const $CPrefix$$Request$ request,
        $CPrefix$$Response$ *response);
)");
    printer->Print(
      *vars,
R"(
/* Async */
GRPC_client_async_response_reader *$CPrefix$$Service$_$Method$_Async(
        GRPC_client_context *const context,
        GRPC_completion_queue *cq,
        const $CPrefix$$Request$ request);

void $CPrefix$$Service$_$Method$_Finish(
        GRPC_client_async_response_reader *reader,
        $CPrefix$$Response$ *response,
        void *tag);
/* call GRPC_completion_queue_next on the cq to wait for result */

)");

  } else if (method->ClientOnlyStreaming()) {
    // Client streaming

    printer->Print(
      *vars,
      R"(
/* Sync */
GRPC_client_writer *$CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        $CPrefix$$Response$ *response);

/* Return value of true means write succeeded */
bool $CPrefix$$Service$_$Method$_Write(
        GRPC_client_writer *writer,
        $CPrefix$$Request$ request);

/* Call $CPrefix$$Service$_$Method$_Terminate to close the stream and end the call */
/* The writer is automatically freed when the request ends */
GRPC_status $CPrefix$$Service$_$Method$_Terminate(GRPC_client_writer *writer);
)");

    printer->Print(
      *vars,
      R"(
/* Async */
GRPC_client_async_writer *$CPrefix$$Service$_$Method$_Async(
        GRPC_client_context *const context,
        GRPC_completion_queue *cq);

void $CPrefix$$Service$_$Method$_Write_Async(
        GRPC_client_async_writer *writer,
        const $CPrefix$$Request$ request,
        void *tag);

void $CPrefix$$Service$_$Method$_Finish(
        GRPC_client_async_writer *writer,
        $CPrefix$$Response$ *response,
        void *tag);
/* Call GRPC_completion_queue_next on the cq to wait for result.   */
/* The writer object is automatically freed when the request ends. */

)");

  } else if (method->ServerOnlyStreaming()) {
    // Server streaming

    printer->Print(
      *vars,
R"(
/* Sync */
GRPC_client_reader *$CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        $CPrefix$$Request$ request);

/* Return value of true means read succeeded */
bool $CPrefix$$Service$_$Method$_Read(
        GRPC_client_reader *reader,
        $CPrefix$$Response$ *response);

/* Call $CPrefix$$Service$_$Method$_Terminate to close the stream and end the call */
/* The reader is automatically freed when the request ends */
GRPC_status $CPrefix$$Service$_$Method$_Terminate(GRPC_client_reader *reader);
)");
    printer->Print(
      *vars,
R"(
/* Async */
GRPC_client_async_reader *$CPrefix$$Service$_$Method$_Async(
        GRPC_client_context *const context,
        GRPC_completion_queue *cq,
        const $CPrefix$$Request$ request);

void $CPrefix$$Service$_$Method$_Read_Async(
        GRPC_client_async_reader *reader,
        $CPrefix$$Response$ *response,
        void *tag);

void $CPrefix$$Service$_$Method$_Finish(
        GRPC_client_async_reader *reader,
        void *tag);
/* call GRPC_completion_queue_next on the cq to wait for result */
/* the reader object is automatically freed when the request ends */

)");

  } else if (method->BidiStreaming()) {
    // Bidi

    printer->Print(
      *vars,
R"(
/* Sync */
GRPC_client_reader_writer *$CPrefix$$Service$_$Method$(
        GRPC_client_context *const context);

bool $CPrefix$$Service$_$Method$_Read(
        GRPC_client_reader_writer *reader_writer,
        $CPrefix$$Response$ *response);

bool $CPrefix$$Service$_$Method$_Write(
        GRPC_client_reader_writer *reader_writer,
        $CPrefix$$Request$ request);

/* Signals to the server that we are no longer sending request items */
bool $CPrefix$$Service$_$Method$_Writes_Done(GRPC_client_reader_writer *reader_writer);

/* Ends the call. The reader_writer object is automatically freed */
GRPC_status $CPrefix$$Service$_$Method$_Terminate(GRPC_client_reader_writer *reader_writer);
)");

    printer->Print(
      *vars,
      R"(
/* Async */
GRPC_client_async_reader_writer *$CPrefix$$Service$_$Method$_Async(
        GRPC_client_context *const context);

void $CPrefix$$Service$_$Method$_Read_Async(
        GRPC_client_async_reader_writer *reader_writer,
        $CPrefix$$Response$ *response,
        void *tag);

void $CPrefix$$Service$_$Method$_Write_Async(
        GRPC_client_async_reader_writer *reader_writer,
        $CPrefix$$Request$ request,
        void *tag);

void $CPrefix$$Service$_$Method$_Finish(
        GRPC_client_async_reader_writer *reader_writer,
        void *tag);
/* call GRPC_completion_queue_next on the cq to wait for result */
/* the reader-writer object is automatically freed when the request ends */

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
    (*vars)["MethodEnum"] = "NORMAL_RPC";
  } else if (method->ClientOnlyStreaming()) {
    (*vars)["MethodEnum"] = "CLIENT_STREAMING";
  } else if (method->ServerOnlyStreaming()) {
    (*vars)["MethodEnum"] = "SERVER_STREAMING";
  } else if (method->BidiStreaming()) {
    (*vars)["MethodEnum"] = "BIDI_STREAMING";
  }

  printer->Print(*vars, R"(
GRPC_method GRPC_method_$CPrefix$$Service$_$Method$ = {
  $MethodEnum$,
  "/$Package$$Service$/$Method$"
};
)");

  if (method->NoStreaming()) {
    // Unary
    printer->Print(
      *vars,
      R"(
GRPC_status $CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        const $CPrefix$$Request$ request,
        $CPrefix$$Response$ *response) {
  const GRPC_message request_msg = { &request, sizeof(request) };
  GRPC_client_context_set_serialization_impl(context,
        (grpc_serialization_impl) { $CPrefix$$Request$_serializer, $CPrefix$$Response$_deserializer });
  return GRPC_unary_blocking_call(GRPC_method_$CPrefix$$Service$_$Method$, context, request_msg, response);
}
)");
    printer->Print(
      *vars,
      R"(
/* Async */
GRPC_client_async_response_reader *$CPrefix$$Service$_$Method$_Async(
        GRPC_client_context *const context,
        GRPC_completion_queue *cq,
        const $CPrefix$$Request$ request) {
  const GRPC_message request_msg = { &request, sizeof(request) };
  GRPC_client_context_set_serialization_impl(context,
        (grpc_serialization_impl) { $CPrefix$$Request$_serializer, $CPrefix$$Response$_deserializer });
  return GRPC_unary_async_call(cq, GRPC_method_$CPrefix$$Service$_$Method$, request_msg, context);
}

void $CPrefix$$Service$_$Method$_Finish(
        GRPC_client_async_response_reader *reader,
        $CPrefix$$Response$ *response,
        void *tag) {
  GRPC_client_async_finish(reader, response, tag);
}
)");

  } else if (method->ClientOnlyStreaming()) {
    printer->Print(
      *vars,
      R"(
GRPC_client_writer *$CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        $CPrefix$$Response$ *response) {
  GRPC_client_context_set_serialization_impl(context,
        (grpc_serialization_impl) { $CPrefix$$Request$_serializer, $CPrefix$$Response$_deserializer });
  return GRPC_client_streaming_blocking_call(GRPC_method_$CPrefix$$Service$_$Method$, context, response);
}

bool $CPrefix$$Service$_$Method$_Write(
        GRPC_client_writer *writer,
        $CPrefix$$Request$ request) {
  const GRPC_message request_msg = { &request, sizeof(request) };
  return GRPC_client_streaming_blocking_write(writer, request_msg);
}

GRPC_status $CPrefix$$Service$_$Method$_Terminate(GRPC_client_writer *writer) {
  return GRPC_client_writer_terminate(writer);
}
)");

    printer->Print(
      *vars,
      R"(
/* Async TBD */
)");

  } else if (method->ServerOnlyStreaming()) {
    printer->Print(
      *vars,
      R"(
GRPC_client_reader *$CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        $CPrefix$$Request$ request) {
  const GRPC_message request_msg = { &request, sizeof(request) };
  GRPC_client_context_set_serialization_impl(context,
        (grpc_serialization_impl) { $CPrefix$$Request$_serializer, $CPrefix$$Response$_deserializer });
  return GRPC_server_streaming_blocking_call(GRPC_method_$CPrefix$$Service$_$Method$, context, request_msg);
}

bool $CPrefix$$Service$_$Method$_Read(
        GRPC_client_reader *reader,
        $CPrefix$$Response$ *response) {
  return GRPC_server_streaming_blocking_read(reader, response);
}

GRPC_status $CPrefix$$Service$_$Method$_Terminate(GRPC_client_reader *reader) {
  return GRPC_client_reader_terminate(reader);
}
)");
    printer->Print(
      *vars,
      R"(
/* Async TBD */
)");

  } else if (method->BidiStreaming()) {
    printer->Print(
      *vars,
      R"(
GRPC_client_reader_writer *$CPrefix$$Service$_$Method$(
        GRPC_client_context *const context) {
  GRPC_client_context_set_serialization_impl(context,
        (grpc_serialization_impl) { $CPrefix$$Request$_serializer, $CPrefix$$Response$_deserializer });
  return GRPC_bidi_streaming_blocking_call(GRPC_method_$CPrefix$$Service$_$Method$, context);
}

bool $CPrefix$$Service$_$Method$_Read(
        GRPC_client_reader_writer *reader_writer,
        $CPrefix$$Response$ *response) {
  return GRPC_bidi_streaming_blocking_read(reader_writer, response);
}

bool $CPrefix$$Service$_$Method$_Write(
        GRPC_client_reader_writer *reader_writer,
        $CPrefix$$Request$ request) {
  const GRPC_message request_msg = { &request, sizeof(request) };
  return GRPC_bidi_streaming_blocking_write(reader_writer, request_msg);
}

bool $CPrefix$$Service$_$Method$_Writes_Done(GRPC_client_reader_writer *reader_writer) {
  return GRPC_bidi_streaming_blocking_writes_done(reader_writer);
}

GRPC_status $CPrefix$$Service$_$Method$_Terminate(GRPC_client_reader_writer *reader_writer) {
  return GRPC_client_reader_writer_terminate(reader_writer);
}
)");
    printer->Print(
      *vars,
      R"(
/* Async TBD */
)");
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

  printer->Print(*vars, BlockifyComments("Service implementation for " + service->name() + "\n\n").c_str());
  for (int i = 0; i < service->method_count(); ++i) {
    (*vars)["Idx"] = as_string(i);
    PrintSourceClientMethod(printer, service->method(i).get(), vars);
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
    // TODO(yifeit): hook this up to C prefix
    vars["CPrefix"] = grpc_cpp_generator::DotsToUnderscores(file->package()) + "_";

    // We need to generate a short serialization helper for every message type
    // This should be handled in protoc but there's nothing we can do at the moment
    // given we're on nanopb.
    for (auto& msg : dynamic_cast<CFile*>(file)->messages()) {
      std::map<grpc::string, grpc::string> vars_msg(vars);
      vars_msg["msgType"] = msg->name();
      printer->Print(vars_msg, R"(
GRPC_message $CPrefix$$msgType$_serializer(const GRPC_message input);
void $CPrefix$$msgType$_deserializer(const GRPC_message input, void *output);
)");
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

    printer->Print(vars, "/* Message header */\n");
    printer->Print(vars, "#include \"$filename_base$$message_header_ext$\"\n");
    printer->Print(vars, "/* Other message dependencies */\n");
    // include all other service headers on which this one depends
    for (auto &depFile : dynamic_cast<CFile*>(file)->dependencies()) {
      std::map<grpc::string, grpc::string> depvars(vars);
      depvars["filename_base"] = depFile->filename_without_ext();
      depvars["service_header_ext"] = depFile->service_header_ext();
      printer->Print(depvars, "#include \"$filename_base$$service_header_ext$\"\n");
    }
    printer->Print(vars, "/* Service header */\n");
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

    grpc::string nano_encode = params.nanopb_headers_prefix + "pb_encode.h";
    grpc::string nano_decode = params.nanopb_headers_prefix + "pb_decode.h";

    static const char *headers_strs[] = {
      "grpc_c/status.h",
      "grpc_c/grpc_c.h",
      "grpc_c/channel.h",
      "grpc_c/unary_blocking_call.h",
      "grpc_c/unary_async_call.h",
      "grpc_c/client_streaming_blocking_call.h",
      "grpc_c/server_streaming_blocking_call.h",
      "grpc_c/bidi_streaming_blocking_call.h",
      "grpc_c/client_context.h",
      "grpc_c/codegen/client_context_priv.h",
      // Relying on Nanopb for Protobuf serialization for now
      nano_encode.c_str(),
      nano_decode.c_str(),
      "grpc_c/pb_compat.h"
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
      "grpc_c/status.h",
      "grpc_c/grpc_c.h",
      "grpc_c/client_context.h",
      "grpc_c/channel.h"
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
    if (!file->package().empty()) {
      vars["Package"].append(".");
    }
    // TODO(yifeit): hook this up to C prefix
    vars["CPrefix"] = grpc_cpp_generator::DotsToUnderscores(file->package()) + "_";

    // We need to generate a short serialization helper for every message type
    // This should be handled in protoc but there's nothing we can do at the moment
    // given we're on nanopb.
    for (auto& msg : dynamic_cast<CFile*>(file)->messages()) {
      std::map<grpc::string, grpc::string> vars_msg(vars);
      vars_msg["msgType"] = msg->name();
      printer->Print(vars_msg, R"(
GRPC_message $CPrefix$$msgType$_serializer(const GRPC_message input) {
  pb_ostream_t ostream = {
    .callback = GRPC_pb_compat_dynamic_array_callback,
    .state = GRPC_pb_compat_dynamic_array_alloc(),
    .max_size = SIZE_MAX
  };
  pb_encode(&ostream, $CPrefix$$msgType$_fields, input.data);
  GRPC_message msg = (GRPC_message) {
    GRPC_pb_compat_dynamic_array_get_content(ostream.state),
    ostream.bytes_written
  };
  GRPC_pb_compat_dynamic_array_free(ostream.state);
  return msg;
}
)");
      printer->Print(vars_msg, R"(
void $CPrefix$$msgType$_deserializer(const GRPC_message input, void *output) {
  pb_istream_t istream = pb_istream_from_buffer((void *) input.data, input.length);
  pb_decode(&istream, $CPrefix$$msgType$_fields, output);
}
)");
    }

    for (int i = 0; i < file->service_count(); ++i) {
      PrintSourceService(printer.get(), file->service(i).get(), &vars);
      printer->Print("\n");
    }
  }
  return output;
}

} // namespace grpc_c_generator
