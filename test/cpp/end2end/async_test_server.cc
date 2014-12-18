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

#include "test/cpp/end2end/async_test_server.h"

#include <chrono>

#include <grpc/support/log.h>
#include "src/cpp/proto/proto_utils.h"
#include "test/cpp/util/echo.pb.h"
#include <grpc++/async_server.h>
#include <grpc++/async_server_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/status.h>
#include <gtest/gtest.h>

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;

using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::seconds;
using std::chrono::system_clock;

namespace grpc {
namespace testing {

AsyncTestServer::AsyncTestServer() : server_(&cq_), cq_drained_(false) {}

AsyncTestServer::~AsyncTestServer() {}

void AsyncTestServer::AddPort(const grpc::string& addr) {
  server_.AddPort(addr);
}

void AsyncTestServer::Start() { server_.Start(); }

// Return true if deadline actual is within 0.5s from expected.
bool DeadlineMatched(const system_clock::time_point& actual,
                     const system_clock::time_point& expected) {
  microseconds diff_usecs = duration_cast<microseconds>(expected - actual);
  gpr_log(GPR_INFO, "diff_usecs= %d", diff_usecs.count());
  return diff_usecs.count() < 500000 && diff_usecs.count() > -500000;
}

void AsyncTestServer::RequestOneRpc() { server_.RequestOneRpc(); }

void AsyncTestServer::MainLoop() {
  EchoRequest request;
  EchoResponse response;
  void* tag = nullptr;

  RequestOneRpc();

  while (true) {
    CompletionQueue::CompletionType t = cq_.Next(&tag);
    AsyncServerContext* server_context = static_cast<AsyncServerContext*>(tag);
    switch (t) {
      case CompletionQueue::SERVER_RPC_NEW:
        gpr_log(GPR_INFO, "SERVER_RPC_NEW %p", server_context);
        if (server_context) {
          EXPECT_EQ(server_context->method(), "/foo");
          // TODO(ctiller): verify deadline
          server_context->Accept(cq_.cq());
          // Handle only one rpc at a time.
          RequestOneRpc();
          server_context->StartRead(&request);
        }
        break;
      case CompletionQueue::RPC_END:
        gpr_log(GPR_INFO, "RPC_END %p", server_context);
        delete server_context;
        break;
      case CompletionQueue::SERVER_READ_OK:
        gpr_log(GPR_INFO, "SERVER_READ_OK %p", server_context);
        response.set_message(request.message());
        server_context->StartWrite(response, 0);
        break;
      case CompletionQueue::SERVER_READ_ERROR:
        gpr_log(GPR_INFO, "SERVER_READ_ERROR %p", server_context);
        server_context->StartWriteStatus(Status::OK);
        break;
      case CompletionQueue::HALFCLOSE_OK:
        gpr_log(GPR_INFO, "HALFCLOSE_OK %p", server_context);
        // Do nothing, just wait for RPC_END.
        break;
      case CompletionQueue::SERVER_WRITE_OK:
        gpr_log(GPR_INFO, "SERVER_WRITE_OK %p", server_context);
        server_context->StartRead(&request);
        break;
      case CompletionQueue::SERVER_WRITE_ERROR:
        EXPECT_TRUE(0);
        break;
      case CompletionQueue::QUEUE_CLOSED: {
        gpr_log(GPR_INFO, "QUEUE_CLOSED");
        HandleQueueClosed();
        return;
      }
      default:
        EXPECT_TRUE(0);
        break;
    }
  }
}

void AsyncTestServer::HandleQueueClosed() {
  std::unique_lock<std::mutex> lock(cq_drained_mu_);
  cq_drained_ = true;
  cq_drained_cv_.notify_all();
}

void AsyncTestServer::Shutdown() {
  // The server need to be shut down before cq_ as grpc_server flushes all
  // pending requested calls to the completion queue at shutdown.
  server_.Shutdown();
  cq_.Shutdown();
  std::unique_lock<std::mutex> lock(cq_drained_mu_);
  while (!cq_drained_) {
    cq_drained_cv_.wait(lock);
  }
}

}  // namespace testing
}  // namespace grpc
