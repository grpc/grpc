/*
 *
 * Copyright 2015, Google Inc.
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
#include <iostream>
#include <thread>

#include "test/cpp/util/create_test_channel.h"
#include "src/cpp/util/time.h"
#include "src/cpp/server/thread_pool.h"
#include <gflags/gflags.h>
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "test/core/util/port.h"

#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google { }
namespace gflags { }
using namespace google;
using namespace gflags;

using std::chrono::system_clock;

// DEFINE_bool(enable_ssl, true, "Whether to use ssl/tls.");

void Call() {}

int main(int argc, char** argv) {
  grpc_init();

  ParseCommandLineFlags(&argc, &argv, true);

  if (argc < grpc::string(argv[1]) != "call") {
    std::cout << "Usage: grpc_cli call server_host:port full_method_string\n"
              << "Example: grpc_cli call service.googleapis.com "
              << "/grpc.cpp.test.util.TestService/Echo " << std::endl;
  }
  grpc::string server_address(argv[2]);
  grpc::string method(argv[3]);
  std::cout << "connecting to " << server_address << std::endl;
  std::cout << "method is " << method << std::endl;

  std::unique_ptr<grpc::Credentials> creds = grpc::GoogleDefaultCredentials();
  std::shared_ptr<grpc::ChannelInterface> channel =
      grpc::CreateChannel(server_address, creds, grpc::ChannelArguments());

  // Parse argument
  Call();

  channel.reset();
  grpc_shutdown();
  return 0;
}



