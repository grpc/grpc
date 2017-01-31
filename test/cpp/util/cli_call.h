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

#ifndef GRPC_TEST_CPP_UTIL_CLI_CALL_H
#define GRPC_TEST_CPP_UTIL_CLI_CALL_H

#include <map>

#include <grpc++/channel.h>
#include <grpc++/completion_queue.h>
#include <grpc++/generic/generic_stub.h>
#include <grpc++/support/status.h>
#include <grpc++/support/string_ref.h>

namespace grpc {

class ClientContext;

namespace testing {

// CliCall handles the sending and receiving of generic messages given the name
// of the remote method. This class is only used by GrpcTool. Its thread-safe
// and thread-unsafe methods should not be used together.
class CliCall final {
 public:
  typedef std::multimap<grpc::string, grpc::string> OutgoingMetadataContainer;
  typedef std::multimap<grpc::string_ref, grpc::string_ref>
      IncomingMetadataContainer;

  CliCall(std::shared_ptr<grpc::Channel> channel, const grpc::string& method,
          const OutgoingMetadataContainer& metadata);
  ~CliCall();

  // Perform an unary generic RPC.
  static Status Call(std::shared_ptr<grpc::Channel> channel,
                     const grpc::string& method, const grpc::string& request,
                     grpc::string* response,
                     const OutgoingMetadataContainer& metadata,
                     IncomingMetadataContainer* server_initial_metadata,
                     IncomingMetadataContainer* server_trailing_metadata);

  // Send a generic request message in a synchronous manner. NOT thread-safe.
  void Write(const grpc::string& request);

  // Send a generic request message in a synchronous manner. NOT thread-safe.
  void WritesDone();

  // Receive a generic response message in a synchronous manner.NOT thread-safe.
  bool Read(grpc::string* response,
            IncomingMetadataContainer* server_initial_metadata);

  // Thread-safe write. Must be used with ReadAndMaybeNotifyWrite. Send out a
  // generic request message and wait for ReadAndMaybeNotifyWrite to finish it.
  void WriteAndWait(const grpc::string& request);

  // Thread-safe WritesDone. Must be used with ReadAndMaybeNotifyWrite. Send out
  // WritesDone for gereneric request messages and wait for
  // ReadAndMaybeNotifyWrite to finish it.
  void WritesDoneAndWait();

  // Thread-safe Read. Blockingly receive a generic response message. Notify
  // writes if they are finished when this read is waiting for a resposne.
  bool ReadAndMaybeNotifyWrite(
      grpc::string* response,
      IncomingMetadataContainer* server_initial_metadata);

  // Finish the RPC.
  Status Finish(IncomingMetadataContainer* server_trailing_metadata);

 private:
  std::unique_ptr<grpc::GenericStub> stub_;
  grpc::ClientContext ctx_;
  std::unique_ptr<grpc::GenericClientAsyncReaderWriter> call_;
  grpc::CompletionQueue cq_;
  gpr_mu write_mu_;
  gpr_cv write_cv_;  // Protected by write_mu_;
  bool write_done_;  // Portected by write_mu_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_CLI_CALL_H
