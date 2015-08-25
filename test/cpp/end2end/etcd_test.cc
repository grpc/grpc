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

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <gtest/gtest.h>
#include <grpc/grpc.h>
#include <grpc/grpc_etcd.h>
#include <grpc/support/string_util.h>

#include "test/core/util/test_config.h"
#include "test/core/util/port.h"
#include "test/cpp/util/echo.grpc.pb.h"
#include "src/core/support/env.h"

extern "C" {
#include "src/core/httpcli/httpcli.h"
}

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
    server1_ = SetUpServer(port1);
    int port2 = grpc_pick_unused_port_or_die();
    server2_ = SetUpServer(port2);

    // Register service /test in etcd
    RegisterService("/test");

    // Register service instance /test/1 in etcd
    string value =
        "{\"host\":\"localhost\",\"port\":\"" + to_string(port1) + "\"}";
    RegisterInstance("/test/1", value);

    // Register service instance /test/2 in etcd
    value = "{\"host\":\"localhost\",\"port\":\"" + to_string(port2) + "\"}";
    RegisterInstance("/test/2", value);
  }

  // Requires etcd server running
  void SetUpEtcd() {
    // Finds etcd server address in environment
    // Default is localhost:2379
    etcd_address_ = "localhost:2379";
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
  }

  std::unique_ptr<Server> SetUpServer(int port) {
    string server_address = "localhost:" + to_string(port);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    builder.RegisterService(&service_);
    std::unique_ptr<Server> server = builder.BuildAndStart();
    return server;
  }

  static void on_http_response(void* arg,
                               const grpc_httpcli_response* response) {
    gpr_mu_lock(GRPC_POLLSET_MU(&pollset));
    http_done = 1;
    grpc_pollset_kick(&pollset, NULL);
    gpr_mu_unlock(GRPC_POLLSET_MU(&pollset));
  }

  static void send_http_request(const string& method, const string& path,
                                const string& host, const string& body) {
    GPR_ASSERT(method == "GET" || method == "DELETE" || method == "PUT" ||
               method == "POST");
    grpc_httpcli_request request;
    grpc_httpcli_header hdr = {(char*)"Content-Type",
                               (char*)"application/x-www-form-urlencoded"};
    memset(&request, 0, sizeof(request));
    request.host = gpr_strdup(host.c_str());
    request.path = gpr_strdup(path.c_str());
    request.hdr_count = 1;
    request.hdrs = &hdr;
    http_done = 0;

    if (method == "GET") {
      grpc_httpcli_get(&context, &pollset, &request,
                       GRPC_TIMEOUT_SECONDS_TO_DEADLINE(15), on_http_response,
                       NULL);
    } else if (method == "DELETE") {
      grpc_httpcli_delete(&context, &pollset, &request,
                          GRPC_TIMEOUT_SECONDS_TO_DEADLINE(15),
                          on_http_response, NULL);
    } else if (method == "PUT") {
      grpc_httpcli_put(&context, &pollset, &request, body.c_str(), body.size(),
                       GRPC_TIMEOUT_SECONDS_TO_DEADLINE(15), on_http_response,
                       NULL);
    } else if (method == "POST") {
      grpc_httpcli_post(&context, &pollset, &request, body.c_str(), body.size(),
                        GRPC_TIMEOUT_SECONDS_TO_DEADLINE(15), on_http_response,
                        NULL);
    } else {
      gpr_log(GPR_ERROR, "HTTP method not recognized: %s", method.c_str());
    }

    gpr_mu_lock(GRPC_POLLSET_MU(&pollset));
    while (http_done == 0) {
      grpc_pollset_worker worker;
      grpc_pollset_work(&pollset, &worker, gpr_now(GPR_CLOCK_MONOTONIC),
                        GRPC_TIMEOUT_SECONDS_TO_DEADLINE(20));
    }
    gpr_mu_unlock(GRPC_POLLSET_MU(&pollset));
    gpr_free(request.host);
    gpr_free(request.path);
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
    server2_->Shutdown();
    DeleteInstance("/test/2");
  }

  static void destroy_pollset(void* ignored) { grpc_pollset_destroy(&pollset); }

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

  string to_string(const int number) {
    std::stringstream strs;
    strs << number;
    return strs.str();
  }

  std::shared_ptr<Channel> channel_;
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

// Tests etcd state change between two RPCs
// TODO(ctiller): leaked objects in this test
TEST_F(EtcdTest, EtcdStateChangeTwoRpc) {
  ResetStub();

  // First RPC
  EchoRequest request1;
  EchoResponse response1;
  ClientContext context1;
  context1.set_authority("test");
  request1.set_message("Hello");
  Status s1 = stub_->Echo(&context1, request1, &response1);
  EXPECT_EQ(response1.message(), request1.message());
  EXPECT_TRUE(s1.ok());

  // Etcd state changes
  gpr_log(GPR_DEBUG, "Etcd state change");
  ChangeEtcdState();
  // Waits for re-resolving addresses
  sleep(1);

  // Second RPC
  EchoRequest request2;
  EchoResponse response2;
  ClientContext context2;
  context2.set_authority("test");
  request2.set_message("World");
  Status s2 = stub_->Echo(&context2, request2, &response2);
  EXPECT_EQ(response2.message(), request2.message());
  EXPECT_TRUE(s2.ok());
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
