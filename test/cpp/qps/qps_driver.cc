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

#include <gflags/gflags.h>

#include "test/cpp/qps/driver.h"

DEFINE_int32(num_clients, 1, "Number of client binaries");
DEFINE_int32(num_servers, 1, "Number of server binaries");

// Common config
DEFINE_bool(enable_ssl, false, "Use SSL");

// Server config
DEFINE_int32(server_threads, 1, "Number of server threads");

// Client config
DEFINE_int32(client_threads, 1, "Number of client threads");
DEFINE_int32(client_channels, 1, "Number of client channels");
DEFINE_int32(num_rpcs, 10000, "Number of rpcs per client thread");
DEFINE_int32(payload_size, 1, "Payload size");

using grpc::testing::ClientConfig;
using grpc::testing::ServerConfig;

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google { }
namespace gflags { }
using namespace google;
using namespace gflags;

int main(int argc, char **argv) {
  grpc_init();
  ParseCommandLineFlags(&argc, &argv, true);

  ClientConfig client_config;
  client_config.set_enable_ssl(FLAGS_enable_ssl);
  client_config.set_client_threads(FLAGS_client_threads);
  client_config.set_client_channels(FLAGS_client_channels);
  client_config.set_num_rpcs(FLAGS_num_rpcs);
  client_config.set_payload_size(FLAGS_payload_size);

  ServerConfig server_config;
  server_config.set_threads(FLAGS_server_threads);
  server_config.set_enable_ssl(FLAGS_enable_ssl);

  RunScenario(client_config, FLAGS_num_clients, server_config, FLAGS_num_servers);

  grpc_shutdown();
  return 0;
}

