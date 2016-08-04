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

/**
 * Compatibility for GCC 4.4
 */
#if (__GNUC__ == 4) && (__GNUC_MINOR__ <= 4)
// Workaround macro bug
#define _GLIBCXX_USE_NANOSLEEP
#endif

#include <mutex>
#include <chrono>
#include <thread>
#include <condition_variable>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/auth_metadata_processor.h>
#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <gtest/gtest.h>

// Import the relevant bits in gRPC-C runtime
extern "C" {
#include <grpc_c/grpc_c.h>
#include <grpc_c/channel.h>
}

// Import the C client which actually runs the tests
extern "C" {
#include "test/c/end2end/end2end_test_client.h"
}

#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/string_ref_helper.h"
#include "test/cpp/util/test_credentials_provider.h"

/**
 * End-to-end tests for the gRPC C API.
 * This test involves generated code as well.
 * As of early July 2016, this C API does not support creating servers, so we pull in a server implementation for C++
 * and put this test under the C++ build.
 */

namespace grpc {
namespace testing {
namespace {

class End2endTest {
public:
  End2endTest()
    : is_server_started_(false),
      kMaxMessageSize_(8192),
      c_channel_(NULL) {
  }

  ~End2endTest() {
    GRPC_channel_destroy(&c_channel_);
  }

  void TearDown() {
    if (is_server_started_) {
      server_->Shutdown();
    }
  }

  void StartServer(const std::shared_ptr<AuthMetadataProcessor> &processor) {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "127.0.0.1:" << port;
    // Setup server
    ServerBuilder builder;

    builder.AddListeningPort(server_address_.str(), InsecureServerCredentials());
    builder.RegisterService(&service_);
    builder.SetMaxMessageSize(
      kMaxMessageSize_);  // For testing max message size.
    server_ = builder.BuildAndStart();
    is_server_started_ = true;
  }

  void ResetChannel() {
    if (!is_server_started_) {
      StartServer(std::shared_ptr<AuthMetadataProcessor>());
    }
    EXPECT_TRUE(is_server_started_);

    // TODO(yifeit): add credentials
    if (c_channel_) GRPC_channel_destroy(&c_channel_);
    c_channel_ = GRPC_channel_create(server_address_.str().c_str());
  }

  void ResetStub() {
    ResetChannel();
  }

  bool is_server_started_;

  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  const int kMaxMessageSize_;
  TestServiceImpl service_;
  grpc::string user_agent_prefix_;

  GRPC_channel *c_channel_;
};

class UnaryEnd2endTest : public End2endTest {
protected:
};

class ClientStreamingEnd2endTest : public End2endTest {
protected:
};

class ServerStreamingEnd2endTest : public End2endTest {
protected:
};

class BidiStreamingEnd2endTest : public End2endTest {
protected:
};

class AsyncUnaryEnd2endTest : public End2endTest {
protected:
};

TEST(End2endTest, UnaryRpc) {
  UnaryEnd2endTest test;
  test.ResetStub();
  test_client_send_unary_rpc(test.c_channel_, 3);
  test.TearDown();
}

static const int kNumThreads = 50;

void racing_thread(UnaryEnd2endTest& test, bool& start_racing, std::mutex& mu, std::condition_variable& cv, int id) {
  unsigned int seed = time(NULL) * kNumThreads + id;
  {
    std::unique_lock<std::mutex> lock(mu);
    while (!start_racing) {
      cv.wait(lock);
    }
  }
  for (int j = 0; j < 5; j++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(rand_r(&seed) % 3 + 1));
    test_client_send_unary_rpc(test.c_channel_, 5);
  }
}

TEST(End2endTest, UnaryRpcRacing) {
  UnaryEnd2endTest test;
  test.ResetStub();
  std::vector<std::thread> threads;
  std::mutex mu;
  std::condition_variable cv;
  bool start_racing = false;
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread(racing_thread, std::ref(test), std::ref(start_racing), std::ref(mu), std::ref(cv), i));
  }
  {
    std::unique_lock<std::mutex> lock(mu);
    start_racing = true;
    cv.notify_all();
  }
  for (int i = 0; i < kNumThreads; i++) {
    threads[i].join();
  }
  test.TearDown();
}

TEST(End2endTest, ClientStreamingRpc) {
  ClientStreamingEnd2endTest test;
  test.ResetStub();
  test_client_send_client_streaming_rpc(test.c_channel_, 3);
  test.TearDown();
}

TEST(End2endTest, ServerStreamingRpc) {
  ServerStreamingEnd2endTest test;
  test.ResetStub();
  test_client_send_server_streaming_rpc(test.c_channel_, 3);
  test.TearDown();
}

TEST(End2endTest, BidiStreamingRpc) {
  BidiStreamingEnd2endTest test;
  test.ResetStub();
  test_client_send_bidi_streaming_rpc(test.c_channel_, 3);
  test.TearDown();
}

TEST(End2endTest, AsyncUnaryRpc) {
  AsyncUnaryEnd2endTest test;
  test.ResetStub();
  test_client_send_async_unary_rpc(test.c_channel_, 3);
  test.TearDown();
}

} // namespace
} // namespace testing
} // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
