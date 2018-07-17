/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/cpp/server/channelz/channelz_server.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

namespace grpc {

ChannelzServer::ChannelzServer() {}

Status ChannelzServer::GetTopChannels(
    ServerContext* context, const channelz::v1::GetTopChannelsRequest* request,
    channelz::v1::GetTopChannelsResponse* response) {
  char* json_str = grpc_channelz_get_top_channels(request->start_channel_id());
  gpr_log(GPR_ERROR, "%s", json_str);
  google::protobuf::util::Status s =
      google::protobuf::util::JsonStringToMessage(json_str, response);
  gpr_free(json_str);
  if (s != google::protobuf::util::Status::OK) {
    return Status(INTERNAL, s.ToString());
  }
  return Status::OK;
}

Status ChannelzServer::GetChannel(
    ServerContext* context, const channelz::v1::GetChannelRequest* request,
    channelz::v1::GetChannelResponse* response) {
  char* json_str = grpc_channelz_get_channel(request->channel_id());
  gpr_log(GPR_ERROR, "%s", json_str);
  google::protobuf::util::Status s =
      google::protobuf::util::JsonStringToMessage(json_str, response);
  gpr_free(json_str);
  if (s != google::protobuf::util::Status::OK) {
    return Status(INTERNAL, s.ToString());
  }
  return Status::OK;
}

}  // namespace grpc
