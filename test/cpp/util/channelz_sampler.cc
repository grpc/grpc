//
//
// Copyright 2015 gRPC authors.
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
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/channelz_service_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <ostream>
#include <queue>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "google/protobuf/text_format.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/cpp/server/channelz/channelz_service.h"
#include "src/proto/grpc/channelz/channelz.pb.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/test_credentials_provider.h"

ABSL_FLAG(std::string, server_address, "", "channelz server address");
ABSL_FLAG(std::string, custom_credentials_type, "", "custom credentials type");
ABSL_FLAG(int64_t, sampling_times, 1, "number of sampling");
// TODO(Capstan): Consider using absl::Duration
ABSL_FLAG(int64_t, sampling_interval_seconds, 0,
          "sampling interval in seconds");
ABSL_FLAG(std::string, output_json, "", "output filename in json format");

namespace {
using grpc::ClientContext;
using grpc::Status;
using grpc::StatusCode;
using grpc::channelz::v1::GetChannelRequest;
using grpc::channelz::v1::GetChannelResponse;
using grpc::channelz::v1::GetServersRequest;
using grpc::channelz::v1::GetServersResponse;
using grpc::channelz::v1::GetSocketRequest;
using grpc::channelz::v1::GetSocketResponse;
using grpc::channelz::v1::GetSubchannelRequest;
using grpc::channelz::v1::GetSubchannelResponse;
using grpc::channelz::v1::GetTopChannelsRequest;
using grpc::channelz::v1::GetTopChannelsResponse;
}  // namespace

class ChannelzSampler final {
 public:
  // Get server_id of a server
  int64_t GetServerID(const grpc::channelz::v1::Server& server) {
    return server.ref().server_id();
  }

  // Get channel_id of a channel
  inline int64_t GetChannelID(const grpc::channelz::v1::Channel& channel) {
    return channel.ref().channel_id();
  }

  // Get subchannel_id of a subchannel
  inline int64_t GetSubchannelID(
      const grpc::channelz::v1::Subchannel& subchannel) {
    return subchannel.ref().subchannel_id();
  }

  // Get socket_id of a socket
  inline int64_t GetSocketID(const grpc::channelz::v1::Socket& socket) {
    return socket.ref().socket_id();
  }

  // Get name of a server
  inline std::string GetServerName(const grpc::channelz::v1::Server& server) {
    return server.ref().name();
  }

  // Get name of a channel
  inline std::string GetChannelName(
      const grpc::channelz::v1::Channel& channel) {
    return channel.ref().name();
  }

  // Get name of a subchannel
  inline std::string GetSubchannelName(
      const grpc::channelz::v1::Subchannel& subchannel) {
    return subchannel.ref().name();
  }

  // Get name of a socket
  inline std::string GetSocketName(const grpc::channelz::v1::Socket& socket) {
    return socket.ref().name();
  }

  // Get a channel based on channel_id
  grpc::channelz::v1::Channel GetChannelRPC(int64_t channel_id) {
    GetChannelRequest get_channel_request;
    get_channel_request.set_channel_id(channel_id);
    GetChannelResponse get_channel_response;
    ClientContext get_channel_context;
    get_channel_context.set_deadline(
        grpc_timeout_seconds_to_deadline(rpc_timeout_seconds_));
    Status status = channelz_stub_->GetChannel(
        &get_channel_context, get_channel_request, &get_channel_response);
    if (!status.ok()) {
      LOG(ERROR) << "GetChannelRPC failed: "
                 << get_channel_context.debug_error_string();
      CHECK(0);
    }
    return get_channel_response.channel();
  }

  // Get a subchannel based on subchannel_id
  grpc::channelz::v1::Subchannel GetSubchannelRPC(int64_t subchannel_id) {
    GetSubchannelRequest get_subchannel_request;
    get_subchannel_request.set_subchannel_id(subchannel_id);
    GetSubchannelResponse get_subchannel_response;
    ClientContext get_subchannel_context;
    get_subchannel_context.set_deadline(
        grpc_timeout_seconds_to_deadline(rpc_timeout_seconds_));
    Status status = channelz_stub_->GetSubchannel(&get_subchannel_context,
                                                  get_subchannel_request,
                                                  &get_subchannel_response);
    if (!status.ok()) {
      LOG(ERROR) << "GetSubchannelRPC failed: "
                 << get_subchannel_context.debug_error_string();
      CHECK(0);
    }
    return get_subchannel_response.subchannel();
  }

  // get a socket based on socket_id
  grpc::channelz::v1::Socket GetSocketRPC(int64_t socket_id) {
    GetSocketRequest get_socket_request;
    get_socket_request.set_socket_id(socket_id);
    GetSocketResponse get_socket_response;
    ClientContext get_socket_context;
    get_socket_context.set_deadline(
        grpc_timeout_seconds_to_deadline(rpc_timeout_seconds_));
    Status status = channelz_stub_->GetSocket(
        &get_socket_context, get_socket_request, &get_socket_response);
    if (!status.ok()) {
      LOG(ERROR) << "GetSocketRPC failed: "
                 << get_socket_context.debug_error_string();
      CHECK(0);
    }
    return get_socket_response.socket();
  }

  // get the descendant channels/subchannels/sockets of a channel
  // push descendant channels/subchannels to queue for layer traverse
  // store descendant channels/subchannels/sockets for dumping data
  void GetChannelDescedence(
      const grpc::channelz::v1::Channel& channel,
      std::queue<grpc::channelz::v1::Channel>& channel_queue,
      std::queue<grpc::channelz::v1::Subchannel>& subchannel_queue) {
    std::cout << "    Channel ID" << GetChannelID(channel) << "_"
              << GetChannelName(channel) << " descendence - ";
    if (channel.channel_ref_size() > 0 || channel.subchannel_ref_size() > 0) {
      if (channel.channel_ref_size() > 0) {
        std::cout << "channel: ";
        for (const auto& _channelref : channel.channel_ref()) {
          int64_t ch_id = _channelref.channel_id();
          std::cout << "ID" << ch_id << "_" << _channelref.name() << " ";
          grpc::channelz::v1::Channel ch = GetChannelRPC(ch_id);
          channel_queue.push(ch);
          if (CheckID(ch_id)) {
            all_channels_.push_back(ch);
            StoreChannelInJson(ch);
          }
        }
        if (channel.subchannel_ref_size() > 0) {
          std::cout << ", ";
        }
      }
      if (channel.subchannel_ref_size() > 0) {
        std::cout << "subchannel: ";
        for (const auto& _subchannelref : channel.subchannel_ref()) {
          int64_t subch_id = _subchannelref.subchannel_id();
          std::cout << "ID" << subch_id << "_" << _subchannelref.name() << " ";
          grpc::channelz::v1::Subchannel subch = GetSubchannelRPC(subch_id);
          subchannel_queue.push(subch);
          if (CheckID(subch_id)) {
            all_subchannels_.push_back(subch);
            StoreSubchannelInJson(subch);
          }
        }
      }
    } else if (channel.socket_ref_size() > 0) {
      std::cout << "socket: ";
      for (const auto& _socketref : channel.socket_ref()) {
        int64_t so_id = _socketref.socket_id();
        std::cout << "ID" << so_id << "_" << _socketref.name() << " ";
        grpc::channelz::v1::Socket so = GetSocketRPC(so_id);
        if (CheckID(so_id)) {
          all_sockets_.push_back(so);
          StoreSocketInJson(so);
        }
      }
    }
    std::cout << std::endl;
  }

  // get the descendant channels/subchannels/sockets of a subchannel
  // push descendant channels/subchannels to queue for layer traverse
  // store descendant channels/subchannels/sockets for dumping data
  void GetSubchannelDescedence(
      grpc::channelz::v1::Subchannel& subchannel,
      std::queue<grpc::channelz::v1::Channel>& channel_queue,
      std::queue<grpc::channelz::v1::Subchannel>& subchannel_queue) {
    std::cout << "    Subchannel ID" << GetSubchannelID(subchannel) << "_"
              << GetSubchannelName(subchannel) << " descendence - ";
    if (subchannel.channel_ref_size() > 0 ||
        subchannel.subchannel_ref_size() > 0) {
      if (subchannel.channel_ref_size() > 0) {
        std::cout << "channel: ";
        for (const auto& _channelref : subchannel.channel_ref()) {
          int64_t ch_id = _channelref.channel_id();
          std::cout << "ID" << ch_id << "_" << _channelref.name() << " ";
          grpc::channelz::v1::Channel ch = GetChannelRPC(ch_id);
          channel_queue.push(ch);
          if (CheckID(ch_id)) {
            all_channels_.push_back(ch);
            StoreChannelInJson(ch);
          }
        }
        if (subchannel.subchannel_ref_size() > 0) {
          std::cout << ", ";
        }
      }
      if (subchannel.subchannel_ref_size() > 0) {
        std::cout << "subchannel: ";
        for (const auto& _subchannelref : subchannel.subchannel_ref()) {
          int64_t subch_id = _subchannelref.subchannel_id();
          std::cout << "ID" << subch_id << "_" << _subchannelref.name() << " ";
          grpc::channelz::v1::Subchannel subch = GetSubchannelRPC(subch_id);
          subchannel_queue.push(subch);
          if (CheckID(subch_id)) {
            all_subchannels_.push_back(subch);
            StoreSubchannelInJson(subch);
          }
        }
      }
    } else if (subchannel.socket_ref_size() > 0) {
      std::cout << "socket: ";
      for (const auto& _socketref : subchannel.socket_ref()) {
        int64_t so_id = _socketref.socket_id();
        std::cout << "ID" << so_id << "_" << _socketref.name() << " ";
        grpc::channelz::v1::Socket so = GetSocketRPC(so_id);
        if (CheckID(so_id)) {
          all_sockets_.push_back(so);
          StoreSocketInJson(so);
        }
      }
    }
    std::cout << std::endl;
  }

  // Set up the channelz sampler client
  // Initialize json as an array
  void Setup(const std::string& custom_credentials_type,
             const std::string& server_address) {
    json_ = grpc_core::Json::Array();
    rpc_timeout_seconds_ = 20;
    grpc::ChannelArguments channel_args;
    std::shared_ptr<grpc::ChannelCredentials> channel_creds =
        grpc::testing::GetCredentialsProvider()->GetChannelCredentials(
            custom_credentials_type, &channel_args);
    if (!channel_creds) {
      LOG(ERROR) << "Wrong user credential type: " << custom_credentials_type
                 << ". Allowed credential types: INSECURE_CREDENTIALS, ssl, "
                    "alts, google_default_credentials.";
      CHECK(0);
    }
    std::shared_ptr<grpc::Channel> channel =
        CreateChannel(server_address, channel_creds);
    channelz_stub_ = grpc::channelz::v1::Channelz::NewStub(channel);
  }

  // Get all servers, keep querying until getting all
  // Store servers for dumping data
  // Need to check id repeating for servers
  void GetServersRPC() {
    int64_t server_start_id = 0;
    while (true) {
      GetServersRequest get_servers_request;
      GetServersResponse get_servers_response;
      ClientContext get_servers_context;
      get_servers_context.set_deadline(
          grpc_timeout_seconds_to_deadline(rpc_timeout_seconds_));
      get_servers_request.set_start_server_id(server_start_id);
      Status status = channelz_stub_->GetServers(
          &get_servers_context, get_servers_request, &get_servers_response);
      if (!status.ok()) {
        if (status.error_code() == StatusCode::UNIMPLEMENTED) {
          LOG(ERROR) << "Error status UNIMPLEMENTED. Please check and make "
                        "sure channelz has been registered on the server being "
                        "queried.";
        } else {
          LOG(ERROR) << "GetServers RPC with "
                        "GetServersRequest.server_start_id="
                     << server_start_id << ", failed: "
                     << get_servers_context.debug_error_string();
        }
        CHECK(0);
      }
      for (const auto& _server : get_servers_response.server()) {
        all_servers_.push_back(_server);
        StoreServerInJson(_server);
      }
      if (!get_servers_response.end()) {
        server_start_id = GetServerID(all_servers_.back()) + 1;
      } else {
        break;
      }
    }
    std::cout << "Number of servers = " << all_servers_.size() << std::endl;
  }

  // Get sockets that belongs to servers
  // Store sockets for dumping data
  void GetSocketsOfServers() {
    for (const auto& _server : all_servers_) {
      std::cout << "Server ID" << GetServerID(_server) << "_"
                << GetServerName(_server) << " listen_socket - ";
      for (const auto& _socket : _server.listen_socket()) {
        int64_t so_id = _socket.socket_id();
        std::cout << "ID" << so_id << "_" << _socket.name() << " ";
        if (CheckID(so_id)) {
          grpc::channelz::v1::Socket so = GetSocketRPC(so_id);
          all_sockets_.push_back(so);
          StoreSocketInJson(so);
        }
      }
      std::cout << std::endl;
    }
  }

  // Get all top channels, keep querying until getting all
  // Store channels for dumping data
  // No need to check id repeating for top channels
  void GetTopChannelsRPC() {
    int64_t channel_start_id = 0;
    while (true) {
      GetTopChannelsRequest get_top_channels_request;
      GetTopChannelsResponse get_top_channels_response;
      ClientContext get_top_channels_context;
      get_top_channels_context.set_deadline(
          grpc_timeout_seconds_to_deadline(rpc_timeout_seconds_));
      get_top_channels_request.set_start_channel_id(channel_start_id);
      Status status = channelz_stub_->GetTopChannels(
          &get_top_channels_context, get_top_channels_request,
          &get_top_channels_response);
      if (!status.ok()) {
        LOG(ERROR) << "GetTopChannels RPC with "
                      "GetTopChannelsRequest.channel_start_id="
                   << channel_start_id << " failed: "
                   << get_top_channels_context.debug_error_string();
        CHECK(0);
      }
      for (const auto& _topchannel : get_top_channels_response.channel()) {
        top_channels_.push_back(_topchannel);
        all_channels_.push_back(_topchannel);
        StoreChannelInJson(_topchannel);
      }
      if (!get_top_channels_response.end()) {
        channel_start_id = GetChannelID(top_channels_.back()) + 1;
      } else {
        break;
      }
    }
    std::cout << std::endl
              << "Number of top channels = " << top_channels_.size()
              << std::endl;
  }

  // layer traverse for each top channel
  void TraverseTopChannels() {
    for (const auto& _topchannel : top_channels_) {
      int tree_depth = 0;
      std::queue<grpc::channelz::v1::Channel> channel_queue;
      std::queue<grpc::channelz::v1::Subchannel> subchannel_queue;
      std::cout << "Tree depth = " << tree_depth << std::endl;
      GetChannelDescedence(_topchannel, channel_queue, subchannel_queue);
      while (!channel_queue.empty() || !subchannel_queue.empty()) {
        ++tree_depth;
        std::cout << "Tree depth = " << tree_depth << std::endl;
        int ch_q_size = channel_queue.size();
        int subch_q_size = subchannel_queue.size();
        for (int i = 0; i < ch_q_size; ++i) {
          grpc::channelz::v1::Channel ch = channel_queue.front();
          channel_queue.pop();
          GetChannelDescedence(ch, channel_queue, subchannel_queue);
        }
        for (int i = 0; i < subch_q_size; ++i) {
          grpc::channelz::v1::Subchannel subch = subchannel_queue.front();
          subchannel_queue.pop();
          GetSubchannelDescedence(subch, channel_queue, subchannel_queue);
        }
      }
      std::cout << std::endl;
    }
  }

  // dump data of all entities to stdout
  void DumpStdout() {
    std::string data_str;
    for (const auto& _channel : all_channels_) {
      std::cout << "channel ID" << GetChannelID(_channel) << "_"
                << GetChannelName(_channel) << " data:" << std::endl;
      // TODO(mohanli): TextFormat::PrintToString records time as seconds and
      // nanos. Need a more human readable way.
      ::google::protobuf::TextFormat::PrintToString(_channel.data(), &data_str);
      printf("%s\n", data_str.c_str());
    }
    for (const auto& _subchannel : all_subchannels_) {
      std::cout << "subchannel ID" << GetSubchannelID(_subchannel) << "_"
                << GetSubchannelName(_subchannel) << " data:" << std::endl;
      ::google::protobuf::TextFormat::PrintToString(_subchannel.data(),
                                                    &data_str);
      printf("%s\n", data_str.c_str());
    }
    for (const auto& _server : all_servers_) {
      std::cout << "server ID" << GetServerID(_server) << "_"
                << GetServerName(_server) << " data:" << std::endl;
      ::google::protobuf::TextFormat::PrintToString(_server.data(), &data_str);
      printf("%s\n", data_str.c_str());
    }
    for (const auto& _socket : all_sockets_) {
      std::cout << "socket ID" << GetSocketID(_socket) << "_"
                << GetSocketName(_socket) << " data:" << std::endl;
      ::google::protobuf::TextFormat::PrintToString(_socket.data(), &data_str);
      printf("%s\n", data_str.c_str());
    }
  }

  // Store a channel in Json
  void StoreChannelInJson(const grpc::channelz::v1::Channel& channel) {
    std::string id = grpc::to_string(GetChannelID(channel));
    std::string type = "Channel";
    std::string description;
    ::google::protobuf::TextFormat::PrintToString(channel.data(), &description);
    grpc_core::Json description_json = grpc_core::Json::FromString(description);
    StoreEntityInJson(id, type, description_json);
  }

  // Store a subchannel in Json
  void StoreSubchannelInJson(const grpc::channelz::v1::Subchannel& subchannel) {
    std::string id = grpc::to_string(GetSubchannelID(subchannel));
    std::string type = "Subchannel";
    std::string description;
    ::google::protobuf::TextFormat::PrintToString(subchannel.data(),
                                                  &description);
    grpc_core::Json description_json = grpc_core::Json::FromString(description);
    StoreEntityInJson(id, type, description_json);
  }

  // Store a server in Json
  void StoreServerInJson(const grpc::channelz::v1::Server& server) {
    std::string id = grpc::to_string(GetServerID(server));
    std::string type = "Server";
    std::string description;
    ::google::protobuf::TextFormat::PrintToString(server.data(), &description);
    grpc_core::Json description_json = grpc_core::Json::FromString(description);
    StoreEntityInJson(id, type, description_json);
  }

  // Store a socket in Json
  void StoreSocketInJson(const grpc::channelz::v1::Socket& socket) {
    std::string id = grpc::to_string(GetSocketID(socket));
    std::string type = "Socket";
    std::string description;
    ::google::protobuf::TextFormat::PrintToString(socket.data(), &description);
    grpc_core::Json description_json = grpc_core::Json::FromString(description);
    StoreEntityInJson(id, type, description_json);
  }

  // Store an entity in Json
  void StoreEntityInJson(std::string& id, std::string& type,
                         const grpc_core::Json& description) {
    std::string start, finish;
    gpr_timespec ago = gpr_time_sub(
        now_,
        gpr_time_from_seconds(absl::GetFlag(FLAGS_sampling_interval_seconds),
                              GPR_TIMESPAN));
    std::stringstream ss;
    const time_t time_now = now_.tv_sec;
    ss << std::put_time(std::localtime(&time_now), "%F %T");
    finish = ss.str();  // example: "2019-02-01 12:12:18"
    ss.str("");
    const time_t time_ago = ago.tv_sec;
    ss << std::put_time(std::localtime(&time_ago), "%F %T");
    start = ss.str();
    grpc_core::Json obj = grpc_core::Json::FromObject(
        {{"Task",
          grpc_core::Json::FromString(absl::StrFormat("%s_ID%s", type, id))},
         {"Start", grpc_core::Json::FromString(start)},
         {"Finish", grpc_core::Json::FromString(finish)},
         {"ID", grpc_core::Json::FromString(id)},
         {"Type", grpc_core::Json::FromString(type)},
         {"Description", description}});
    json_.push_back(obj);
  }

  // Dump data in json
  std::string DumpJson() {
    return grpc_core::JsonDump(grpc_core::Json::FromArray(json_));
  }

  // Check if one entity has been recorded
  bool CheckID(int64_t id) {
    if (id_set_.count(id) == 0) {
      id_set_.insert(id);
      return true;
    } else {
      return false;
    }
  }

  // Record current time
  void RecordNow() { now_ = gpr_now(GPR_CLOCK_REALTIME); }

 private:
  std::unique_ptr<grpc::channelz::v1::Channelz::Stub> channelz_stub_;
  std::vector<grpc::channelz::v1::Channel> top_channels_;
  std::vector<grpc::channelz::v1::Server> all_servers_;
  std::vector<grpc::channelz::v1::Channel> all_channels_;
  std::vector<grpc::channelz::v1::Subchannel> all_subchannels_;
  std::vector<grpc::channelz::v1::Socket> all_sockets_;
  std::unordered_set<int64_t> id_set_;
  grpc_core::Json::Array json_;
  int64_t rpc_timeout_seconds_;
  gpr_timespec now_;
};

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  std::ofstream output_file(absl::GetFlag(FLAGS_output_json));
  for (int i = 0; i < absl::GetFlag(FLAGS_sampling_times); ++i) {
    ChannelzSampler channelz_sampler;
    channelz_sampler.Setup(absl::GetFlag(FLAGS_custom_credentials_type),
                           absl::GetFlag(FLAGS_server_address));
    std::cout << "Wait for sampling interval "
              << absl::GetFlag(FLAGS_sampling_interval_seconds) << "s..."
              << std::endl;
    const gpr_timespec kDelay = gpr_time_add(
        gpr_now(GPR_CLOCK_MONOTONIC),
        gpr_time_from_seconds(absl::GetFlag(FLAGS_sampling_interval_seconds),
                              GPR_TIMESPAN));
    gpr_sleep_until(kDelay);
    std::cout << "##### " << i << "th sampling #####" << std::endl;
    channelz_sampler.RecordNow();
    channelz_sampler.GetServersRPC();
    channelz_sampler.GetSocketsOfServers();
    channelz_sampler.GetTopChannelsRPC();
    channelz_sampler.TraverseTopChannels();
    channelz_sampler.DumpStdout();
    if (!absl::GetFlag(FLAGS_output_json).empty()) {
      output_file << channelz_sampler.DumpJson() << "\n" << std::flush;
    }
  }
  output_file.close();
  return 0;
}
