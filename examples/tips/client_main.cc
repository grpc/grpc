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

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <google/gflags.h>
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "test/cpp/util/create_test_channel.h"

DEFINE_bool(enable_ssl, false, "Whether to use ssl/tls.");
DEFINE_bool(use_prod_roots, false, "True to use SSL roots for production GFE");
DEFINE_int32(server_port, 0, "Server port.");
DEFINE_string(server_host, "127.0.0.1", "Server host to connect to");
DEFINE_string(server_host_override, "foo.test.google.com",
              "Override the server host which is sent in HTTP header");

using grpc::ChannelInterface;
using grpc::ClientContext;
using grpc::CreateTestChannel;

void CreateTopic(std::shared_ptr<ChannelInterface> channel, grpc::string topic);

int main(int argc, char** argv) {
  grpc_init();

  google::ParseCommandLineFlags(&argc, &argv, true);

  gpr_log(GPR_INFO, "Start TIPS client");

  GPR_ASSERT(FLAGS_server_port);
  const int host_port_buf_size = 1024;
  char host_port[host_port_buf_size];
  snprintf(host_port, host_port_buf_size, "%s:%d", FLAGS_server_host.c_str(),
           FLAGS_server_port);

  std::shared_ptr<ChannelInterface> channel(
      CreateTestChannel(host_port, FLAGS_server_host_override, FLAGS_enable_ssl,
                        FLAGS_use_prod_roots));

  CreateTopic(channel, "test");

  channel.reset();
  grpc_shutdown();
  return 0;
}
