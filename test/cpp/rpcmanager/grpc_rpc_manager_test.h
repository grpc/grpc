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
#ifndef GRPC_TEST_CPP_GRPC_RPC_MANAGER_TEST_H
#define GRPC_TEST_CPP_GRPC_RPC_MANAGER_TEST_H

#include "src/cpp/rpcmanager/grpc_rpc_manager.h"

namespace grpc {
namespace testing {

class GrpcRpcManagerTest GRPC_FINAL : public GrpcRpcManager {
 public:
  GrpcRpcManagerTest(int min_pollers, int max_pollers, int max_threads)
      : GrpcRpcManager(min_pollers, max_pollers, max_threads), num_calls_(0){};

  void PollForWork(bool &is_work_found) GRPC_OVERRIDE;
  void DoWork() GRPC_OVERRIDE;

 private:
  grpc::mutex mu_;
  int num_calls_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_GRPC_RPC_MANAGER_TEST_H
