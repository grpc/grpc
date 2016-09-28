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

#include "test/cpp/rpcmanager/grpc_rpc_manager_test.h"
#include "test/cpp/util/test_config.h"

using grpc::testing::GrpcRpcManagerTest;

// TODO: sreek - Rewrite this test. Find a better test case

grpc::GrpcRpcManager::WorkStatus GrpcRpcManagerTest::PollForWork(void **tag,
                                                                 bool *ok) {
  {
    std::unique_lock<grpc::mutex> lock(mu_);
    std::cout << "Poll: " << std::this_thread::get_id() << std::endl;
  }

  WorkStatus work_status = WORK_FOUND;
  *tag = nullptr;
  *ok = true;

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  {
    std::unique_lock<grpc::mutex> lock(mu_);
    num_calls_++;
    if (num_calls_ > 50) {
      std::cout << "poll: False" << std::endl;
      work_status = SHUTDOWN;
      ShutdownRpcManager();
    }
  }

  return work_status;
}

void GrpcRpcManagerTest::DoWork(void *tag, bool ok) {
  {
    std::unique_lock<grpc::mutex> lock(mu_);
    std::cout << "Work: " << std::this_thread::get_id() << std::endl;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

int main(int argc, char **argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  GrpcRpcManagerTest test_rpc_manager(3, 15);
  test_rpc_manager.Initialize();
  test_rpc_manager.Wait();

  return 0;
}
