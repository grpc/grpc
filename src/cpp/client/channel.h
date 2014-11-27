/*
 *
 * Copyright 2014, Google Inc.
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

#ifndef __GRPCPP_INTERNAL_CLIENT_CHANNEL_H__
#define __GRPCPP_INTERNAL_CLIENT_CHANNEL_H__

#include <grpc++/channel_interface.h>
#include <grpc++/config.h>

struct grpc_channel;

namespace grpc {
class StreamContextInterface;

class Channel : public ChannelInterface {
 public:
  explicit Channel(const grpc::string& target);
  ~Channel() override;

  Status StartBlockingRpc(const RpcMethod& method, ClientContext* context,
                          const google::protobuf::Message& request,
                          google::protobuf::Message* result) override;

  StreamContextInterface* CreateStream(const RpcMethod& method,
                                       ClientContext* context,
                                       const google::protobuf::Message* request,
                                       google::protobuf::Message* result) override;

 protected:
  // TODO(yangg) remove this section when we have the general ssl channel API
  Channel() {}
  void set_c_channel(grpc_channel* channel) { c_channel_ = channel; }

 private:
  const grpc::string target_;
  grpc_channel* c_channel_;  // owned
};

}  // namespace grpc

#endif  // __GRPCPP_INTERNAL_CLIENT_CHANNEL_H__
