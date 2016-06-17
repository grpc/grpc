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

#ifndef GRPCXX_SERVER_H
#define GRPCXX_SERVER_H

#include <list>
#include <memory>
#include <vector>

#include <grpc++/completion_queue.h>
#include <grpc++/impl/call.h>
#include <grpc++/impl/codegen/grpc_library.h>
#include <grpc++/impl/codegen/server_interface.h>
#include <grpc++/impl/rpc_service_method.h>
#include <grpc++/impl/sync.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/support/channel_arguments.h>
#include <grpc++/support/config.h>
#include <grpc++/support/status.h>
#include <grpc/compression.h>

struct grpc_server;

namespace grpc {

class GenericServerContext;
class AsyncGenericService;
class ServerAsyncStreamingInterface;
class ServerContext;
class ServerInitializer;
class ThreadPoolInterface;

/// Models a gRPC server.
///
/// Servers are configured and started via \a grpc::ServerBuilder.
class Server GRPC_FINAL : public ServerInterface, private GrpcLibraryCodegen {
 public:
  ~Server();

  /// Block waiting for all work to complete.
  ///
  /// \warning The server must be either shutting down or some other thread must
  /// call \a Shutdown for this function to ever return.
  void Wait() GRPC_OVERRIDE;

  /// Global Callbacks
  ///
  /// Can be set exactly once per application to install hooks whenever
  /// a server event occurs
  class GlobalCallbacks {
   public:
    virtual ~GlobalCallbacks() {}
    /// Called before server is created.
    virtual void UpdateArguments(ChannelArguments* args) {}
    /// Called before application callback for each synchronous server request
    virtual void PreSynchronousRequest(ServerContext* context) = 0;
    /// Called after application callback for each synchronous server request
    virtual void PostSynchronousRequest(ServerContext* context) = 0;
  };
  /// Set the global callback object. Can only be called once. Does not take
  /// ownership of callbacks, and expects the pointed to object to be alive
  /// until all server objects in the process have been destroyed.
  static void SetGlobalCallbacks(GlobalCallbacks* callbacks);

  // Returns a \em raw pointer to the underlying grpc_server instance.
  grpc_server* c_server();

  // Returns a \em raw pointer to the underlying CompletionQueue.
  CompletionQueue* completion_queue();

 private:
  friend class AsyncGenericService;
  friend class ServerBuilder;
  friend class ServerInitializer;

  class SyncRequest;
  class AsyncRequest;
  class ShutdownRequest;

  class UnimplementedAsyncRequestContext;
  class UnimplementedAsyncRequest;
  class UnimplementedAsyncResponse;

  /// Server constructors. To be used by \a ServerBuilder only.
  ///
  /// \param thread_pool The threadpool instance to use for call processing.
  /// \param thread_pool_owned Does the server own the \a thread_pool instance?
  /// \param max_message_size Maximum message length that the channel can
  /// receive.
  Server(ThreadPoolInterface* thread_pool, bool thread_pool_owned,
         int max_message_size, ChannelArguments* args);

  /// Register a service. This call does not take ownership of the service.
  /// The service must exist for the lifetime of the Server instance.
  bool RegisterService(const grpc::string* host,
                       Service* service) GRPC_OVERRIDE;

  /// Register a generic service. This call does not take ownership of the
  /// service. The service must exist for the lifetime of the Server instance.
  void RegisterAsyncGenericService(AsyncGenericService* service) GRPC_OVERRIDE;

  /// Tries to bind \a server to the given \a addr.
  ///
  /// It can be invoked multiple times.
  ///
  /// \param addr The address to try to bind to the server (eg, localhost:1234,
  /// 192.168.1.1:31416, [::1]:27182, etc.).
  /// \params creds The credentials associated with the server.
  ///
  /// \return bound port number on sucess, 0 on failure.
  ///
  /// \warning It's an error to call this method on an already started server.
  int AddListeningPort(const grpc::string& addr,
                       ServerCredentials* creds) GRPC_OVERRIDE;

  /// Start the server.
  ///
  /// \param cqs Completion queues for handling asynchronous services. The
  /// caller is required to keep all completion queues live until the server is
  /// destroyed.
  /// \param num_cqs How many completion queues does \a cqs hold.
  ///
  /// \return true on a successful shutdown.
  bool Start(ServerCompletionQueue** cqs, size_t num_cqs) GRPC_OVERRIDE;

  /// Process one or more incoming calls.
  void RunRpc() GRPC_OVERRIDE;

  /// Schedule \a RunRpc to run in the threadpool.
  void ScheduleCallback() GRPC_OVERRIDE;

  void PerformOpsOnCall(CallOpSetInterface* ops, Call* call) GRPC_OVERRIDE;

  void ShutdownInternal(gpr_timespec deadline) GRPC_OVERRIDE;

  int max_message_size() const GRPC_OVERRIDE { return max_message_size_; };

  grpc_server* server() GRPC_OVERRIDE { return server_; };

  ServerInitializer* initializer();

  const int max_message_size_;

  // Completion queue.
  CompletionQueue cq_;

  // Sever status
  grpc::mutex mu_;
  bool started_;
  bool shutdown_;
  // The number of threads which are running callbacks.
  int num_running_cb_;
  grpc::condition_variable callback_cv_;

  std::shared_ptr<GlobalCallbacks> global_callbacks_;

  std::list<SyncRequest>* sync_methods_;
  std::vector<grpc::string> services_;
  std::unique_ptr<RpcServiceMethod> unknown_method_;
  bool has_generic_service_;

  // Pointer to the c grpc server.
  grpc_server* server_;

  ThreadPoolInterface* thread_pool_;
  // Whether the thread pool is created and owned by the server.
  bool thread_pool_owned_;

  std::unique_ptr<ServerInitializer> server_initializer_;
};

}  // namespace grpc

#endif  // GRPCXX_SERVER_H
