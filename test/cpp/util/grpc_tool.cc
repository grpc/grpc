//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "test/cpp/util/grpc_tool.h"

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/string_ref.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "test/cpp/util/cli_call.h"
#include "test/cpp/util/proto_file_parser.h"
#include "test/cpp/util/proto_reflection_descriptor_database.h"
#include "test/cpp/util/service_describer.h"

#if GPR_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

ABSL_FLAG(bool, l, false, "Use a long listing format");
ABSL_FLAG(bool, remotedb, true,
          "Use server types to parse and format messages");
ABSL_FLAG(std::string, metadata, "",
          "Metadata to send to server, in the form of key1:val1:key2:val2");
ABSL_FLAG(std::string, proto_path, ".",
          "Path to look for the proto file. "
          "Multiple paths can be separated by " GRPC_CLI_PATH_SEPARATOR);
ABSL_FLAG(std::string, protofiles, "", "Name of the proto file.");
ABSL_FLAG(bool, binary_input, false, "Input in binary format");
ABSL_FLAG(bool, binary_output, false, "Output in binary format");
ABSL_FLAG(std::string, default_service_config, "",
          "Default service config to use on the channel, if non-empty. Note "
          "that this will be ignored if the name resolver returns a service "
          "config.");
ABSL_FLAG(bool, display_peer_address, false,
          "Log the peer socket address of the connection that each RPC is made "
          "on to stderr.");
ABSL_FLAG(bool, json_input, false, "Input in json format");
ABSL_FLAG(bool, json_output, false, "Output in json format");
ABSL_FLAG(std::string, infile, "", "Input file (default is stdin)");
ABSL_FLAG(bool, batch, false,
          "Input contains multiple requests. Please do not use this to send "
          "more than a few RPCs. gRPC CLI has very different performance "
          "characteristics compared with normal RPC calls which make it "
          "unsuitable for loadtesting or significant production traffic.");
// TODO(Capstan): Consider using absl::Duration
ABSL_FLAG(double, timeout, -1,
          "Specify timeout in seconds, used to set the deadline for all "
          "RPCs. The default value of -1 means no deadline has been set.");
ABSL_FLAG(
    int, max_recv_msg_size, 0,
    "Specify the max receive message size in bytes for all RPCs. -1 indicates "
    "unlimited. The default value of 0 means to use the gRPC default.");
ABSL_FLAG(std::string, channel_args, "",
          "Comma-separated list of key=value gRPC ChannelArgs to apply "
          "(a=b,c=d,...). Values may be integers or strings.");

namespace grpc {
namespace testing {
namespace {

class GrpcTool {
 public:
  explicit GrpcTool();
  virtual ~GrpcTool() {}

  bool Help(int argc, const char** argv, const CliCredentials& cred,
            const GrpcToolOutputCallback& callback);
  bool CallMethod(int argc, const char** argv, const CliCredentials& cred,
                  const GrpcToolOutputCallback& callback);
  bool ListServices(int argc, const char** argv, const CliCredentials& cred,
                    const GrpcToolOutputCallback& callback);
  bool PrintType(int argc, const char** argv, const CliCredentials& cred,
                 const GrpcToolOutputCallback& callback);
  // TODO(zyc): implement the following methods
  // bool ListServices(int argc, const char** argv, GrpcToolOutputCallback
  // callback);
  // bool PrintTypeId(int argc, const char** argv, GrpcToolOutputCallback
  // callback);
  bool ParseMessage(int argc, const char** argv, const CliCredentials& cred,
                    const GrpcToolOutputCallback& callback);
  bool ToText(int argc, const char** argv, const CliCredentials& cred,
              const GrpcToolOutputCallback& callback);
  bool ToJson(int argc, const char** argv, const CliCredentials& cred,
              const GrpcToolOutputCallback& callback);
  bool ToBinary(int argc, const char** argv, const CliCredentials& cred,
                const GrpcToolOutputCallback& callback);

  void SetPrintCommandMode(int exit_status) {
    print_command_usage_ = true;
    usage_exit_status_ = exit_status;
  }

 private:
  void CommandUsage(const std::string& usage) const;
  bool print_command_usage_;
  int usage_exit_status_;
  const std::string cred_usage_;
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
    std::multimap<std::string, std::string>* client_metadata) {
  if (absl::GetFlag(FLAGS_metadata).empty()) {
    return;
  }
  std::vector<std::string> fields;
  const char delim = ':';
  const char escape = '\\';
  size_t cur = -1;
  std::stringstream ss;
  while (++cur < absl::GetFlag(FLAGS_metadata).length()) {
    switch (absl::GetFlag(FLAGS_metadata).at(cur)) {
      case escape:
        if (cur < absl::GetFlag(FLAGS_metadata).length() - 1) {
          char c = absl::GetFlag(FLAGS_metadata).at(++cur);
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
        ss << absl::GetFlag(FLAGS_metadata).at(cur);
    }
  }
  fields.push_back(ss.str());
  if (fields.size() % 2) {
    fprintf(stderr, "Failed to parse metadata flag.\n");
    exit(1);
  }
  for (size_t i = 0; i < fields.size(); i += 2) {
    client_metadata->insert(
        std::pair<std::string, std::string>(fields[i], fields[i + 1]));
  }
}

template <typename T>
void PrintMetadata(const T& m, const std::string& message) {
  if (m.empty()) {
    return;
  }
  fprintf(stderr, "%s\n", message.c_str());
  std::string pair;
  for (typename T::const_iterator iter = m.begin(); iter != m.end(); ++iter) {
    pair.clear();
    pair.append(iter->first.data(), iter->first.size());
    pair.append(" : ");
    pair.append(iter->second.data(), iter->second.size());
    fprintf(stderr, "%s\n", pair.c_str());
  }
}

void ReadResponse(CliCall* call, const std::string& method_name,
                  const GrpcToolOutputCallback& callback,
                  ProtoFileParser* parser, gpr_mu* parser_mu, bool print_mode) {
  std::string serialized_response_proto;
  std::multimap<grpc::string_ref, grpc::string_ref> server_initial_metadata;

  for (bool receive_initial_metadata = true; call->ReadAndMaybeNotifyWrite(
           &serialized_response_proto,
           receive_initial_metadata ? &server_initial_metadata : nullptr);
       receive_initial_metadata = false) {
    fprintf(stderr, "got response.\n");
    if (!absl::GetFlag(FLAGS_binary_output)) {
      gpr_mu_lock(parser_mu);
      serialized_response_proto = parser->GetFormattedStringFromMethod(
          method_name, serialized_response_proto, false /* is_request */,
          absl::GetFlag(FLAGS_json_output));
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

std::shared_ptr<grpc::Channel> CreateCliChannel(
    const std::string& server_address, const CliCredentials& cred,
    const grpc::ChannelArguments& extra_args) {
  grpc::ChannelArguments args(extra_args);
  if (!cred.GetSslTargetNameOverride().empty()) {
    args.SetSslTargetNameOverride(cred.GetSslTargetNameOverride());
  }
  if (!absl::GetFlag(FLAGS_default_service_config).empty()) {
    args.SetString(GRPC_ARG_SERVICE_CONFIG,
                   absl::GetFlag(FLAGS_default_service_config));
  }
  // See |GRPC_ARG_MAX_METADATA_SIZE| in |grpc_types.h|.
  // Set to large enough size (10M) that should work for most use cases.
  args.SetInt(GRPC_ARG_MAX_METADATA_SIZE, 10 * 1024 * 1024);

  // Extend channel args according to flag --channel_args.
  const auto flag = absl::GetFlag(FLAGS_channel_args);
  for (absl::string_view arg :
       absl::StrSplit(flag, ',', absl::SkipWhitespace())) {
    std::pair<std::string, std::string> kv =
        absl::StrSplit(arg, absl::MaxSplits('=', 1), absl::SkipWhitespace());
    int ival;
    if (absl::SimpleAtoi(kv.second, &ival)) {
      args.SetInt(kv.first, ival);
    } else if (!kv.second.empty()) {
      args.SetString(kv.first, kv.second);
    }
  }
  return grpc::CreateCustomChannel(server_address, cred.GetCredentials(), args);
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
    {"tojson", BindWith5Args(&GrpcTool::ToJson), 2, 3},
};

void Usage(const std::string& msg) {
  fprintf(
      stderr,
      "%s\n"
      "  grpc_cli ls ...         ; List services\n"
      "  grpc_cli call ...       ; Call method\n"
      "  grpc_cli type ...       ; Print type\n"
      "  grpc_cli parse ...      ; Parse message\n"
      "  grpc_cli totext ...     ; Convert binary message to text\n"
      "  grpc_cli tojson ...     ; Convert binary message to json\n"
      "  grpc_cli tobinary ...   ; Convert text message to binary\n"
      "  grpc_cli help ...       ; Print this message, or per-command usage\n"
      "\n",
      msg.c_str());

  exit(1);
}

const Command* FindCommand(const std::string& name) {
  for (int i = 0; i < static_cast<int>(ArraySize(ops)); i++) {
    if (name == ops[i].command) {
      return &ops[i];
    }
  }
  return nullptr;
}
}  // namespace

int GrpcToolMainLib(int argc, const char** argv, const CliCredentials& cred,
                    const GrpcToolOutputCallback& callback) {
  if (argc < 2) {
    Usage("No command specified");
  }

  std::string command = argv[1];
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
    Usage("Invalid command '" + command + "'");
  }
  return 1;
}

GrpcTool::GrpcTool() : print_command_usage_(false), usage_exit_status_(0) {}

void GrpcTool::CommandUsage(const std::string& usage) const {
  if (print_command_usage_) {
    fprintf(stderr, "\n%s%s\n", usage.c_str(),
            (usage.empty() || usage[usage.size() - 1] != '\n') ? "\n" : "");
    exit(usage_exit_status_);
  }
}

bool GrpcTool::Help(int argc, const char** argv, const CliCredentials& cred,
                    const GrpcToolOutputCallback& callback) {
  CommandUsage(
      "Print help\n"
      "  grpc_cli help [subcommand]\n");

  if (argc == 0) {
    Usage("");
  } else {
    const Command* cmd = FindCommand(argv[0]);
    if (cmd == nullptr) {
      Usage("Unknown command '" + std::string(argv[0]) + "'");
    }
    SetPrintCommandMode(0);
    cmd->function(this, -1, nullptr, cred, callback);
  }
  return true;
}

bool GrpcTool::ListServices(int argc, const char** argv,
                            const CliCredentials& cred,
                            const GrpcToolOutputCallback& callback) {
  CommandUsage(
      "List services\n"
      "  grpc_cli ls <address> [<service>[/<method>]]\n"
      "    <address>                ; host:port\n"
      "    <service>                ; Exported service name\n"
      "    <method>                 ; Method name\n"
      "    --l                      ; Use a long listing format\n"
      "    --outfile                ; Output filename (defaults to stdout)\n" +
      cred.GetCredentialUsage());

  std::string server_address(argv[0]);
  std::shared_ptr<grpc::Channel> channel =
      CreateCliChannel(server_address, cred, grpc::ChannelArguments());
  grpc::ProtoReflectionDescriptorDatabase desc_db(channel);
  grpc::protobuf::DescriptorPool desc_pool(&desc_db);

  std::vector<std::string> service_list;
  if (!desc_db.GetServices(&service_list)) {
    fprintf(stderr, "Received an error when querying services endpoint.\n");
    return false;
  }

  // If no service is specified, dump the list of services.
  std::string output;
  if (argc < 2) {
    // List all services, if --l is passed, then include full description,
    // otherwise include a summarized list only.
    if (absl::GetFlag(FLAGS_l)) {
      output = DescribeServiceList(service_list, desc_pool);
    } else {
      for (auto it = service_list.begin(); it != service_list.end(); it++) {
        auto const& service = *it;
        output.append(service);
        output.append("\n");
      }
    }
  } else {
    std::string service_name;
    std::string method_name;
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
        output = absl::GetFlag(FLAGS_l) ? DescribeService(service)
                                        : SummarizeService(service);
      } else {
        method_name.insert(0, 1, '.');
        method_name.insert(0, service_name);
        const grpc::protobuf::MethodDescriptor* method =
            desc_pool.FindMethodByName(method_name);
        if (method != nullptr) {
          output = absl::GetFlag(FLAGS_l) ? DescribeMethod(method)
                                          : SummarizeMethod(method);
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
          output = absl::GetFlag(FLAGS_l) ? DescribeMethod(method)
                                          : SummarizeMethod(method);
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

bool GrpcTool::PrintType(int /*argc*/, const char** argv,
                         const CliCredentials& cred,
                         const GrpcToolOutputCallback& callback) {
  CommandUsage(
      "Print type\n"
      "  grpc_cli type <address> <type>\n"
      "    <address>                ; host:port\n"
      "    <type>                   ; Protocol buffer type name\n" +
      cred.GetCredentialUsage());

  std::string server_address(argv[0]);
  std::shared_ptr<grpc::Channel> channel =
      CreateCliChannel(server_address, cred, grpc::ChannelArguments());
  grpc::ProtoReflectionDescriptorDatabase desc_db(channel);
  grpc::protobuf::DescriptorPool desc_pool(&desc_db);

  std::string output;
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
                          const GrpcToolOutputCallback& callback) {
  CommandUsage(
      "Call method\n"
      "  grpc_cli call <address> <service>[.<method>] <request>\n"
      "    <address>                ; host:port\n"
      "    <service>                ; Exported service name\n"
      "    <method>                 ; Method name\n"
      "    <request>                ; Text protobuffer (overrides infile)\n"
      "    --protofiles             ; Comma separated proto files used as a"
      " fallback when parsing request/response\n"
      "    --proto_path             ; The search paths of proto files"
      " (" GRPC_CLI_PATH_SEPARATOR
      " separated), valid only when --protofiles is given\n"
      "    --noremotedb             ; Don't attempt to use reflection service"
      " at all\n"
      "    --metadata               ; The metadata to be sent to the server\n"
      "    --infile                 ; Input filename (defaults to stdin)\n"
      "    --outfile                ; Output filename (defaults to stdout)\n"
      "    --binary_input           ; Input in binary format\n"
      "    --binary_output          ; Output in binary format\n"
      "    --json_input             ; Input in json format\n"
      "    --json_output            ; Output in json format\n"
      "    --max_recv_msg_size      ; Specify max receive message size in "
      "bytes. -1 indicates unlimited. The default value of 0 means to use the "
      "gRPC default.\n"
      "    --timeout                ; Specify timeout (in seconds), used to "
      "set the deadline for RPCs. The default value of -1 means no "
      "deadline has been set.\n" +
      cred.GetCredentialUsage());

  std::stringstream output_ss;
  std::string request_text;
  std::string server_address(argv[0]);
  std::string method_name(argv[1]);
  std::string formatted_method_name;
  std::unique_ptr<ProtoFileParser> parser;
  std::string serialized_request_proto;
  CliArgs cli_args;
  cli_args.timeout = absl::GetFlag(FLAGS_timeout);
  bool print_mode = false;

  grpc::ChannelArguments args;
  if (absl::GetFlag(FLAGS_max_recv_msg_size) != 0) {
    args.SetMaxReceiveMessageSize(absl::GetFlag(FLAGS_max_recv_msg_size));
  }
  std::shared_ptr<grpc::Channel> channel =
      CreateCliChannel(server_address, cred, args);

  if (!absl::GetFlag(FLAGS_binary_input) ||
      !absl::GetFlag(FLAGS_binary_output)) {
    parser = std::make_unique<grpc::testing::ProtoFileParser>(
        absl::GetFlag(FLAGS_remotedb) ? channel : nullptr,
        absl::GetFlag(FLAGS_proto_path), absl::GetFlag(FLAGS_protofiles));
    if (parser->HasError()) {
      fprintf(
          stderr,
          "Failed to find remote reflection service and local proto files.\n");
      return false;
    }
  }

  if (absl::GetFlag(FLAGS_binary_input)) {
    formatted_method_name = method_name;
  } else {
    formatted_method_name = parser->GetFormattedMethodName(method_name);
    if (parser->HasError()) {
      fprintf(stderr, "Failed to find method %s in proto files.\n",
              method_name.c_str());
    }
  }

  if (argc == 3) {
    request_text = argv[2];
  }

  if (parser != nullptr &&
      parser->IsStreaming(method_name, true /* is_request */)) {
    std::istream* input_stream;
    std::ifstream input_file;

    if (absl::GetFlag(FLAGS_batch)) {
      fprintf(stderr, "Batch mode for streaming RPC is not supported.\n");
      return false;
    }

    std::multimap<std::string, std::string> client_metadata;
    ParseMetadataFlag(&client_metadata);
    PrintMetadata(client_metadata, "Sending client initial metadata:");

    CliCall call(channel, formatted_method_name, client_metadata, cli_args);
    if (absl::GetFlag(FLAGS_display_peer_address)) {
      fprintf(stderr, "New call for method_name:%s has peer address:|%s|\n",
              formatted_method_name.c_str(), call.peer().c_str());
    }

    if (absl::GetFlag(FLAGS_infile).empty()) {
      if (isatty(fileno(stdin))) {
        print_mode = true;
        fprintf(stderr, "reading streaming request message from stdin...\n");
      }
      input_stream = &std::cin;
    } else {
      input_file.open(absl::GetFlag(FLAGS_infile),
                      std::ios::in | std::ios::binary);
      if (!input_file) {
        fprintf(stderr, "Failed to open infile %s.\n",
                absl::GetFlag(FLAGS_infile).c_str());
        return false;
      }

      input_stream = &input_file;
    }

    gpr_mu parser_mu;
    gpr_mu_init(&parser_mu);
    std::thread read_thread(ReadResponse, &call, method_name, callback,
                            parser.get(), &parser_mu, print_mode);

    std::stringstream request_ss;
    std::string line;
    while (!request_text.empty() ||
           (!input_stream->eof() && getline(*input_stream, line))) {
      if (!request_text.empty()) {
        if (absl::GetFlag(FLAGS_binary_input)) {
          serialized_request_proto = request_text;
          request_text.clear();
        } else {
          gpr_mu_lock(&parser_mu);
          serialized_request_proto = parser->GetSerializedProtoFromMethod(
              method_name, request_text, true /* is_request */,
              absl::GetFlag(FLAGS_json_input));
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
        if (line.empty()) {
          request_text = request_ss.str();
          request_ss.str(std::string());
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
    gpr_mu_destroy(&parser_mu);

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
    if (absl::GetFlag(FLAGS_batch)) {
      if (parser != nullptr &&
          parser->IsStreaming(method_name, false /* is_request */)) {
        fprintf(stderr, "Batch mode for streaming RPC is not supported.\n");
        return false;
      }

      std::istream* input_stream;
      std::ifstream input_file;

      if (absl::GetFlag(FLAGS_infile).empty()) {
        if (isatty(fileno(stdin))) {
          print_mode = true;
          fprintf(stderr, "reading request messages from stdin...\n");
        }
        input_stream = &std::cin;
      } else {
        input_file.open(absl::GetFlag(FLAGS_infile),
                        std::ios::in | std::ios::binary);
        input_stream = &input_file;
      }

      std::multimap<std::string, std::string> client_metadata;
      ParseMetadataFlag(&client_metadata);
      if (print_mode) {
        PrintMetadata(client_metadata, "Sending client initial metadata:");
      }

      std::stringstream request_ss;
      std::string line;
      while (!request_text.empty() ||
             (!input_stream->eof() && getline(*input_stream, line))) {
        if (!request_text.empty()) {
          if (absl::GetFlag(FLAGS_binary_input)) {
            serialized_request_proto = request_text;
            request_text.clear();
          } else {
            serialized_request_proto = parser->GetSerializedProtoFromMethod(
                method_name, request_text, true /* is_request */,
                absl::GetFlag(FLAGS_json_input));
            request_text.clear();
            if (parser->HasError()) {
              if (print_mode) {
                fprintf(stderr, "Failed to parse request.\n");
              }
              continue;
            }
          }

          std::string serialized_response_proto;
          std::multimap<grpc::string_ref, grpc::string_ref>
              server_initial_metadata, server_trailing_metadata;
          CliCall call(channel, formatted_method_name, client_metadata,
                       cli_args);
          if (absl::GetFlag(FLAGS_display_peer_address)) {
            fprintf(stderr,
                    "New call for method_name:%s has peer address:|%s|\n",
                    formatted_method_name.c_str(), call.peer().c_str());
          }
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

            if (absl::GetFlag(FLAGS_binary_output)) {
              if (!callback(serialized_response_proto)) {
                break;
              }
            } else {
              std::string response_text = parser->GetFormattedStringFromMethod(
                  method_name, serialized_response_proto,
                  false /* is_request */, absl::GetFlag(FLAGS_json_output));

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
          if (line.empty()) {
            request_text = request_ss.str();
            request_ss.str(std::string());
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
      if (!absl::GetFlag(FLAGS_infile).empty()) {
        fprintf(stderr, "warning: request given in argv, ignoring --infile\n");
      }
    } else {
      std::stringstream input_stream;
      if (absl::GetFlag(FLAGS_infile).empty()) {
        if (isatty(fileno(stdin))) {
          fprintf(stderr, "reading request message from stdin...\n");
        }
        input_stream << std::cin.rdbuf();
      } else {
        std::ifstream input_file(absl::GetFlag(FLAGS_infile),
                                 std::ios::in | std::ios::binary);
        input_stream << input_file.rdbuf();
        input_file.close();
      }
      request_text = input_stream.str();
    }

    if (absl::GetFlag(FLAGS_binary_input)) {
      serialized_request_proto = request_text;
    } else {
      serialized_request_proto = parser->GetSerializedProtoFromMethod(
          method_name, request_text, true /* is_request */,
          absl::GetFlag(FLAGS_json_input));
      if (parser->HasError()) {
        fprintf(stderr, "Failed to parse request.\n");
        return false;
      }
    }
    fprintf(stderr, "connecting to %s\n", server_address.c_str());

    std::string serialized_response_proto;
    std::multimap<std::string, std::string> client_metadata;
    std::multimap<grpc::string_ref, grpc::string_ref> server_initial_metadata,
        server_trailing_metadata;
    ParseMetadataFlag(&client_metadata);
    PrintMetadata(client_metadata, "Sending client initial metadata:");

    CliCall call(channel, formatted_method_name, client_metadata, cli_args);
    if (absl::GetFlag(FLAGS_display_peer_address)) {
      fprintf(stderr, "New call for method_name:%s has peer address:|%s|\n",
              formatted_method_name.c_str(), call.peer().c_str());
    }
    call.Write(serialized_request_proto);
    call.WritesDone();

    for (bool receive_initial_metadata = true; call.Read(
             &serialized_response_proto,
             receive_initial_metadata ? &server_initial_metadata : nullptr);
         receive_initial_metadata = false) {
      if (!absl::GetFlag(FLAGS_binary_output)) {
        serialized_response_proto = parser->GetFormattedStringFromMethod(
            method_name, serialized_response_proto, false /* is_request */,
            absl::GetFlag(FLAGS_json_output));
        if (parser->HasError()) {
          fprintf(stderr, "Failed to parse response.\n");
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
                            const GrpcToolOutputCallback& callback) {
  CommandUsage(
      "Parse message\n"
      "  grpc_cli parse <address> <type> [<message>]\n"
      "    <address>                ; host:port\n"
      "    <type>                   ; Protocol buffer type name\n"
      "    <message>                ; Text protobuffer (overrides --infile)\n"
      "    --protofiles             ; Comma separated proto files used as a"
      " fallback when parsing request/response\n"
      "    --proto_path             ; The search paths of proto files"
      " (" GRPC_CLI_PATH_SEPARATOR
      " separated), valid  only when --protofiles is given\n"
      "    --noremotedb             ; Don't attempt to use reflection service"
      " at all\n"
      "    --infile                 ; Input filename (defaults to stdin)\n"
      "    --outfile                ; Output filename (defaults to stdout)\n"
      "    --binary_input           ; Input in binary format\n"
      "    --binary_output          ; Output in binary format\n"
      "    --json_input             ; Input in json format\n"
      "    --json_output            ; Output in json format\n" +
      cred.GetCredentialUsage());

  std::stringstream output_ss;
  std::string message_text;
  std::string server_address(argv[0]);
  std::string type_name(argv[1]);
  std::unique_ptr<grpc::testing::ProtoFileParser> parser;
  std::string serialized_request_proto;

  if (argc == 3) {
    message_text = argv[2];
    if (!absl::GetFlag(FLAGS_infile).empty()) {
      fprintf(stderr, "warning: message given in argv, ignoring --infile.\n");
    }
  } else {
    std::stringstream input_stream;
    if (absl::GetFlag(FLAGS_infile).empty()) {
      if (isatty(fileno(stdin))) {
        fprintf(stderr, "reading request message from stdin...\n");
      }
      input_stream << std::cin.rdbuf();
    } else {
      std::ifstream input_file(absl::GetFlag(FLAGS_infile),
                               std::ios::in | std::ios::binary);
      input_stream << input_file.rdbuf();
      input_file.close();
    }
    message_text = input_stream.str();
  }

  if (!absl::GetFlag(FLAGS_binary_input) ||
      !absl::GetFlag(FLAGS_binary_output)) {
    std::shared_ptr<grpc::Channel> channel =
        CreateCliChannel(server_address, cred, grpc::ChannelArguments());
    parser = std::make_unique<grpc::testing::ProtoFileParser>(
        absl::GetFlag(FLAGS_remotedb) ? channel : nullptr,
        absl::GetFlag(FLAGS_proto_path), absl::GetFlag(FLAGS_protofiles));
    if (parser->HasError()) {
      fprintf(
          stderr,
          "Failed to find remote reflection service and local proto files.\n");
      return false;
    }
  }

  if (absl::GetFlag(FLAGS_binary_input)) {
    serialized_request_proto = message_text;
  } else {
    serialized_request_proto = parser->GetSerializedProtoFromMessageType(
        type_name, message_text, absl::GetFlag(FLAGS_json_input));
    if (parser->HasError()) {
      fprintf(stderr, "Failed to serialize the message.\n");
      return false;
    }
  }

  if (absl::GetFlag(FLAGS_binary_output)) {
    output_ss << serialized_request_proto;
  } else {
    std::string output_text;
    output_text = parser->GetFormattedStringFromMessageType(
        type_name, serialized_request_proto, absl::GetFlag(FLAGS_json_output));
    if (parser->HasError()) {
      fprintf(stderr, "Failed to deserialize the message.\n");
      return false;
    }

    output_ss << output_text << std::endl;
  }

  return callback(output_ss.str());
}

bool GrpcTool::ToText(int argc, const char** argv, const CliCredentials& cred,
                      const GrpcToolOutputCallback& callback) {
  CommandUsage(
      "Convert binary message to text\n"
      "  grpc_cli totext <protofiles> <type>\n"
      "    <protofiles>             ; Comma separated list of proto files\n"
      "    <type>                   ; Protocol buffer type name\n"
      "    --proto_path             ; The search paths of proto files"
      " (" GRPC_CLI_PATH_SEPARATOR
      " separated)\n"
      "    --infile                 ; Input filename (defaults to stdin)\n"
      "    --outfile                ; Output filename (defaults to stdout)\n");

  absl::SetFlag(&FLAGS_protofiles, argv[0]);
  absl::SetFlag(&FLAGS_remotedb, false);
  absl::SetFlag(&FLAGS_binary_input, true);
  absl::SetFlag(&FLAGS_binary_output, false);
  return ParseMessage(argc, argv, cred, callback);
}

bool GrpcTool::ToJson(int argc, const char** argv, const CliCredentials& cred,
                      const GrpcToolOutputCallback& callback) {
  CommandUsage(
      "Convert binary message to json\n"
      "  grpc_cli tojson <protofiles> <type>\n"
      "    <protofiles>             ; Comma separated list of proto files\n"
      "    <type>                   ; Protocol buffer type name\n"
      "    --proto_path             ; The search paths of proto files"
      " (" GRPC_CLI_PATH_SEPARATOR
      " separated)\n"
      "    --infile                 ; Input filename (defaults to stdin)\n"
      "    --outfile                ; Output filename (defaults to stdout)\n");

  absl::SetFlag(&FLAGS_protofiles, argv[0]);
  absl::SetFlag(&FLAGS_remotedb, false);
  absl::SetFlag(&FLAGS_binary_input, true);
  absl::SetFlag(&FLAGS_binary_output, false);
  absl::SetFlag(&FLAGS_json_output, true);
  return ParseMessage(argc, argv, cred, callback);
}

bool GrpcTool::ToBinary(int argc, const char** argv, const CliCredentials& cred,
                        const GrpcToolOutputCallback& callback) {
  CommandUsage(
      "Convert text message to binary\n"
      "  grpc_cli tobinary <protofiles> <type> [<message>]\n"
      "    <protofiles>             ; Comma separated list of proto files\n"
      "    <type>                   ; Protocol buffer type name\n"
      "    --proto_path             ; The search paths of proto files"
      " (" GRPC_CLI_PATH_SEPARATOR
      " separated)\n"
      "    --infile                 ; Input filename (defaults to stdin)\n"
      "    --outfile                ; Output filename (defaults to stdout)\n");

  absl::SetFlag(&FLAGS_protofiles, argv[0]);
  absl::SetFlag(&FLAGS_remotedb, false);
  absl::SetFlag(&FLAGS_binary_input, false);
  absl::SetFlag(&FLAGS_binary_output, true);
  return ParseMessage(argc, argv, cred, callback);
}

}  // namespace testing
}  // namespace grpc
