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

#ifndef __GRPCPP_TEST_END2END_ASYNC_TEST_SERVER_H__
#define __GRPCPP_TEST_END2END_ASYNC_TEST_SERVER_H__

#include <condition_variable>
#include <mutex>
#include <string>

#include <grpc++/async_server.h>
#include <grpc++/completion_queue.h>

namespace grpc {

namespace testing {

class AsyncTestServer {
 public:
  AsyncTestServer();
  virtual ~AsyncTestServer();

  void AddPort(const grpc::string& addr);
  void Start();
  void RequestOneRpc();
  virtual void MainLoop();
  void Shutdown();

  CompletionQueue* completion_queue() { return &cq_; }

 protected:
  void HandleQueueClosed();

 private:
  CompletionQueue cq_;
  AsyncServer server_;
  bool cq_drained_;
  std::mutex cq_drained_mu_;
  std::condition_variable cq_drained_cv_;
};

}  // namespace testing
}  // namespace grpc

#endif  // __GRPCPP_TEST_END2END_ASYNC_TEST_SERVER_H__
