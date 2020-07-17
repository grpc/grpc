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
#include <gflags/gflags.h>
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

#include <google/protobuf/text_format.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <queue>
#include <string>
#include "absl/strings/str_format.h"
#include "src/core/lib/json/json.h"
#include "src/cpp/server/channelz/channelz_service.h"
#include "src/proto/grpc/channelz/channelz.pb.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/test_credentials_provider.h"

DEFINE_string(server_address, "", "channelz server address");
DEFINE_string(custom_credentials_type, "", "custom credentials type");
DEFINE_int64(sampling_times, 1, "number of sampling");
DEFINE_int64(sampling_interval_seconds, 0, "sampling interval in seconds");
DEFINE_string(output_json, "", "output filename in json format");

using grpc::channelz::v1::GetChannelRequest;
using grpc::channelz::v1::GetChannelResponse;
using grpc::channelz::v1::GetServerRequest;
using grpc::channelz::v1::GetServerResponse;
using grpc::channelz::v1::GetServerSocketsRequest;
using grpc::channelz::v1::GetServerSocketsResponse;
using grpc::channelz::v1::GetServersRequest;
using grpc::channelz::v1::GetServersResponse;
using grpc::channelz::v1::GetSocketRequest;
using grpc::channelz::v1::GetSocketResponse;
using grpc::channelz::v1::GetSubchannelRequest;
using grpc::channelz::v1::GetSubchannelResponse;
using grpc::channelz::v1::GetTopChannelsRequest;
using grpc::channelz::v1::GetTopChannelsResponse;

using grpc::ClientContext;
using grpc::Status;

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

  // Get a channel based on channel_id
  grpc::channelz::v1::Channel GetChannelRPC(int64_t channel_id) {
    GetChannelRequest get_channel_request;
    get_channel_request.set_channel_id(channel_id);
    GetChannelResponse get_channel_response;
    ClientContext get_channel_context;
    channelz_stub->GetChannel(&get_channel_context, get_channel_request,
                              &get_channel_response);
    return get_channel_response.channel();
  }

  // Get a subchannel based on subchannel_id
  grpc::channelz::v1::Subchannel GetSubchannelRPC(int64_t subchannel_id) {
    GetSubchannelRequest get_subchannel_request;
    get_subchannel_request.set_subchannel_id(subchannel_id);
    GetSubchannelResponse get_subchannel_response;
    ClientContext get_subchannel_context;
    channelz_stub->GetSubchannel(&get_subchannel_context,
                                 get_subchannel_request,
                                 &get_subchannel_response);
    return get_subchannel_response.subchannel();
  }

  // get a socket based on socket_id
  grpc::channelz::v1::Socket GetSocketRPC(int64_t socket_id) {
    GetSocketRequest get_socket_request;
    get_socket_request.set_socket_id(socket_id);
    GetSocketResponse get_socket_response;
    ClientContext get_socket_context;
    channelz_stub->GetSocket(&get_socket_context, get_socket_request,
                             &get_socket_response);
    return get_socket_response.socket();
  }

  // get the descedent channels/subchannels/sockets of a channel
  // push descedent channels/subchannels to queue for layer traverse
  // store descedent channels/subchannels/sockets for dumping data
  void GetChannelDescedence(
      const grpc::channelz::v1::Channel& channel,
      std::queue<grpc::channelz::v1::Channel>& channel_queue,
      std::queue<grpc::channelz::v1::Subchannel>& subchannel_queue) {
    std::cout << "    Channel " << GetChannelID(channel) << " descendence - ";
    if (channel.channel_ref_size() > 0) {
      std::cout << "channel: ";
      for (const auto& _channelref : channel.channel_ref()) {
        int64_t ch_id = _channelref.channel_id();
        std::cout << ch_id << " ";
        grpc::channelz::v1::Channel ch = GetChannelRPC(ch_id);
        channel_queue.push(ch);
        if (CheckID(ch_id)) {
          all_channels.push_back(ch);
          StoreChannelInJson(ch);
        }
      }
    }
    if (channel.subchannel_ref_size() > 0) {
      std::cout << "subchannel: ";
      for (const auto& _subchannelref : channel.subchannel_ref()) {
        int64_t subch_id = _subchannelref.subchannel_id();
        std::cout << subch_id << " ";
        grpc::channelz::v1::Subchannel subch = GetSubchannelRPC(subch_id);
        subchannel_queue.push(subch);
        if (CheckID(subch_id)) {
          all_subchannels.push_back(subch);
          StoreSubchannelInJson(subch);
        }
      }
    }
    if (channel.socket_ref_size() > 0) {
      std::cout << "socket: ";
      for (const auto& _socketref : channel.socket_ref()) {
        int64_t so_id = _socketref.socket_id();
        std::cout << so_id << " ";
        grpc::channelz::v1::Socket so = GetSocketRPC(so_id);
        if (CheckID(so_id)) {
          all_sockets.push_back(so);
          StoreSocketInJson(so);
        }
      }
    }
    std::cout << std::endl;
  }

  // get the descedent channels/subchannels/sockets of a subchannel
  // push descedent channels/subchannels to queue for layer traverse
  // store descedent channels/subchannels/sockets for dumping data
  void GetSubchannelDescedence(
      grpc::channelz::v1::Subchannel& subchannel,
      std::queue<grpc::channelz::v1::Channel>& channel_queue,
      std::queue<grpc::channelz::v1::Subchannel>& subchannel_queue) {
    std::cout << "    Subchannel " << GetSubchannelID(subchannel)
              << " descendence - ";
    if (subchannel.channel_ref_size() > 0) {
      std::cout << "channel: ";
      for (const auto& _channelref : subchannel.channel_ref()) {
        int64_t ch_id = _channelref.channel_id();
        std::cout << ch_id << " ";
        grpc::channelz::v1::Channel ch = GetChannelRPC(ch_id);
        channel_queue.push(ch);
        if (CheckID(ch_id)) {
          all_channels.push_back(ch);
          StoreChannelInJson(ch);
        }
      }
    }
    if (subchannel.subchannel_ref_size() > 0) {
      std::cout << "subchannel: ";
      for (const auto& _subchannelref : subchannel.subchannel_ref()) {
        int64_t subch_id = _subchannelref.subchannel_id();
        std::cout << subch_id << " ";
        grpc::channelz::v1::Subchannel subch = GetSubchannelRPC(subch_id);
        subchannel_queue.push(subch);
        if (CheckID(subch_id)) {
          all_subchannels.push_back(subch);
          StoreSubchannelInJson(subch);
        }
      }
    }
    if (subchannel.socket_ref_size() > 0) {
      std::cout << "socket: ";
      for (const auto& _socketref : subchannel.socket_ref()) {
        int64_t so_id = _socketref.socket_id();
        std::cout << so_id << " ";
        grpc::channelz::v1::Socket so = GetSocketRPC(so_id);
        if (CheckID(so_id)) {
          all_sockets.push_back(so);
          StoreSocketInJson(so);
        }
      }
    }
    std::cout << std::endl;
  }

  // Set up the channelz sampler client
  // Initialize json as an array
  void Setup(std::string custom_credentials_type, std::string server_address) {
    json = grpc_core::Json::Array();
    grpc::ChannelArguments channel_args;
    std::shared_ptr<grpc::ChannelCredentials> channel_creds =
        grpc::testing::GetCredentialsProvider()->GetChannelCredentials(
            custom_credentials_type, &channel_args);
    std::shared_ptr<grpc::Channel> channel =
        CreateChannel(server_address, channel_creds);
    channelz_stub = grpc::channelz::v1::Channelz::NewStub(channel);
  }

  // Get all servers, keep querying until getting all
  // Store servers for dumping data
  // Need to check id repeating for servers
  bool GetServersRPC() {
    int64_t server_start_id = 0;
    while (true) {
      GetServersRequest get_server_request;
      GetServersResponse get_server_response;
      ClientContext get_server_context;
      get_server_request.set_start_server_id(server_start_id);
      Status status = channelz_stub->GetServers(
          &get_server_context, get_server_request, &get_server_response);
      if (!status.ok()) {
        gpr_log(GPR_ERROR, "%s",
                get_server_context.debug_error_string().c_str());
        return false;
      }
      for (const auto& _server : get_server_response.server()) {
        all_servers.push_back(_server);
        StoreServerInJson(_server);
      }
      if (!get_server_response.end()) {
        server_start_id = GetServerID(all_servers.back()) + 1;
      } else {
        break;
      }
    }
    std::cout << "Number of servers = " << all_servers.size() << std::endl;
    return true;
  }

  // Get sockets that belongs to servers
  // Store sockets for dumping data
  void GetSocketsOfServers() {
    for (const auto& _server : all_servers) {
      std::cout << "Server " << GetServerID(_server) << " listen_socket: ";
      for (const auto& _socket : _server.listen_socket()) {
        int64_t so_id = _socket.socket_id();
        std::cout << so_id << " ";
        if (CheckID(so_id)) {
          grpc::channelz::v1::Socket so = GetSocketRPC(so_id);
          all_sockets.push_back(so);
          StoreSocketInJson(so);
        }
      }
      std::cout << std::endl;
    }
  }

  // Get all top channels, keep querying until getting all
  // Store channels for dumping data
  // No need to check id repeating for top channels
  bool GetTopChannelsRPC() {
    int64_t channel_start_id = 0;
    while (true) {
      GetTopChannelsRequest get_top_channels_request;
      GetTopChannelsResponse get_top_channels_response;
      ClientContext get_top_channels_context;
      get_top_channels_request.set_start_channel_id(channel_start_id);
      Status status = channelz_stub->GetTopChannels(&get_top_channels_context,
                                                    get_top_channels_request,
                                                    &get_top_channels_response);
      if (!status.ok()) {
        gpr_log(GPR_ERROR, "%s",
                get_top_channels_context.debug_error_string().c_str());
        return false;
      }
      for (const auto& _topchannel : get_top_channels_response.channel()) {
        top_channels.push_back(_topchannel);
        all_channels.push_back(_topchannel);
        StoreChannelInJson(_topchannel);
      }
      if (!get_top_channels_response.end()) {
        channel_start_id = GetChannelID(top_channels.back()) + 1;
      } else {
        break;
      }
    }
    std::cout << "Number of top channels = " << top_channels.size()
              << std::endl;
    return true;
  }

  // layer traverse for each top channel
  void TraverseTopChannels() {
    for (const auto& _topchannel : top_channels) {
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
    }
  }

  // dump data of all entities to stdout
  void DumpStdout() {
    std::string data_str;
    for (const auto& _channel : all_channels) {
      std::cout << "channel " << GetChannelID(_channel)
                << " data:" << std::endl;
      ::google::protobuf::TextFormat::PrintToString(_channel.data(), &data_str);
      printf("%s", data_str.c_str());
    }
    for (const auto& _subchannel : all_subchannels) {
      std::cout << "subchannel " << GetSubchannelID(_subchannel)
                << " data:" << std::endl;
      ::google::protobuf::TextFormat::PrintToString(_subchannel.data(),
                                                    &data_str);
      printf("%s", data_str.c_str());
    }
    for (const auto& _server : all_servers) {
      std::cout << "server " << GetServerID(_server) << " data:" << std::endl;
      ::google::protobuf::TextFormat::PrintToString(_server.data(), &data_str);
      printf("%s", data_str.c_str());
    }
    for (const auto& _socket : all_sockets) {
      std::cout << "socket " << GetSocketID(_socket) << " data:" << std::endl;
      ::google::protobuf::TextFormat::PrintToString(_socket.data(), &data_str);
      printf("%s", data_str.c_str());
    }
  }

  // Clear stored entities to prepare for the next scan
  void ClearEntities() {
    all_channels.clear();
    all_subchannels.clear();
    all_servers.clear();
    all_sockets.clear();
    top_channels.clear();
    id_set.clear();
  }

  // Store a channel in Json
  void StoreChannelInJson(const grpc::channelz::v1::Channel& channel) {
    std::string id = grpc::to_string(GetChannelID(channel));
    std::string type = "Channel";
    std::string description;
    ::google::protobuf::TextFormat::PrintToString(channel.data(), &description);
    StoreEntityInJson(id, type, description);
  }

  // Store a subchannel in Json
  void StoreSubchannelInJson(const grpc::channelz::v1::Subchannel& subchannel) {
    std::string id = grpc::to_string(GetSubchannelID(subchannel));
    std::string type = "Subchannel";
    std::string description;
    ::google::protobuf::TextFormat::PrintToString(subchannel.data(),
                                                  &description);
    StoreEntityInJson(id, type, description);
  }

  // Store a server in Json
  void StoreServerInJson(const grpc::channelz::v1::Server& server) {
    std::string id = grpc::to_string(GetServerID(server));
    std::string type = "Server";
    std::string description;
    ::google::protobuf::TextFormat::PrintToString(server.data(), &description);
    StoreEntityInJson(id, type, description);
  }

  // Store a socket in Json
  void StoreSocketInJson(const grpc::channelz::v1::Socket& socket) {
    std::string id = grpc::to_string(GetSocketID(socket));
    std::string type = "Socket";
    std::string description;
    ::google::protobuf::TextFormat::PrintToString(socket.data(), &description);
    StoreEntityInJson(id, type, description);
  }

  // Store an entity in Json
  void StoreEntityInJson(std::string& id, std::string& type,
                         std::string& description) {
    std::string start, finish;
    time_t ago = now - int64_t(FLAGS_sampling_interval_seconds);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now), "%F %T");
    finish = ss.str();  // example: "2019-02-01 12:12:18"
    ss.str("");
    ss << std::put_time(std::localtime(&ago), "%F %T");
    start = ss.str();

    grpc_core::Json obj =
        grpc_core::Json::Object{{"Task", absl::StrFormat("%s_ID%s", type, id)},
                                {"Start", start},
                                {"Finish", finish},
                                {"ID", id},
                                {"Type", type},
                                {"Description", description}};
    json.mutable_array()->push_back(obj);
  }

  // Dump data in json
  std::string DumpJson() { return json.Dump(); }

  // Check if one entity has been recorded
  bool CheckID(int64_t id) {
    if (id_set.count(id) == 0) {
      id_set.insert(id);
      return true;
    } else {
      return false;
    }
  }

  time_t now;
  // private:
  std::unique_ptr<grpc::channelz::v1::Channelz::Stub> channelz_stub;
  std::vector<grpc::channelz::v1::Channel> top_channels;
  std::vector<grpc::channelz::v1::Server> all_servers;
  std::vector<grpc::channelz::v1::Channel> all_channels;
  std::vector<grpc::channelz::v1::Subchannel> all_subchannels;
  std::vector<grpc::channelz::v1::Socket> all_sockets;
  std::unordered_set<int64_t> id_set;
  grpc_core::Json json;
};

int main(int argc, char** argv) {
  // make sure flags can be used
  grpc::testing::InitTest(&argc, &argv, true);

  // create a channelz client
  ChannelzSampler channelz_sampler;
  channelz_sampler.Setup(FLAGS_custom_credentials_type, FLAGS_server_address);

  // Keep sampling based on FLAGS_sampling_times and
  for (int i = 0; i < FLAGS_sampling_times; ++i) {
    std::cout << "Wait for sampling interval "
              << FLAGS_sampling_interval_seconds << "s..." << std::endl;
    sleep(FLAGS_sampling_interval_seconds);
    std::cout << "##### " << i << "th sampling #####" << std::endl;
    // Get sampling time
    channelz_sampler.now = time(0);
    // Server side code
    bool isRPCOK = channelz_sampler.GetServersRPC();
    if (!isRPCOK) GPR_ASSERT(0);
    channelz_sampler.GetSocketsOfServers();
    // Client side code
    isRPCOK = channelz_sampler.GetTopChannelsRPC();
    if (!isRPCOK) GPR_ASSERT(0);
    channelz_sampler.TraverseTopChannels();
    // dump data to stdout
    channelz_sampler.DumpStdout();
    // clear stored entities to prepare for the next sampling
    channelz_sampler.ClearEntities();
  }

  // store data in Json
  if (!FLAGS_output_json.empty()) {
    std::ofstream output_file(FLAGS_output_json, std::ios::out);
    output_file << channelz_sampler.DumpJson() << std::flush;
    output_file.close();
  }

  return 0;
}
