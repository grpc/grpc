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
 *is % allowed in string
 */

#include <chrono>
#include <memory>
#include <string>

#include <gflags/gflags.h>
#include <grpc++/grpc++.h>
#include <grpc/support/log.h>

#include "test/cpp/rpcmanager/grpc_rpc_manager_test.h"
#include "test/cpp/util/test_config.h"

using grpc::testing::GrpcRpcManagerTest;

static const int kMinPollers = 2;
static const int kMaxPollers = 10;

static const int kPollingTimeoutMsec = 10;
static const int kDoWorkDurationMsec = 1;

static const int kNumDoWorkIterations = 10;

grpc::GrpcRpcManager::WorkStatus GrpcRpcManagerTest::PollForWork(void **tag,
                                                                 bool *ok) {
  {
    std::unique_lock<grpc::mutex> lock(mu_);
    gpr_log(GPR_INFO, "PollForWork: Entered");
  }

  WorkStatus work_status = WORK_FOUND;
  *tag = nullptr;
  *ok = true;

  // Simulate "polling for work" by sleeping for sometime
  std::this_thread::sleep_for(std::chrono::milliseconds(kPollingTimeoutMsec));

  {
    std::unique_lock<grpc::mutex> lock(mu_);
    num_calls_++;
    if (num_calls_ > kNumDoWorkIterations) {
      gpr_log(GPR_DEBUG, "PollForWork: Returning shutdown");
      work_status = SHUTDOWN;
      ShutdownRpcManager();
    }
  }

  return work_status;
}

void GrpcRpcManagerTest::DoWork(void *tag, bool ok) {
  {
    std::unique_lock<grpc::mutex> lock(mu_);
    gpr_log(GPR_DEBUG, "DoWork()");
  }

  // Simulate "doing work" by sleeping
  std::this_thread::sleep_for(std::chrono::milliseconds(kDoWorkDurationMsec));
}

int main(int argc, char **argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  GrpcRpcManagerTest test_rpc_manager(kMinPollers, kMaxPollers);
  test_rpc_manager.Initialize();
  test_rpc_manager.Wait();

  return 0;
}
