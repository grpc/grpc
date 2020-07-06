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

#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <queue>
#include <string>
#include "src/cpp/server/channelz/channelz_service.h"
#include "src/proto/grpc/channelz/channelz.pb.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/test_credentials_provider.h"

DEFINE_string(server_address, "", "channelz server address");
DEFINE_string(custom_credentials_type, "", "custom credentials type");
// "INSECURE_CREDENTIALS" - InsecureChannelCredentials
// "alts" - AltsCredentials
// "ssl" - SslCredentials
// "google_default_credentials" - GoogleDefaultCredentials: tls connection +
// oauth token

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

int64_t GetServerID(grpc::channelz::v1::Server& server) {
  return server.ref().server_id();
}
int64_t GetChannelID(grpc::channelz::v1::Channel& channel) {
  return channel.ref().channel_id();
}
int64_t GetSubchannelID(grpc::channelz::v1::Subchannel& subchannel) {
  return subchannel.ref().subchannel_id();
}
int64_t GetSocketID(grpc::channelz::v1::Socket& socket) {
  return socket.ref().socket_id();
}

void GetChannelRPC(int64_t channel_id,
                   grpc::channelz::v1::Channelz::Stub* channelz_stub,
                   std::queue<grpc::channelz::v1::Channel>& channel_queue) {
  GetChannelRequest get_channel_request;
  get_channel_request.set_channel_id(channel_id);
  GetChannelResponse get_channel_response;
  ClientContext get_channel_context;
  channelz_stub->GetChannel(&get_channel_context, get_channel_request,
                            &get_channel_response);
  channel_queue.push(get_channel_response.channel());
}

void GetSubchannelRPC(
    int64_t subchannel_id, grpc::channelz::v1::Channelz::Stub* channelz_stub,
    std::queue<grpc::channelz::v1::Subchannel>& subchannel_queue) {
  GetSubchannelRequest get_subchannel_request;
  get_subchannel_request.set_subchannel_id(subchannel_id);
  GetSubchannelResponse get_subchannel_response;
  ClientContext get_subchannel_context;
  channelz_stub->GetSubchannel(&get_subchannel_context, get_subchannel_request,
                               &get_subchannel_response);
  subchannel_queue.push(get_subchannel_response.subchannel());
}

void GetChannelDescedence(
    grpc::channelz::v1::Channelz::Stub* channelz_stub,
    grpc::channelz::v1::Channel& channel,
    std::queue<grpc::channelz::v1::Channel>& channel_queue,
    std::queue<grpc::channelz::v1::Subchannel>& subchannel_queue) {
  std::cout << "    Channel " << GetChannelID(channel) << " descendence - ";
  if (channel.channel_ref_size() > 0) {
    std::cout << "channel: ";
    for (auto& i : channel.channel_ref()) {
      int64_t ch_id = i.channel_id();
      std::cout << ch_id << " ";
      GetChannelRPC(ch_id, channelz_stub, channel_queue);
    }
  }
  if (channel.subchannel_ref_size() > 0) {
    std::cout << "subchannel: ";
    for (auto& i : channel.subchannel_ref()) {
      int64_t subch_id = i.subchannel_id();
      std::cout << subch_id << " ";
      GetSubchannelRPC(subch_id, channelz_stub, subchannel_queue);
    }
  }
  if (channel.socket_ref_size() > 0) {
    std::cout << "socket: ";
    for (auto& i : channel.socket_ref()) {
      int64_t so_id = i.socket_id();
      std::cout << so_id << " ";
    }
  }
  std::cout << std::endl;
}

void GetSubchannelDescedence(
    grpc::channelz::v1::Channelz::Stub* channelz_stub,
    grpc::channelz::v1::Subchannel& subchannel,
    std::queue<grpc::channelz::v1::Channel>& channel_queue,
    std::queue<grpc::channelz::v1::Subchannel>& subchannel_queue) {
  std::cout << "    Subchannel " << GetSubchannelID(subchannel)
            << " descendence - ";
  if (subchannel.channel_ref_size() > 0) {
    std::cout << "channel: ";
    for (auto& i : subchannel.channel_ref()) {
      int64_t ch_id = i.channel_id();
      std::cout << ch_id << " ";
      GetChannelRPC(ch_id, channelz_stub, channel_queue);
    }
  }
  if (subchannel.subchannel_ref_size() > 0) {
    std::cout << "subchannel: ";
    for (auto& i : subchannel.subchannel_ref()) {
      int64_t subch_id = i.subchannel_id();
      std::cout << subch_id << " ";
      GetSubchannelRPC(subch_id, channelz_stub, subchannel_queue);
    }
  }
  if (subchannel.socket_ref_size() > 0) {
    std::cout << "socket: ";
    for (auto& i : subchannel.socket_ref()) {
      int64_t so_id = i.socket_id();
      std::cout << so_id << " ";
    }
  }
  std::cout << std::endl;
}

int main(int argc, char** argv) {
  // make sure flags can be used
  grpc::testing::InitTest(&argc, &argv, true);
  std::cout << "server address: " << FLAGS_server_address << std::endl;
  std::cout << "custom credentials type: " << FLAGS_custom_credentials_type
            << std::endl;

  // create a channelz client
  ::grpc::channelz::experimental::InitChannelzService();
  grpc::ChannelArguments channel_args;
  std::shared_ptr<grpc::ChannelCredentials> channel_creds =
      grpc::testing::GetCredentialsProvider()->GetChannelCredentials(
          FLAGS_custom_credentials_type, &channel_args);
  std::shared_ptr<grpc::Channel> channel =
      CreateChannel(FLAGS_server_address, channel_creds);
  std::unique_ptr<grpc::channelz::v1::Channelz::Stub> channelz_stub =
      grpc::channelz::v1::Channelz::NewStub(channel);

  // Server side code
  // Get all servers by calling GetServers
  GetServersRequest get_server_request;
  GetServersResponse get_server_response;
  ClientContext get_server_context;
  std::vector<grpc::channelz::v1::Server> top_servers;
  int64_t server_start_id = 0;
  while (true) {
    get_server_request.set_start_server_id(server_start_id);
    Status status = channelz_stub->GetServers(
        &get_server_context, get_server_request, &get_server_response);
    if (!status.ok()) {
      std::cout << "Channelz failed: " << status.error_message() << std::endl;
      return 1;
    }
    for (auto& i : get_server_response.server()) {
      top_servers.push_back(i);
    }
    if (!get_server_response.end()) {
      server_start_id = GetServerID(top_servers.back()) + 1;
    } else {
      break;
    }
  }
  std::cout << "Number of servers = " << top_servers.size() << std::endl;

  // print out sockets of each server
  for (auto& i : top_servers) {
    std::cout << "Server " << GetServerID(i) << " listen_socket: ";
    for (auto& j : i.listen_socket()) {
      std::cout << j.socket_id() << " ";
    }
    std::cout << std::endl;
  }
  std::cout << std::endl;

  // Client side code
  // Get all top channels by calling GetTopChannels
  GetTopChannelsRequest get_top_channels_request;
  GetTopChannelsResponse get_top_channels_response;
  ClientContext get_top_channels_context;
  std::vector<grpc::channelz::v1::Channel> top_channels;
  int64_t channel_start_id = 0;
  while (true) {
    get_top_channels_request.set_start_channel_id(channel_start_id);
    Status status = channelz_stub->GetTopChannels(&get_top_channels_context,
                                                  get_top_channels_request,
                                                  &get_top_channels_response);
    if (!status.ok()) {
      std::cout << "Channelz failed: " << status.error_message() << std::endl;
      return 1;
    }
    for (auto& i : get_top_channels_response.channel()) {
      top_channels.push_back(i);
    }
    if (!get_top_channels_response.end()) {
      channel_start_id = GetChannelID(top_channels.back()) + 1;
    } else {
      break;
    }
  }
  std::cout << "Number of top channels = " << top_channels.size() << std::endl;

  // Layer traverse the tree for each top channels
  for (auto& i : top_channels) {
    int tree_depth = 0;
    std::queue<grpc::channelz::v1::Channel> channel_queue;
    std::queue<grpc::channelz::v1::Subchannel> subchannel_queue;
    std::cout << "Tree depth = " << tree_depth << std::endl;
    GetChannelDescedence(channelz_stub.get(), i, channel_queue,
                         subchannel_queue);

    while (!channel_queue.empty() || !subchannel_queue.empty()) {
      ++tree_depth;
      std::cout << "Tree depth = " << tree_depth << std::endl;
      int ch_q_size = channel_queue.size();
      int subch_q_size = subchannel_queue.size();
      for (int i = 0; i < ch_q_size; ++i) {
        grpc::channelz::v1::Channel ch = channel_queue.front();
        channel_queue.pop();
        GetChannelDescedence(channelz_stub.get(), ch, channel_queue,
                             subchannel_queue);
      }
      for (int i = 0; i < subch_q_size; ++i) {
        grpc::channelz::v1::Subchannel subch = subchannel_queue.front();
        subchannel_queue.pop();
        GetSubchannelDescedence(channelz_stub.get(), subch, channel_queue,
                                subchannel_queue);
      }
    }
    std::cout << std::endl;
  }

  // store result
  // std::ofstream output_file("output.txt", std::ios::out);
  // output_file << "test" << std::flush;
  // output_file.close();

  return 0;
}
