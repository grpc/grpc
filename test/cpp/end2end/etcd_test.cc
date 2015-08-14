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

extern "C" {
#include "test/core/util/test_config.h"
#include "test/core/util/port.h"
#include "src/core/support/env.h"
#include "src/core/httpcli/httpcli.h"
}
#include "test/cpp/util/echo.grpc.pb.h"
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
#include <gtest/gtest.h>
#include <grpc/grpc.h>
#include <grpc/grpc_etcd.h>
#include <grpc/support/string_util.h>

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;

namespace grpc {
namespace testing {

class EtcdTestServiceImpl
    : public ::grpc::cpp::test::util::TestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    response->set_message(request->message());
    return Status::OK;
  }
};

class EtcdTest : public ::testing::Test {
 protected:
  EtcdTest() {}

  void SetUp() GRPC_OVERRIDE {
    SetUpEtcd();

    // Setup two servers
    int port1 = grpc_pick_unused_port_or_die();
    port1 = 1111;
    server1_ = SetUpServer(port1);
    int port2 = grpc_pick_unused_port_or_die();
    port2 = 2222;
    server2_ = SetUpServer(port2);

    // DeleteInstance("/test/1");

    // Register service /test in etcd
    // RegisterService("/test2");

    // Register service instance /test/1 in etcd
    string value = "111";
    RegisterInstance("/test/1", value);

    // Register service instance /test/2 in etcd
    value = "2222";
    // RegisterInstance("/test/2", value);
  }

  std::unique_ptr<Server> SetUpServer(int port) {
    string server_address = "localhost:" + std::to_string(port);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    builder.RegisterService(&service_);
    std::unique_ptr<Server> server = builder.BuildAndStart();
    return server;
  }

  // Require etcd server running beforehand
  void SetUpEtcd() {
    // Find etcd server address in environment
    // Default is localhost:4001
    etcd_address_ = "127.0.0.1:4001";
    char* addr = gpr_getenv("GRPC_ETCD_SERVER_TEST");
    if (addr != NULL) {
      string addr_str(addr);
      etcd_address_ = addr_str;
      gpr_free(addr);
    }
    gpr_log(GPR_DEBUG, etcd_address_.c_str());

    grpc_httpcli_context_init(&context);
    grpc_pollset_init(&pollset);

    // Registers etcd name resolver in grpc
    grpc_etcd_register();

    // Unregisters all plugins when exit
    atexit(grpc_unregister_all_plugins);
  }

  static void on_http_response(void *arg, const grpc_httpcli_response *response) {
    gpr_log(GPR_DEBUG, response->body);
    gpr_mu_lock(GRPC_POLLSET_MU(&pollset));
    http_done = 1;
    grpc_pollset_kick(&pollset, NULL);
    gpr_mu_unlock(GRPC_POLLSET_MU(&pollset));
  }

  static void send_http_request(const string& method, const string& path, const string& host, const string& body) {
    GPR_ASSERT(method == "GET" || method == "DELETE" || method == "PUT" || method == "POST");
    grpc_httpcli_request request;
    memset(&request, 0, sizeof(request));
    request.host = (char*)host.c_str();
    request.path = (char*)path.c_str();
    request.hdrs[request.hdr_count].key = gpr_strdup("Content-Type");
    request.hdrs[request.hdr_count].value = gpr_strdup("application/x-www-form-urlencoded");
    request.hdr_count++;

    http_done = 0;
    gpr_log(GPR_DEBUG, method.c_str());
    gpr_log(GPR_DEBUG, request.host);
    gpr_log(GPR_DEBUG, request.path);
    gpr_log(GPR_DEBUG, body.c_str());
    gpr_log(GPR_DEBUG, "%d", body.size());

    if (method == "GET") {
      grpc_httpcli_get(&context, &pollset, &request, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(15), on_http_response, NULL);
    } else if (method == "DELETE") {
      grpc_httpcli_delete(&context, &pollset, &request, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(15), on_http_response, NULL);
    } else if (method == "PUT") {
      grpc_httpcli_put(&context, &pollset, &request, body.c_str(), body.size(), GRPC_TIMEOUT_SECONDS_TO_DEADLINE(15), on_http_response, NULL);
    } else if (method == "POST") {
      grpc_httpcli_post(&context, &pollset, &request, body.c_str(), body.size(), GRPC_TIMEOUT_SECONDS_TO_DEADLINE(15), on_http_response, NULL);
    } 
    
    gpr_mu_lock(GRPC_POLLSET_MU(&pollset));
    while (http_done == 0) {
      grpc_pollset_worker worker;
      grpc_pollset_work(&pollset, &worker, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(20));
    }
    gpr_mu_unlock(GRPC_POLLSET_MU(&pollset));
  }

  void RegisterService(const string& name) {
    string path = "/v2/keys" + name;
    string body = "dir=true";
    send_http_request("PUT", path, etcd_address_, body);
  }

  void RegisterInstance(const string& name, const string& value) {
    string path = "/v2/keys" + name;
    string body = "value=" + value;
    send_http_request("PUT", path, etcd_address_, body);
  }

  void DeleteInstance(const string& name) {
    string path = "/v2/keys" + name;
    send_http_request("DELETE", path, etcd_address_, "");
  }

  void ChangeEtcdState() {
    DeleteInstance("/test/1");
  }

  static void destroy_pollset(void *ignored) { 
    grpc_pollset_destroy(&pollset); 
  }

  void TearDown() GRPC_OVERRIDE {
    server1_->Shutdown();
    server2_->Shutdown();

    grpc_httpcli_context_destroy(&context);
    grpc_pollset_shutdown(&pollset, destroy_pollset, NULL);
  }

  void ResetStub() {
    string target = "etcd://" + etcd_address_ + "/test";
    channel_ = CreateChannel(target, InsecureCredentials(), ChannelArguments());
    stub_ = std::move(grpc::cpp::test::util::TestService::NewStub(channel_));
  }

  std::shared_ptr<ChannelInterface> channel_;
  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
  std::unique_ptr<Server> server1_;
  std::unique_ptr<Server> server2_;
  EtcdTestServiceImpl service_;
  string etcd_address_;
  static grpc_httpcli_context context;
  static grpc_pollset pollset;
  static int http_done;
};

grpc_httpcli_context EtcdTest::context;
grpc_pollset EtcdTest::pollset;
int EtcdTest::http_done;

// Simple echo RPC
TEST_F(EtcdTest, SimpleRpc) {
  ResetStub();

  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_authority("test");
  request.set_message("Hello");
  Status s = stub_->Echo(&context, request, &response);
  gpr_log(GPR_DEBUG, "response: %s", response.message().c_str());
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());

  ChangeEtcdState();
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
