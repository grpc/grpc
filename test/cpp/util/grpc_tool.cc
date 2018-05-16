/*
 *
 * Copyright 2016 gRPC authors.
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

#include "test/cpp/util/grpc_tool.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <gflags/gflags.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/string_ref.h>

#include "test/cpp/util/cli_call.h"
#include "test/cpp/util/proto_file_parser.h"
#include "test/cpp/util/proto_reflection_descriptor_database.h"
#include "test/cpp/util/service_describer.h"

#if GPR_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

namespace grpc {
namespace testing {

DEFINE_bool(l, false, "Use a long listing format");
DEFINE_bool(remotedb, true, "Use server types to parse and format messages");
DEFINE_string(metadata, "",
              "Metadata to send to server, in the form of key1:val1:key2:val2");
DEFINE_string(proto_path, ".", "Path to look for the proto file.");
DEFINE_string(protofiles, "", "Name of the proto file.");
DEFINE_bool(binary_input, false, "Input in binary format");
DEFINE_bool(binary_output, false, "Output in binary format");
DEFINE_string(infile, "", "Input file (default is stdin)");
DEFINE_bool(batch, false,
            "Input contains multiple requests. Please do not use this to send "
            "more than a few RPCs. gRPC CLI has very different performance "
            "characteristics compared with normal RPC calls which make it "
            "unsuitable for loadtesting or significant production traffic.");

namespace {

class GrpcTool {
 public:
  explicit GrpcTool();
  virtual ~GrpcTool() {}

  bool Help(int argc, const char** argv, const CliCredentials& cred,
            GrpcToolOutputCallback callback);
  bool CallMethod(int argc, const char** argv, const CliCredentials& cred,
                  GrpcToolOutputCallback callback);
  bool ListServices(int argc, const char** argv, const CliCredentials& cred,
                    GrpcToolOutputCallback callback);
  bool PrintType(int argc, const char** argv, const CliCredentials& cred,
                 GrpcToolOutputCallback callback);
  // TODO(zyc): implement the following methods
  // bool ListServices(int argc, const char** argv, GrpcToolOutputCallback
  // callback);
  // bool PrintTypeId(int argc, const char** argv, GrpcToolOutputCallback
  // callback);
  bool ParseMessage(int argc, const char** argv, const CliCredentials& cred,
                    GrpcToolOutputCallback callback);
  bool ToText(int argc, const char** argv, const CliCredentials& cred,
              GrpcToolOutputCallback callback);
  bool ToBinary(int argc, const char** argv, const CliCredentials& cred,
                GrpcToolOutputCallback callback);

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
  const char delim = ':';
  const char escape = '\\';
  size_t cur = -1;
  std::stringstream ss;
  while (++cur < FLAGS_metadata.length()) {
    switch (FLAGS_metadata.at(cur)) {
      case escape:
        if (cur < FLAGS_metadata.length() - 1) {
          char c = FLAGS_metadata.at(++cur);
          if (c == delim || c == escape) {
            ss << c;
            continue;
          }
        }
        fprintf(stderr, "Failed to parse metadata flag.\n");
        exit(1);
      case delim:
        fields.push_back(ss.str());
        ss.str("");
        ss.clear();
        break;
      default:
        ss << FLAGS_metadata.at(cur);
    }
  }
  fields.push_back(ss.str());
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

void ReadResponse(CliCall* call, const grpc::string& method_name,
                  GrpcToolOutputCallback callback, ProtoFileParser* parser,
                  gpr_mu* parser_mu, bool print_mode) {
  grpc::string serialized_response_proto;
  std::multimap<grpc::string_ref, grpc::string_ref> server_initial_metadata;

  for (bool receive_initial_metadata = true; call->ReadAndMaybeNotifyWrite(
           &serialized_response_proto,
           receive_initial_metadata ? &server_initial_metadata : nullptr);
       receive_initial_metadata = false) {
    fprintf(stderr, "got response.\n");
    if (!FLAGS_binary_output) {
      gpr_mu_lock(parser_mu);
      serialized_response_proto = parser->GetTextFormatFromMethod(
          method_name, serialized_response_proto, false /* is_request */);
      if (parser->HasError() && print_mode) {
        fprintf(stderr, "Failed to parse response.\n");
      }
      gpr_mu_unlock(parser_mu);
    }
    if (receive_initial_metadata) {
      PrintMetadata(server_initial_metadata,
                    "Received initial metadata from server:");
    }
    if (!callback(serialized_response_proto) && print_mode) {
      fprintf(stderr, "Failed to output response.\n");
    }
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
    {"ls", BindWith5Args(&GrpcTool::ListServices), 1, 3},
    {"list", BindWith5Args(&GrpcTool::ListServices), 1, 3},
    {"call", BindWith5Args(&GrpcTool::CallMethod), 2, 3},
    {"type", BindWith5Args(&GrpcTool::PrintType), 2, 2},
    {"parse", BindWith5Args(&GrpcTool::ParseMessage), 2, 3},
    {"totext", BindWith5Args(&GrpcTool::ToText), 2, 3},
    {"tobinary", BindWith5Args(&GrpcTool::ToBinary), 2, 3},
};

void Usage(const grpc::string& msg) {
  fprintf(
      stderr,
      "%s\n"
      "  grpc_cli ls ...         ; List services\n"
      "  grpc_cli call ...       ; Call method\n"
      "  grpc_cli type ...       ; Print type\n"
      "  grpc_cli parse ...      ; Parse message\n"
      "  grpc_cli totext ...     ; Convert binary message to text\n"
      "  grpc_cli tobinary ...   ; Convert text message to binary\n"
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
  return nullptr;
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
  if (cmd != nullptr) {
    GrpcTool grpc_tool;
    if (argc < cmd->min_args || argc > cmd->max_args) {
      // Force the command to print its usage message
      fprintf(stderr, "\nWrong number of arguments for %s\n", command.c_str());
      grpc_tool.SetPrintCommandMode(1);
      return cmd->function(&grpc_tool, -1, nullptr, cred, callback);
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
    if (cmd == nullptr) {
      Usage("Unknown command '" + grpc::string(argv[0]) + "'");
    }
    SetPrintCommandMode(0);
    cmd->function(this, -1, nullptr, cred, callback);
  }
  return true;
}

bool GrpcTool::ListServices(int argc, const char** argv,
                            const CliCredentials& cred,
                            GrpcToolOutputCallback callback) {
  CommandUsage(
      "List services\n"
      "  grpc_cli ls <address> [<service>[/<method>]]\n"
      "    <address>                ; host:port\n"
      "    <service>                ; Exported service name\n"
      "    <method>                 ; Method name\n"
      "    --l                      ; Use a long listing format\n"
      "    --outfile                ; Output filename (defaults to stdout)\n" +
      cred.GetCredentialUsage());

  grpc::string server_address(argv[0]);
  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateChannel(server_address, cred.GetCredentials());
  grpc::ProtoReflectionDescriptorDatabase desc_db(channel);
  grpc::protobuf::DescriptorPool desc_pool(&desc_db);

  std::vector<grpc::string> service_list;
  if (!desc_db.GetServices(&service_list)) {
    fprintf(stderr, "Received an error when querying services endpoint.\n");
    return false;
  }

  // If no service is specified, dump the list of services.
  grpc::string output;
  if (argc < 2) {
    // List all services, if --l is passed, then include full description,
    // otherwise include a summarized list only.
    if (FLAGS_l) {
      output = DescribeServiceList(service_list, desc_pool);
    } else {
      for (auto it = service_list.begin(); it != service_list.end(); it++) {
        auto const& service = *it;
        output.append(service);
        output.append("\n");
      }
    }
  } else {
    grpc::string service_name;
    grpc::string method_name;
    std::stringstream ss(argv[1]);

    // Remove leading slashes.
    while (ss.peek() == '/') {
      ss.get();
    }

    // Parse service and method names. Support the following patterns:
    //   Service
    //   Service Method
    //   Service.Method
    //   Service/Method
    if (argc == 3) {
      std::getline(ss, service_name, '/');
      method_name = argv[2];
    } else {
      if (std::getline(ss, service_name, '/')) {
        std::getline(ss, method_name);
      }
    }

    const grpc::protobuf::ServiceDescriptor* service =
        desc_pool.FindServiceByName(service_name);
    if (service != nullptr) {
      if (method_name.empty()) {
        output = FLAGS_l ? DescribeService(service) : SummarizeService(service);
      } else {
        method_name.insert(0, 1, '.');
        method_name.insert(0, service_name);
        const grpc::protobuf::MethodDescriptor* method =
            desc_pool.FindMethodByName(method_name);
        if (method != nullptr) {
          output = FLAGS_l ? DescribeMethod(method) : SummarizeMethod(method);
        } else {
          fprintf(stderr, "Method %s not found in service %s.\n",
                  method_name.c_str(), service_name.c_str());
          return false;
        }
      }
    } else {
      if (!method_name.empty()) {
        fprintf(stderr, "Service %s not found.\n", service_name.c_str());
        return false;
      } else {
        const grpc::protobuf::MethodDescriptor* method =
            desc_pool.FindMethodByName(service_name);
        if (method != nullptr) {
          output = FLAGS_l ? DescribeMethod(method) : SummarizeMethod(method);
        } else {
          fprintf(stderr, "Service or method %s not found.\n",
                  service_name.c_str());
          return false;
        }
      }
    }
  }
  return callback(output);
}

bool GrpcTool::PrintType(int argc, const char** argv,
                         const CliCredentials& cred,
                         GrpcToolOutputCallback callback) {
  CommandUsage(
      "Print type\n"
      "  grpc_cli type <address> <type>\n"
      "    <address>                ; host:port\n"
      "    <type>                   ; Protocol buffer type name\n" +
      cred.GetCredentialUsage());

  grpc::string server_address(argv[0]);
  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateChannel(server_address, cred.GetCredentials());
  grpc::ProtoReflectionDescriptorDatabase desc_db(channel);
  grpc::protobuf::DescriptorPool desc_pool(&desc_db);

  grpc::string output;
  const grpc::protobuf::Descriptor* descriptor =
      desc_pool.FindMessageTypeByName(argv[1]);
  if (descriptor != nullptr) {
    output = descriptor->DebugString();
  } else {
    fprintf(stderr, "Type %s not found.\n", argv[1]);
    return false;
  }
  return callback(output);
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
  grpc::string formatted_method_name;
  std::unique_ptr<ProtoFileParser> parser;
  grpc::string serialized_request_proto;
  bool print_mode = false;

  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateChannel(server_address, cred.GetCredentials());

  parser.reset(new grpc::testing::ProtoFileParser(
      FLAGS_remotedb ? channel : nullptr, FLAGS_proto_path, FLAGS_protofiles));

  if (FLAGS_binary_input) {
    formatted_method_name = method_name;
  } else {
    formatted_method_name = parser->GetFormattedMethodName(method_name);
  }

  if (parser->HasError()) {
    return false;
  }

  if (argc == 3) {
    request_text = argv[2];
  }

  if (parser->IsStreaming(method_name, true /* is_request */)) {
    std::istream* input_stream;
    std::ifstream input_file;

    if (FLAGS_batch) {
      fprintf(stderr, "Batch mode for streaming RPC is not supported.\n");
      return false;
    }

    std::multimap<grpc::string, grpc::string> client_metadata;
    ParseMetadataFlag(&client_metadata);
    PrintMetadata(client_metadata, "Sending client initial metadata:");

    CliCall call(channel, formatted_method_name, client_metadata);

    if (FLAGS_infile.empty()) {
      if (isatty(fileno(stdin))) {
        print_mode = true;
        fprintf(stderr, "reading streaming request message from stdin...\n");
      }
      input_stream = &std::cin;
    } else {
      input_file.open(FLAGS_infile, std::ios::in | std::ios::binary);
      input_stream = &input_file;
    }

    gpr_mu parser_mu;
    gpr_mu_init(&parser_mu);
    std::thread read_thread(ReadResponse, &call, method_name, callback,
                            parser.get(), &parser_mu, print_mode);

    std::stringstream request_ss;
    grpc::string line;
    while (!request_text.empty() ||
           (!input_stream->eof() && getline(*input_stream, line))) {
      if (!request_text.empty()) {
        if (FLAGS_binary_input) {
          serialized_request_proto = request_text;
          request_text.clear();
        } else {
          gpr_mu_lock(&parser_mu);
          serialized_request_proto = parser->GetSerializedProtoFromMethod(
              method_name, request_text, true /* is_request */);
          request_text.clear();
          if (parser->HasError()) {
            if (print_mode) {
              fprintf(stderr, "Failed to parse request.\n");
            }
            gpr_mu_unlock(&parser_mu);
            continue;
          }
          gpr_mu_unlock(&parser_mu);
        }

        call.WriteAndWait(serialized_request_proto);
        if (print_mode) {
          fprintf(stderr, "Request sent.\n");
        }
      } else {
        if (line.length() == 0) {
          request_text = request_ss.str();
          request_ss.str(grpc::string());
          request_ss.clear();
        } else {
          request_ss << line << ' ';
        }
      }
    }
    if (input_file.is_open()) {
      input_file.close();
    }

    call.WritesDoneAndWait();
    read_thread.join();

    std::multimap<grpc::string_ref, grpc::string_ref> server_trailing_metadata;
    Status status = call.Finish(&server_trailing_metadata);
    PrintMetadata(server_trailing_metadata,
                  "Received trailing metadata from server:");

    if (status.ok()) {
      fprintf(stderr, "Stream RPC succeeded with OK status\n");
      return true;
    } else {
      fprintf(stderr, "Rpc failed with status code %d, error message: %s\n",
              status.error_code(), status.error_message().c_str());
      return false;
    }

  } else {  // parser->IsStreaming(method_name, true /* is_request */)
    if (FLAGS_batch) {
      if (parser->IsStreaming(method_name, false /* is_request */)) {
        fprintf(stderr, "Batch mode for streaming RPC is not supported.\n");
        return false;
      }

      std::istream* input_stream;
      std::ifstream input_file;

      if (FLAGS_infile.empty()) {
        if (isatty(fileno(stdin))) {
          print_mode = true;
          fprintf(stderr, "reading request messages from stdin...\n");
        }
        input_stream = &std::cin;
      } else {
        input_file.open(FLAGS_infile, std::ios::in | std::ios::binary);
        input_stream = &input_file;
      }

      std::multimap<grpc::string, grpc::string> client_metadata;
      ParseMetadataFlag(&client_metadata);
      if (print_mode) {
        PrintMetadata(client_metadata, "Sending client initial metadata:");
      }

      std::stringstream request_ss;
      grpc::string line;
      while (!request_text.empty() ||
             (!input_stream->eof() && getline(*input_stream, line))) {
        if (!request_text.empty()) {
          if (FLAGS_binary_input) {
            serialized_request_proto = request_text;
            request_text.clear();
          } else {
            serialized_request_proto = parser->GetSerializedProtoFromMethod(
                method_name, request_text, true /* is_request */);
            request_text.clear();
            if (parser->HasError()) {
              if (print_mode) {
                fprintf(stderr, "Failed to parse request.\n");
              }
              continue;
            }
          }

          grpc::string serialized_response_proto;
          std::multimap<grpc::string_ref, grpc::string_ref>
              server_initial_metadata, server_trailing_metadata;
          CliCall call(channel, formatted_method_name, client_metadata);
          call.Write(serialized_request_proto);
          call.WritesDone();
          if (!call.Read(&serialized_response_proto,
                         &server_initial_metadata)) {
            fprintf(stderr, "Failed to read response.\n");
          }
          Status status = call.Finish(&server_trailing_metadata);

          if (status.ok()) {
            if (print_mode) {
              fprintf(stderr, "Rpc succeeded with OK status.\n");
              PrintMetadata(server_initial_metadata,
                            "Received initial metadata from server:");
              PrintMetadata(server_trailing_metadata,
                            "Received trailing metadata from server:");
            }

            if (FLAGS_binary_output) {
              if (!callback(serialized_response_proto)) {
                break;
              }
            } else {
              grpc::string response_text = parser->GetTextFormatFromMethod(
                  method_name, serialized_response_proto,
                  false /* is_request */);
              if (parser->HasError() && print_mode) {
                fprintf(stderr, "Failed to parse response.\n");
              } else {
                if (!callback(response_text)) {
                  break;
                }
              }
            }
          } else {
            if (print_mode) {
              fprintf(stderr,
                      "Rpc failed with status code %d, error message: %s\n",
                      status.error_code(), status.error_message().c_str());
            }
          }
        } else {
          if (line.length() == 0) {
            request_text = request_ss.str();
            request_ss.str(grpc::string());
            request_ss.clear();
          } else {
            request_ss << line << ' ';
          }
        }
      }

      if (input_file.is_open()) {
        input_file.close();
      }

      return true;
    }

    if (argc == 3) {
      if (!FLAGS_infile.empty()) {
        fprintf(stderr, "warning: request given in argv, ignoring --infile\n");
      }
    } else {
      std::stringstream input_stream;
      if (FLAGS_infile.empty()) {
        if (isatty(fileno(stdin))) {
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

    CliCall call(channel, formatted_method_name, client_metadata);
    call.Write(serialized_request_proto);
    call.WritesDone();

    for (bool receive_initial_metadata = true; call.Read(
             &serialized_response_proto,
             receive_initial_metadata ? &server_initial_metadata : nullptr);
         receive_initial_metadata = false) {
      if (!FLAGS_binary_output) {
        serialized_response_proto = parser->GetTextFormatFromMethod(
            method_name, serialized_response_proto, false /* is_request */);
        if (parser->HasError()) {
          return false;
        }
      }
      if (receive_initial_metadata) {
        PrintMetadata(server_initial_metadata,
                      "Received initial metadata from server:");
      }
      if (!callback(serialized_response_proto)) {
        return false;
      }
    }
    Status status = call.Finish(&server_trailing_metadata);
    PrintMetadata(server_trailing_metadata,
                  "Received trailing metadata from server:");
    if (status.ok()) {
      fprintf(stderr, "Rpc succeeded with OK status\n");
      return true;
    } else {
      fprintf(stderr, "Rpc failed with status code %d, error message: %s\n",
              status.error_code(), status.error_message().c_str());
      return false;
    }
  }
  GPR_UNREACHABLE_CODE(return false);
}

bool GrpcTool::ParseMessage(int argc, const char** argv,
                            const CliCredentials& cred,
                            GrpcToolOutputCallback callback) {
  CommandUsage(
      "Parse message\n"
      "  grpc_cli parse <address> <type> [<message>]\n"
      "    <address>                ; host:port\n"
      "    <type>                   ; Protocol buffer type name\n"
      "    <message>                ; Text protobuffer (overrides --infile)\n"
      "    --protofiles             ; Comma separated proto files used as a"
      " fallback when parsing request/response\n"
      "    --proto_path             ; The search path of proto files, valid"
      " only when --protofiles is given\n"
      "    --infile                 ; Input filename (defaults to stdin)\n"
      "    --outfile                ; Output filename (defaults to stdout)\n"
      "    --binary_input           ; Input in binary format\n"
      "    --binary_output          ; Output in binary format\n" +
      cred.GetCredentialUsage());

  std::stringstream output_ss;
  grpc::string message_text;
  grpc::string server_address(argv[0]);
  grpc::string type_name(argv[1]);
  std::unique_ptr<grpc::testing::ProtoFileParser> parser;
  grpc::string serialized_request_proto;

  if (argc == 3) {
    message_text = argv[2];
    if (!FLAGS_infile.empty()) {
      fprintf(stderr, "warning: message given in argv, ignoring --infile.\n");
    }
  } else {
    std::stringstream input_stream;
    if (FLAGS_infile.empty()) {
      if (isatty(fileno(stdin))) {
        fprintf(stderr, "reading request message from stdin...\n");
      }
      input_stream << std::cin.rdbuf();
    } else {
      std::ifstream input_file(FLAGS_infile, std::ios::in | std::ios::binary);
      input_stream << input_file.rdbuf();
      input_file.close();
    }
    message_text = input_stream.str();
  }

  if (!FLAGS_binary_input || !FLAGS_binary_output) {
    std::shared_ptr<grpc::Channel> channel =
        grpc::CreateChannel(server_address, cred.GetCredentials());
    parser.reset(
        new grpc::testing::ProtoFileParser(FLAGS_remotedb ? channel : nullptr,
                                           FLAGS_proto_path, FLAGS_protofiles));
    if (parser->HasError()) {
      return false;
    }
  }

  if (FLAGS_binary_input) {
    serialized_request_proto = message_text;
  } else {
    serialized_request_proto =
        parser->GetSerializedProtoFromMessageType(type_name, message_text);
    if (parser->HasError()) {
      return false;
    }
  }

  if (FLAGS_binary_output) {
    output_ss << serialized_request_proto;
  } else {
    grpc::string output_text = parser->GetTextFormatFromMessageType(
        type_name, serialized_request_proto);
    if (parser->HasError()) {
      return false;
    }
    output_ss << output_text << std::endl;
  }

  return callback(output_ss.str());
}

bool GrpcTool::ToText(int argc, const char** argv, const CliCredentials& cred,
                      GrpcToolOutputCallback callback) {
  CommandUsage(
      "Convert binary message to text\n"
      "  grpc_cli totext <protofiles> <type>\n"
      "    <protofiles>             ; Comma separated list of proto files\n"
      "    <type>                   ; Protocol buffer type name\n"
      "    --proto_path             ; The search path of proto files\n"
      "    --infile                 ; Input filename (defaults to stdin)\n"
      "    --outfile                ; Output filename (defaults to stdout)\n");

  FLAGS_protofiles = argv[0];
  FLAGS_remotedb = false;
  FLAGS_binary_input = true;
  FLAGS_binary_output = false;
  return ParseMessage(argc, argv, cred, callback);
}

bool GrpcTool::ToBinary(int argc, const char** argv, const CliCredentials& cred,
                        GrpcToolOutputCallback callback) {
  CommandUsage(
      "Convert text message to binary\n"
      "  grpc_cli tobinary <protofiles> <type> [<message>]\n"
      "    <protofiles>             ; Comma separated list of proto files\n"
      "    <type>                   ; Protocol buffer type name\n"
      "    --proto_path             ; The search path of proto files\n"
      "    --infile                 ; Input filename (defaults to stdin)\n"
      "    --outfile                ; Output filename (defaults to stdout)\n");

  FLAGS_protofiles = argv[0];
  FLAGS_remotedb = false;
  FLAGS_binary_input = false;
  FLAGS_binary_output = true;
  return ParseMessage(argc, argv, cred, callback);
}

}  // namespace testing
}  // namespace grpc
