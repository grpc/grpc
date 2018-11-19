/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_INTERCEPTOR_H
#define GRPCPP_IMPL_CODEGEN_INTERCEPTOR_H

#include <grpc/impl/codegen/grpc_types.h>
#include <grpcpp/impl/codegen/byte_buffer.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/metadata_map.h>

namespace grpc {

class ChannelInterface;
class Status;

namespace experimental {
class InterceptedMessage {
 public:
  template <class M>
  bool Extract(M* msg);  // returns false if definitely invalid extraction
  template <class M>
  M* MutableExtract();
  uint64_t length();  // length on wire
};

enum class InterceptionHookPoints {
  /* The first two in this list are for clients and servers */
  PRE_SEND_INITIAL_METADATA,
  PRE_SEND_MESSAGE,
  PRE_SEND_STATUS /* server only */,
  PRE_SEND_CLOSE /* client only */,
  /* The following three are for hijacked clients only and can only be
     registered by the global interceptor */
  PRE_RECV_INITIAL_METADATA,
  PRE_RECV_MESSAGE,
  PRE_RECV_STATUS,
  /* The following two are for all clients and servers */
  POST_RECV_INITIAL_METADATA,
  POST_RECV_MESSAGE,
  POST_RECV_STATUS /* client only */,
  POST_RECV_CLOSE /* server only */,
  /* This is a special hook point available to both clients and servers when
     TryCancel() is performed.
     - No other hook points will be present along with this.
     - It is illegal for an interceptor to block/delay this operation.
     - ALL interceptors see this hook point irrespective of whether the RPC was
     hijacked or not. */
  PRE_SEND_CANCEL,
  NUM_INTERCEPTION_HOOKS
};

class InterceptorBatchMethods {
 public:
  virtual ~InterceptorBatchMethods(){};
  // Queries to check whether the current batch has an interception hook point
  // of type \a type
  virtual bool QueryInterceptionHookPoint(InterceptionHookPoints type) = 0;
  // Calling this will signal that the interceptor is done intercepting the
  // current batch of the RPC.
  // Proceed is a no-op if the batch contains PRE_SEND_CANCEL. Simply returning
  // from the Intercept method does the job of continuing the RPC in this case.
  virtual void Proceed() = 0;
  // Calling this indicates that the interceptor has hijacked the RPC (only
  // valid if the batch contains send_initial_metadata on the client side)
  virtual void Hijack() = 0;

  // Returns a modifable ByteBuffer holding serialized form of the message to be
  // sent
  virtual ByteBuffer* GetSendMessage() = 0;

  // Returns a modifiable multimap of the initial metadata to be sent
  virtual std::multimap<grpc::string, grpc::string>*
  GetSendInitialMetadata() = 0;

  // Returns the status to be sent
  virtual Status GetSendStatus() = 0;

  // Modifies the status with \a status
  virtual void ModifySendStatus(const Status& status) = 0;

  // Returns a modifiable multimap of the trailing metadata to be sent
  virtual std::multimap<grpc::string, grpc::string>*
  GetSendTrailingMetadata() = 0;

  // Returns a pointer to the modifiable received message. Note that the message
  // is already deserialized
  virtual void* GetRecvMessage() = 0;

  // Returns a modifiable multimap of the received initial metadata
  virtual std::multimap<grpc::string_ref, grpc::string_ref>*
  GetRecvInitialMetadata() = 0;

  // Returns a modifiable view of the received status
  virtual Status* GetRecvStatus() = 0;

  // Returns a modifiable multimap of the received trailing metadata
  virtual std::multimap<grpc::string_ref, grpc::string_ref>*
  GetRecvTrailingMetadata() = 0;

  // Gets an intercepted channel. When a call is started on this interceptor,
  // only interceptors after the current interceptor are created from the
  // factory objects registered with the channel.
  virtual std::unique_ptr<ChannelInterface> GetInterceptedChannel() = 0;
};

class Interceptor {
 public:
  virtual ~Interceptor() {}

  virtual void Intercept(InterceptorBatchMethods* methods) = 0;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_INTERCEPTOR_H
