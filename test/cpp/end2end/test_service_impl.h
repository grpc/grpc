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
#ifndef GRPC_TEST_CPP_END2END_TEST_SERVICE_IMPL_H
#define GRPC_TEST_CPP_END2END_TEST_SERVICE_IMPL_H

#include <memory>
#include <mutex>

#include <grpc++/server_context.h>
#include <grpc/grpc.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"

namespace grpc {
namespace testing {

const int kNumResponseStreamsMsgs = 3;
const char* const kServerCancelAfterReads = "cancel_after_reads";
const char* const kServerTryCancelRequest = "server_try_cancel";
const char* const kDebugInfoTrailerKey = "debug-info-bin";
const char* const kServerFinishAfterNReads = "server_finish_after_n_reads";
const char* const kServerUseCoalescingApi = "server_use_coalescing_api";

typedef enum {
  DO_NOT_CANCEL = 0,
  CANCEL_BEFORE_PROCESSING,
  CANCEL_DURING_PROCESSING,
  CANCEL_AFTER_PROCESSING
} ServerTryCancelRequestPhase;

class TestServiceImpl : public ::grpc::testing::EchoTestService::Service {
 public:
  TestServiceImpl() : signal_client_(false), host_() {}
  explicit TestServiceImpl(const grpc::string& host)
      : signal_client_(false), host_(new grpc::string(host)) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override;

  // Unimplemented is left unimplemented to test the returned error.

  Status RequestStream(ServerContext* context,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) override;

  Status ResponseStream(ServerContext* context, const EchoRequest* request,
                        ServerWriter<EchoResponse>* writer) override;

  Status BidiStream(
      ServerContext* context,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override;

  bool signal_client() {
    std::unique_lock<std::mutex> lock(mu_);
    return signal_client_;
  }

 private:
  int GetIntValueFromMetadata(
      const char* key,
      const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
      int default_value);

  void ServerTryCancel(ServerContext* context);

 private:
  bool signal_client_;
  std::mutex mu_;
  std::unique_ptr<grpc::string> host_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_TEST_SERVICE_IMPL_H
