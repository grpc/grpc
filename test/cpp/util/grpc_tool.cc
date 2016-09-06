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

#include "test/cpp/util/grpc_tool.h"

#include <unistd.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <gflags/gflags.h>
#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
#include <grpc++/grpc++.h>
#include <grpc++/security/credentials.h>
#include <grpc++/support/string_ref.h>
#include <grpc/grpc.h>

#include "test/cpp/util/cli_call.h"
#include "test/cpp/util/proto_file_parser.h"
#include "test/cpp/util/proto_reflection_descriptor_database.h"
#include "test/cpp/util/test_config.h"

DEFINE_bool(remotedb, true, "Use server types to parse and format messages");
DEFINE_string(metadata, "",
              "Metadata to send to server, in the form of key1:val1:key2:val2");
DEFINE_string(proto_path, ".", "Path to look for the proto file.");
DEFINE_string(protofiles, "", "Name of the proto file.");
DEFINE_bool(binary_input, false, "Input in binary format");
DEFINE_bool(binary_output, false, "Output in binary format");
DEFINE_string(infile, "", "Input file (default is stdin)");

namespace grpc {
namespace testing {
namespace {

class GrpcTool {
 public:
  explicit GrpcTool();
  virtual ~GrpcTool() {}

  bool Help(int argc, const char** argv, const CliCredentials& cred,
            GrpcToolOutputCallback callback);
  bool CallMethod(int argc, const char** argv, const CliCredentials& cred,
                  GrpcToolOutputCallback callback);
  // TODO(zyc): implement the following methods
  // bool ListServices(int argc, const char** argv, GrpcToolOutputCallback
  // callback);
  // bool PrintType(int argc, const char** argv, GrpcToolOutputCallback
  // callback);
  // bool PrintTypeId(int argc, const char** argv, GrpcToolOutputCallback
  // callback);
  // bool ParseMessage(int argc, const char** argv, GrpcToolOutputCallback
  // callback);
  // bool ToText(int argc, const char** argv, GrpcToolOutputCallback callback);
  // bool ToBinary(int argc, const char** argv, GrpcToolOutputCallback
  // callback);

  void SetPrintCommandMode(int exit_status) {
    print_command_usage_ = true;
    usage_exit_status_ = exit_status;
  }

 private:
  void CommandUsage(const grpc::string& usage) const;
  bool print_command_usage_;
  int usage_exit_status_;
  const grpc::string cred_usage_;
};

template <typename T>
std::function<bool(GrpcTool*, int, const char**, const CliCredentials&,
                   GrpcToolOutputCallback)>
BindWith5Args(T&& func) {
  return std::bind(std::forward<T>(func), std::placeholders::_1,
                   std::placeholders::_2, std::placeholders::_3,
                   std::placeholders::_4, std::placeholders::_5);
}

template <typename T>
size_t ArraySize(T& a) {
  return ((sizeof(a) / sizeof(*(a))) /
          static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))));
}

void ParseMetadataFlag(
    std::multimap<grpc::string, grpc::string>* client_metadata) {
  if (FLAGS_metadata.empty()) {
    return;
  }
  std::vector<grpc::string> fields;
  const char* delim = ":";
  size_t cur, next = -1;
  do {
    cur = next + 1;
    next = FLAGS_metadata.find_first_of(delim, cur);
    fields.push_back(FLAGS_metadata.substr(cur, next - cur));
  } while (next != grpc::string::npos);
  if (fields.size() % 2) {
    fprintf(stderr, "Failed to parse metadata flag.\n");
    exit(1);
  }
  for (size_t i = 0; i < fields.size(); i += 2) {
    client_metadata->insert(
        std::pair<grpc::string, grpc::string>(fields[i], fields[i + 1]));
  }
}

template <typename T>
void PrintMetadata(const T& m, const grpc::string& message) {
  if (m.empty()) {
    return;
  }
  fprintf(stderr, "%s\n", message.c_str());
  grpc::string pair;
  for (typename T::const_iterator iter = m.begin(); iter != m.end(); ++iter) {
    pair.clear();
    pair.append(iter->first.data(), iter->first.size());
    pair.append(" : ");
    pair.append(iter->second.data(), iter->second.size());
    fprintf(stderr, "%s\n", pair.c_str());
  }
}

struct Command {
  const char* command;
  std::function<bool(GrpcTool*, int, const char**, const CliCredentials&,
                     GrpcToolOutputCallback)>
      function;
  int min_args;
  int max_args;
};

const Command ops[] = {
    {"help", BindWith5Args(&GrpcTool::Help), 0, INT_MAX},
    // {"ls", BindWith5Args(&GrpcTool::ListServices), 1, 3},
    // {"list", BindWith5Args(&GrpcTool::ListServices), 1, 3},
    {"call", BindWith5Args(&GrpcTool::CallMethod), 2, 3},
    // {"type", BindWith5Args(&GrpcTool::PrintType), 2, 2},
    // {"parse", BindWith5Args(&GrpcTool::ParseMessage), 2, 3},
    // {"totext", BindWith5Args(&GrpcTool::ToText), 2, 3},
    // {"tobinary", BindWith5Args(&GrpcTool::ToBinary), 2, 3},
};

void Usage(const grpc::string& msg) {
  fprintf(
      stderr,
      "%s\n"
      // "  grpc_cli ls ...         ; List services\n"
      "  grpc_cli call ...       ; Call method\n"
      // "  grpc_cli type ...       ; Print type\n"
      // "  grpc_cli parse ...      ; Parse message\n"
      // "  grpc_cli totext ...     ; Convert binary message to text\n"
      // "  grpc_cli tobinary ...   ; Convert text message to binary\n"
      "  grpc_cli help ...       ; Print this message, or per-command usage\n"
      "\n",
      msg.c_str());

  exit(1);
}

const Command* FindCommand(const grpc::string& name) {
  for (int i = 0; i < (int)ArraySize(ops); i++) {
    if (name == ops[i].command) {
      return &ops[i];
    }
  }
  return NULL;
}
}  // namespace

int GrpcToolMainLib(int argc, const char** argv, const CliCredentials& cred,
                    GrpcToolOutputCallback callback) {
  if (argc < 2) {
    Usage("No command specified");
  }

  grpc::string command = argv[1];
  argc -= 2;
  argv += 2;

  const Command* cmd = FindCommand(command);
  if (cmd != NULL) {
    GrpcTool grpc_tool;
    if (argc < cmd->min_args || argc > cmd->max_args) {
      // Force the command to print its usage message
      fprintf(stderr, "\nWrong number of arguments for %s\n", command.c_str());
      grpc_tool.SetPrintCommandMode(1);
      return cmd->function(&grpc_tool, -1, NULL, cred, callback);
    }
    const bool ok = cmd->function(&grpc_tool, argc, argv, cred, callback);
    return ok ? 0 : 1;
  } else {
    Usage("Invalid command '" + grpc::string(command.c_str()) + "'");
  }
  return 1;
}

GrpcTool::GrpcTool() : print_command_usage_(false), usage_exit_status_(0) {}

void GrpcTool::CommandUsage(const grpc::string& usage) const {
  if (print_command_usage_) {
    fprintf(stderr, "\n%s%s\n", usage.c_str(),
            (usage.empty() || usage[usage.size() - 1] != '\n') ? "\n" : "");
    exit(usage_exit_status_);
  }
}

bool GrpcTool::Help(int argc, const char** argv, const CliCredentials& cred,
                    GrpcToolOutputCallback callback) {
  CommandUsage(
      "Print help\n"
      "  grpc_cli help [subcommand]\n");

  if (argc == 0) {
    Usage("");
  } else {
    const Command* cmd = FindCommand(argv[0]);
    if (cmd == NULL) {
      Usage("Unknown command '" + grpc::string(argv[0]) + "'");
    }
    SetPrintCommandMode(0);
    cmd->function(this, -1, NULL, cred, callback);
  }
  return true;
}

bool GrpcTool::CallMethod(int argc, const char** argv,
                          const CliCredentials& cred,
                          GrpcToolOutputCallback callback) {
  CommandUsage(
      "Call method\n"
      "  grpc_cli call <address> <service>[.<method>] <request>\n"
      "    <address>                ; host:port\n"
      "    <service>                ; Exported service name\n"
      "    <method>                 ; Method name\n"
      "    <request>                ; Text protobuffer (overrides infile)\n"
      "    --protofiles             ; Comma separated proto files used as a"
      " fallback when parsing request/response\n"
      "    --proto_path             ; The search path of proto files, valid"
      " only when --protofiles is given\n"
      "    --metadata               ; The metadata to be sent to the server\n"
      "    --infile                 ; Input filename (defaults to stdin)\n"
      "    --outfile                ; Output filename (defaults to stdout)\n"
      "    --binary_input           ; Input in binary format\n"
      "    --binary_output          ; Output in binary format\n" +
      cred.GetCredentialUsage());

  std::stringstream output_ss;
  grpc::string request_text;
  grpc::string server_address(argv[0]);
  grpc::string method_name(argv[1]);
  std::unique_ptr<grpc::testing::ProtoFileParser> parser;
  grpc::string serialized_request_proto;

  if (argc == 3) {
    request_text = argv[2];
    if (!FLAGS_infile.empty()) {
      fprintf(stderr, "warning: request given in argv, ignoring --infile\n");
    }
  } else {
    std::stringstream input_stream;
    if (FLAGS_infile.empty()) {
      if (isatty(STDIN_FILENO)) {
        fprintf(stderr, "reading request message from stdin...\n");
      }
      input_stream << std::cin.rdbuf();
    } else {
      std::ifstream input_file(FLAGS_infile, std::ios::in | std::ios::binary);
      input_stream << input_file.rdbuf();
      input_file.close();
    }
    request_text = input_stream.str();
  }

  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateChannel(server_address, cred.GetCredentials());
  if (!FLAGS_binary_input || !FLAGS_binary_output) {
    parser.reset(
        new grpc::testing::ProtoFileParser(FLAGS_remotedb ? channel : nullptr,
                                           FLAGS_proto_path, FLAGS_protofiles));
    if (parser->HasError()) {
      return false;
    }
  }

  if (FLAGS_binary_input) {
    serialized_request_proto = request_text;
  } else {
    serialized_request_proto = parser->GetSerializedProtoFromMethod(
        method_name, request_text, true /* is_request */);
    if (parser->HasError()) {
      return false;
    }
  }
  fprintf(stderr, "connecting to %s\n", server_address.c_str());

  grpc::string serialized_response_proto;
  std::multimap<grpc::string, grpc::string> client_metadata;
  std::multimap<grpc::string_ref, grpc::string_ref> server_initial_metadata,
      server_trailing_metadata;
  ParseMetadataFlag(&client_metadata);
  PrintMetadata(client_metadata, "Sending client initial metadata:");
  grpc::Status status = grpc::testing::CliCall::Call(
      channel, parser->GetFormatedMethodName(method_name),
      serialized_request_proto, &serialized_response_proto, client_metadata,
      &server_initial_metadata, &server_trailing_metadata);
  PrintMetadata(server_initial_metadata,
                "Received initial metadata from server:");
  PrintMetadata(server_trailing_metadata,
                "Received trailing metadata from server:");
  if (status.ok()) {
    fprintf(stderr, "Rpc succeeded with OK status\n");
    if (FLAGS_binary_output) {
      output_ss << serialized_response_proto;
    } else {
      grpc::string response_text = parser->GetTextFormatFromMethod(
          method_name, serialized_response_proto, false /* is_request */);
      if (parser->HasError()) {
        return false;
      }
      output_ss << "Response: \n " << response_text << std::endl;
    }
  } else {
    fprintf(stderr, "Rpc failed with status code %d, error message: %s\n",
            status.error_code(), status.error_message().c_str());
  }

  return callback(output_ss.str());
}

}  // namespace testing
}  // namespace grpc
