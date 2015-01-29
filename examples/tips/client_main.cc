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
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <google/gflags.h>
#include <grpc++/channel_interface.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>
#include <grpc++/status.h>

#include "examples/tips/client.h"
#include "test/cpp/util/create_test_channel.h"

DEFINE_int32(server_port, 443, "Server port.");
DEFINE_string(server_host,
              "pubsub-staging.googleapis.com", "Server host to connect to");
DEFINE_string(default_service_account, "",
              "Email of GCE default service account");
DEFINE_string(service_account_key_file, "",
              "Path to service account json key file.");
DEFINE_string(oauth_scope, "", "Scope for OAuth tokens.");

grpc::string GetServiceAccountJsonKey() {
  static grpc::string json_key;
  if (json_key.empty()) {
    std::ifstream json_key_file(FLAGS_service_account_key_file);
    std::stringstream key_stream;
    key_stream << json_key_file.rdbuf();
    json_key = key_stream.str();
  }
  return json_key;
}

int main(int argc, char** argv) {
  grpc_init();
  google::ParseCommandLineFlags(&argc, &argv, true);
  gpr_log(GPR_INFO, "Start TIPS client");

  const int host_port_buf_size = 1024;
  char host_port[host_port_buf_size];
  snprintf(host_port, host_port_buf_size, "%s:%d", FLAGS_server_host.c_str(),
           FLAGS_server_port);

  std::unique_ptr<grpc::Credentials> creds;
  if (FLAGS_service_account_key_file != "") {
    grpc::string json_key = GetServiceAccountJsonKey();
    creds = grpc::CredentialsFactory::ServiceAccountCredentials(
        json_key, FLAGS_oauth_scope, std::chrono::hours(1));
  } else {
    creds = grpc::CredentialsFactory::ComputeEngineCredentials();
  }

  std::shared_ptr<grpc::ChannelInterface> channel(
      grpc::CreateTestChannel(
          host_port,
          FLAGS_server_host,
          true,                // enable SSL
          true,                // use prod roots
          creds));

  grpc::examples::tips::Client client(channel);

  grpc::Status s = client.CreateTopic("/topics/stoked-keyword-656/testtopics");
  gpr_log(GPR_INFO, "return code %d, %s", s.code(), s.details().c_str());
  GPR_ASSERT(s.IsOk());

  s = client.GetTopic("/topics/stoked-keyword-656/testtopics");
  gpr_log(GPR_INFO, "return code %d, %s", s.code(), s.details().c_str());
  GPR_ASSERT(s.IsOk());

  s = client.DeleteTopic("/topics/stoked-keyword-656/testtopics");
  gpr_log(GPR_INFO, "return code %d, %s", s.code(), s.details().c_str());
  GPR_ASSERT(s.IsOk());

  channel.reset();
  grpc_shutdown();
  return 0;
}
