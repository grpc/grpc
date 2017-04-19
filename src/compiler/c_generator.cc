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
GRPC_client_writer $CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        $CPrefix$$Response$ *response);

/* Return value of true means write succeeded */
bool $CPrefix$$Service$_$Method$_Write(
        GRPC_client_writer *writer,
        $CPrefix$$Request$ request);

/* Terminating the writer takes care of ending the call, freeing the writer. */
/* Returns call status in the context object. */
void GRPC_client_writer_terminate(GRPC_client_writer *writer);
)");

    printer->Print(
      *vars,
      R"(
/* Async */
GRPC_client_async_writer *$CPrefix$$Service$_$Method$_Async(
        GRPC_channel *channel,
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
/* call GRPC_completion_queue_next on the cq to wait for result   */
/* the writer object is automatically freed when the request ends */
)");

  } else if (method->ServerOnlyStreaming()) {
    // Server streaming

    printer->Print(
      *vars,
R"(
/* Sync */
GRPC_client_reader $CPrefix$$Service$_$Method$(
        GRPC_client_context *const context,
        $CPrefix$$Request$ request);

/* Return value of true means write succeeded */
bool $CPrefix$$Service$_$Method$_Read(
        GRPC_client_reader *reader,
        $CPrefix$$Response$ *response);
)");
    printer->Print(
      *vars,
R"(
/* Async */
GRPC_client_async_reader *$CPrefix$$Service$_$Method$_Async(
        GRPC_channel *channel,
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
/* the writer object is automatically freed when the request ends */
)");

  } else if (method->BidiStreaming()) {
    // Bidi

    printer->Print(
      *vars,
R"(
/* Sync */
GRPC_client_reader_writer *$CPrefix$$Service$_$Method$(
        GRPC_channel *channel,
        GRPC_client_context *const context);

bool $CPrefix$$Service$_$Method$_Read(
        GRPC_client_reader_writer *reader_writer,
        $CPrefix$$Response$ *response);

bool $CPrefix$$Service$_$Method$_Write(
        GRPC_client_reader_writer *reader_writer,
        $CPrefix$$Request$ request);
)");

    printer->Print(
      *vars,
      R"(
/* Async */
GRPC_client_async_reader_writer *$CPrefix$$Service$_$Method$_Async(
        GRPC_channel *channel,
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

  } else if (method->ClientOnlyStreaming()) {

  } else if (method->ServerOnlyStreaming()) {

  } else if (method->BidiStreaming()) {

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
      "grpc_c/client_context.h"
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
      "grpc_c/client_context.h"
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
