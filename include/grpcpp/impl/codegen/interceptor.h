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

/// An enumeration of different possible points at which the \a Intercept
/// method of the \a Interceptor interface may be called. Any given call
/// to \a Intercept will include one or more of these hook points, and
/// each hook point makes certain types of information available to the
/// interceptor.
/// In these enumeration names, PRE_SEND means that an interception has taken
/// place between the time the application provided a certain type of data
/// (e.g., initial metadata, status) and the time that that data goes to the
/// other side. POST_SEND means that the data has been committed for going to
/// the other side (even if it has not yet been received at the other side).
/// PRE_RECV means an interception between the time that a certain
/// operation has been requested and it is available. POST_RECV means that a
/// result is available but has not yet been passed back to the application.
enum class InterceptionHookPoints {
  /// The first two in this list are for clients and servers
  PRE_SEND_INITIAL_METADATA,
  PRE_SEND_MESSAGE,
  PRE_SEND_STATUS,  // server only
  PRE_SEND_CLOSE,   // client only: WritesDone for stream; after write in unary
  /// The following three are for hijacked clients only and can only be
  /// registered by the global interceptor
  PRE_RECV_INITIAL_METADATA,
  PRE_RECV_MESSAGE,
  PRE_RECV_STATUS,
  /// The following two are for all clients and servers
  POST_RECV_INITIAL_METADATA,
  POST_RECV_MESSAGE,
  POST_RECV_STATUS,  // client only
  POST_RECV_CLOSE,   // server only
  /// This is a special hook point available to both clients and servers when
  /// TryCancel() is performed.
  ///  - No other hook points will be present along with this.
  ///  - It is illegal for an interceptor to block/delay this operation.
  ///  - ALL interceptors see this hook point irrespective of whether the
  ///    RPC was hijacked or not.
  PRE_SEND_CANCEL,
  NUM_INTERCEPTION_HOOKS
};

/// Class that is passed as an argument to the \a Intercept method
/// of the application's \a Interceptor interface implementation. It has five
/// purposes:
///   1. Indicate which hook points are present at a specific interception
///   2. Allow an interceptor to inform the library that an RPC should
///      continue to the next stage of its processing (which may be another
///      interceptor or the main path of the library)
///   3. Allow an interceptor to hijack the processing of the RPC (only for
///      client-side RPCs with PRE_SEND_INITIAL_METADATA) so that it does not
///      proceed with normal processing beyond that stage
///   4. Access the relevant fields of an RPC at each interception point
///   5. Set some fields of an RPC at each interception point, when possible
class InterceptorBatchMethods {
 public:
  virtual ~InterceptorBatchMethods(){};
  /// Determine whether the current batch has an interception hook point
  /// of type \a type
  virtual bool QueryInterceptionHookPoint(InterceptionHookPoints type) = 0;
  /// Signal that the interceptor is done intercepting the current batch of the
  /// RPC. Every interceptor must either call Proceed or Hijack on each
  /// interception. In most cases, only Proceed will be used. Explicit use of
  /// Proceed is what enables interceptors to delay the processing of RPCs
  /// while they perform other work.
  /// Proceed is a no-op if the batch contains PRE_SEND_CANCEL. Simply returning
  /// from the Intercept method does the job of continuing the RPC in this case.
  /// This is because PRE_SEND_CANCEL is always in a separate batch and is not
  /// allowed to be delayed.
  virtual void Proceed() = 0;
  /// Indicate that the interceptor has hijacked the RPC (only valid if the
  /// batch contains send_initial_metadata on the client side). Later
  /// interceptors in the interceptor list will not be called. Later batches
  /// on the same RPC will go through interception, but only up to the point
  /// of the hijacking interceptor.
  virtual void Hijack() = 0;

  /// Returns a modifable ByteBuffer holding the serialized form of the message
  /// that is going to be sent. Valid for PRE_SEND_MESSAGE interceptions.
  /// A return value of nullptr indicates that this ByteBuffer is not valid.
  virtual ByteBuffer* GetSendMessage() = 0;

  /// Returns a modifiable multimap of the initial metadata to be sent. Valid
  /// for PRE_SEND_INITIAL_METADATA interceptions. A value of nullptr indicates
  /// that this field is not valid.
  virtual std::multimap<grpc::string, grpc::string>*
  GetSendInitialMetadata() = 0;

  /// Returns the status to be sent. Valid for PRE_SEND_STATUS interceptions.
  virtual Status GetSendStatus() = 0;

  /// Overwrites the status with \a status. Valid for PRE_SEND_STATUS
  /// interceptions.
  virtual void ModifySendStatus(const Status& status) = 0;

  /// Returns a modifiable multimap of the trailing metadata to be sent. Valid
  /// for PRE_SEND_STATUS interceptions. A value of nullptr indicates
  /// that this field is not valid.
  virtual std::multimap<grpc::string, grpc::string>*
  GetSendTrailingMetadata() = 0;

  /// Returns a pointer to the modifiable received message. Note that the
  /// message is already deserialized but the type is not set; the interceptor
  /// should static_cast to the appropriate type before using it. This is valid
  /// for POST_RECV_MESSAGE interceptions; nullptr for not valid
  virtual void* GetRecvMessage() = 0;

  /// Returns a modifiable multimap of the received initial metadata.
  /// Valid for POST_RECV_INITIAL_METADATA interceptions; nullptr if not valid
  virtual std::multimap<grpc::string_ref, grpc::string_ref>*
  GetRecvInitialMetadata() = 0;

  /// Returns a modifiable view of the received status on POST_RECV_STATUS
  /// interceptions; nullptr if not valid.
  virtual Status* GetRecvStatus() = 0;

  /// Returns a modifiable multimap of the received trailing metadata on
  /// POST_RECV_STATUS interceptions; nullptr if not valid
  virtual std::multimap<grpc::string_ref, grpc::string_ref>*
  GetRecvTrailingMetadata() = 0;

  /// Gets an intercepted channel. When a call is started on this interceptor,
  /// only interceptors after the current interceptor are created from the
  /// factory objects registered with the channel. This allows calls to be
  /// started from interceptors without infinite regress through the interceptor
  /// list.
  virtual std::unique_ptr<ChannelInterface> GetInterceptedChannel() = 0;
};

/// Interface for an interceptor. Interceptor authors must create a class
/// that derives from this parent class.
class Interceptor {
 public:
  virtual ~Interceptor() {}

  /// The one public method of an Interceptor interface. Override this to
  /// trigger the desired actions at the hook points described above.
  virtual void Intercept(InterceptorBatchMethods* methods) = 0;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_INTERCEPTOR_H
