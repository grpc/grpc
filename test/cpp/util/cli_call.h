/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
