/*
 *
 * Copyright 2016, Google Inc.
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

#include <thread>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/impl/server_builder_option.h>
#include <grpc++/impl/server_builder_plugin.h>
#include <grpc++/impl/server_initializer.h>
#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

#define PLUGIN_NAME "TestServerBuilderPlugin"

namespace grpc {
namespace testing {

class TestServerBuilderPlugin : public ServerBuilderPlugin {
 public:
  TestServerBuilderPlugin() : service_(new TestServiceImpl()) {
    init_server_is_called_ = false;
    finish_is_called_ = false;
    change_arguments_is_called_ = false;
    register_service_ = false;
  }

  grpc::string name() override { return PLUGIN_NAME; }

  void InitServer(ServerInitializer* si) override {
    init_server_is_called_ = true;
    if (register_service_) {
      si->RegisterService(service_);
    }
  }

  void Finish(ServerInitializer* si) override { finish_is_called_ = true; }

  void ChangeArguments(const grpc::string& name, void* value) override {
    change_arguments_is_called_ = true;
  }

  bool has_async_methods() const override {
    if (register_service_) {
      return service_->has_async_methods();
    }
    return false;
  }

  bool has_sync_methods() const override {
    if (register_service_) {
      return service_->has_synchronous_methods();
    }
    return false;
  }

  void SetRegisterService() { register_service_ = true; }

  bool init_server_is_called() { return init_server_is_called_; }
  bool finish_is_called() { return finish_is_called_; }
  bool change_arguments_is_called() { return change_arguments_is_called_; }

 private:
  bool init_server_is_called_;
  bool finish_is_called_;
  bool change_arguments_is_called_;
  bool register_service_;
  std::shared_ptr<TestServiceImpl> service_;
};

class InsertPluginServerBuilderOption : public ServerBuilderOption {
 public:
  InsertPluginServerBuilderOption() { register_service_ = false; }

  void UpdateArguments(ChannelArguments* arg) override {}

  void UpdatePlugins(
      std::vector<std::unique_ptr<ServerBuilderPlugin>>* plugins) override {
    plugins->clear();

    std::unique_ptr<TestServerBuilderPlugin> plugin(
        new TestServerBuilderPlugin());
    if (register_service_) plugin->SetRegisterService();
    plugins->emplace_back(std::move(plugin));
  }

  void SetRegisterService() { register_service_ = true; }

 private:
  bool register_service_;
};

std::unique_ptr<ServerBuilderPlugin> CreateTestServerBuilderPlugin() {
  return std::unique_ptr<ServerBuilderPlugin>(new TestServerBuilderPlugin());
}

void AddTestServerBuilderPlugin() {
  static bool already_here = false;
  if (already_here) return;
  already_here = true;
  ::grpc::ServerBuilder::InternalAddPluginFactory(
      &CreateTestServerBuilderPlugin);
}

// Force AddServerBuilderPlugin() to be called at static initialization time.
struct StaticTestPluginInitializer {
  StaticTestPluginInitializer() { AddTestServerBuilderPlugin(); }
} static_plugin_initializer_test_;

// When the param boolean is true, the ServerBuilder plugin will be added at the
// time of static initialization. When it's false, the ServerBuilder plugin will
// be added using ServerBuilder::SetOption().
class ServerBuilderPluginTest : public ::testing::TestWithParam<bool> {
 public:
  ServerBuilderPluginTest() {}

  void SetUp() override {
    port_ = grpc_pick_unused_port_or_die();
    builder_.reset(new ServerBuilder());
  }

  void InsertPlugin() {
    if (GetParam()) {
      // Add ServerBuilder plugin in static initialization
      CheckPresent();
    } else {
      // Add ServerBuilder plugin using ServerBuilder::SetOption()
      builder_->SetOption(std::unique_ptr<ServerBuilderOption>(
          new InsertPluginServerBuilderOption()));
    }
  }

  void InsertPluginWithTestService() {
    if (GetParam()) {
      // Add ServerBuilder plugin in static initialization
      auto plugin = CheckPresent();
      EXPECT_TRUE(plugin);
      plugin->SetRegisterService();
    } else {
      // Add ServerBuilder plugin using ServerBuilder::SetOption()
      std::unique_ptr<InsertPluginServerBuilderOption> option(
          new InsertPluginServerBuilderOption());
      option->SetRegisterService();
      builder_->SetOption(std::move(option));
    }
  }

  void StartServer() {
    grpc::string server_address = "localhost:" + to_string(port_);
    builder_->AddListeningPort(server_address, InsecureServerCredentials());
    // we run some tests without a service, and for those we need to supply a
    // frequently polled completion queue
    cq_ = builder_->AddCompletionQueue();
    cq_thread_ = new std::thread(&ServerBuilderPluginTest::RunCQ, this);
    server_ = builder_->BuildAndStart();
    EXPECT_TRUE(CheckPresent());
  }

  void ResetStub() {
    string target = "dns:localhost:" + to_string(port_);
    channel_ = CreateChannel(target, InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  void TearDown() override {
    auto plugin = CheckPresent();
    EXPECT_TRUE(plugin);
    EXPECT_TRUE(plugin->init_server_is_called());
    EXPECT_TRUE(plugin->finish_is_called());
    server_->Shutdown();
    cq_->Shutdown();
    cq_thread_->join();
    delete cq_thread_;
  }

  string to_string(const int number) {
    std::stringstream strs;
    strs << number;
    return strs.str();
  }

 protected:
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<ServerBuilder> builder_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<ServerCompletionQueue> cq_;
  std::unique_ptr<Server> server_;
  std::thread* cq_thread_;
  TestServiceImpl service_;
  int port_;

 private:
  TestServerBuilderPlugin* CheckPresent() {
    auto it = builder_->plugins_.begin();
    for (; it != builder_->plugins_.end(); it++) {
      if ((*it)->name() == PLUGIN_NAME) break;
    }
    if (it != builder_->plugins_.end()) {
      return static_cast<TestServerBuilderPlugin*>(it->get());
    } else {
      return nullptr;
    }
  }

  void RunCQ() {
    void* tag;
    bool ok;
    while (cq_->Next(&tag, &ok))
      ;
  }
};

TEST_P(ServerBuilderPluginTest, PluginWithoutServiceTest) {
  InsertPlugin();
  StartServer();
}

TEST_P(ServerBuilderPluginTest, PluginWithServiceTest) {
  InsertPluginWithTestService();
  StartServer();
  ResetStub();

  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello hello hello hello");
  ClientContext context;
  context.set_compression_algorithm(GRPC_COMPRESS_GZIP);
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());
}

INSTANTIATE_TEST_CASE_P(ServerBuilderPluginTest, ServerBuilderPluginTest,
                        ::testing::Values(false, true));

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
