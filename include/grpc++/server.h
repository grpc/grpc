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

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

#include <grpc++/completion_queue.h>
#include <grpc++/impl/call.h>
#include <grpc++/impl/codegen/grpc_library.h>
#include <grpc++/impl/codegen/server_interface.h>
#include <grpc++/impl/rpc_service_method.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/support/channel_arguments.h>
#include <grpc++/support/config.h>
#include <grpc++/support/status.h>
#include <grpc/compression.h>

struct grpc_server;

namespace grpc {

class AsyncGenericService;
class HealthCheckServiceInterface;
class ServerContext;
class ServerInitializer;

/// Models a gRPC server.
///
/// Servers are configured and started via \a grpc::ServerBuilder.
class Server final : public ServerInterface, private GrpcLibraryCodegen {
 public:
  ~Server();

  /// Block waiting for all work to complete.
  ///
  /// \warning The server must be either shutting down or some other thread must
  /// call \a Shutdown for this function to ever return.
  void Wait() override;

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
    /// Called before server is started.
    virtual void PreServerStart(Server* server) {}
    /// Called after a server port is added.
    virtual void AddPort(Server* server, int port) {}
  };
  /// Set the global callback object. Can only be called once. Does not take
  /// ownership of callbacks, and expects the pointed to object to be alive
  /// until all server objects in the process have been destroyed.
  static void SetGlobalCallbacks(GlobalCallbacks* callbacks);

  // Returns a \em raw pointer to the underlying grpc_server instance.
  grpc_server* c_server();

  /// Returns the health check service.
  HealthCheckServiceInterface* GetHealthCheckService() const {
    return health_check_service_.get();
  }

 private:
  friend class AsyncGenericService;
  friend class ServerBuilder;
  friend class ServerInitializer;

  class SyncRequest;
  class AsyncRequest;
  class ShutdownRequest;

  /// SyncRequestThreadManager is an implementation of ThreadManager. This class
  /// is responsible for polling for incoming RPCs and calling the RPC handlers.
  /// This is only used in case of a Sync server (i.e a server exposing a sync
  /// interface)
  class SyncRequestThreadManager;

  class UnimplementedAsyncRequestContext;
  class UnimplementedAsyncRequest;
  class UnimplementedAsyncResponse;

  /// Server constructors. To be used by \a ServerBuilder only.
  ///
  /// \param max_message_size Maximum message length that the channel can
  /// receive.
  ///
  /// \param args The channel args
  ///
  /// \param sync_server_cqs The completion queues to use if the server is a
  /// synchronous server (or a hybrid server). The server polls for new RPCs on
  /// these queues
  ///
  /// \param min_pollers The minimum number of polling threads per server
  /// completion queue (in param sync_server_cqs) to use for listening to
  /// incoming requests (used only in case of sync server)
  ///
  /// \param max_pollers The maximum number of polling threads per server
  /// completion queue (in param sync_server_cqs) to use for listening to
  /// incoming requests (used only in case of sync server)
  ///
  /// \param sync_cq_timeout_msec The timeout to use when calling AsyncNext() on
  /// server completion queues passed via sync_server_cqs param.
  Server(int max_message_size, ChannelArguments* args,
         std::shared_ptr<std::vector<std::unique_ptr<ServerCompletionQueue>>>
             sync_server_cqs,
         int min_pollers, int max_pollers, int sync_cq_timeout_msec);

  /// Register a service. This call does not take ownership of the service.
  /// The service must exist for the lifetime of the Server instance.
  bool RegisterService(const grpc::string* host, Service* service) override;

  /// Register a generic service. This call does not take ownership of the
  /// service. The service must exist for the lifetime of the Server instance.
  void RegisterAsyncGenericService(AsyncGenericService* service) override;

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
                       ServerCredentials* creds) override;

  /// Start the server.
  ///
  /// \param cqs Completion queues for handling asynchronous services. The
  /// caller is required to keep all completion queues live until the server is
  /// destroyed.
  /// \param num_cqs How many completion queues does \a cqs hold.
  ///
  /// \return true on a successful shutdown.
  bool Start(ServerCompletionQueue** cqs, size_t num_cqs) override;

  void PerformOpsOnCall(CallOpSetInterface* ops, Call* call) override;

  void ShutdownInternal(gpr_timespec deadline) override;

  int max_receive_message_size() const override {
    return max_receive_message_size_;
  };

  grpc_server* server() override { return server_; };

  ServerInitializer* initializer();

  const int max_receive_message_size_;

  /// The following completion queues are ONLY used in case of Sync API i.e if
  /// the server has any services with sync methods. The server uses these
  /// completion queues to poll for new RPCs
  std::shared_ptr<std::vector<std::unique_ptr<ServerCompletionQueue>>>
      sync_server_cqs_;

  /// List of ThreadManager instances (one for each cq in the sync_server_cqs)
  std::vector<std::unique_ptr<SyncRequestThreadManager>> sync_req_mgrs_;

  // Sever status
  std::mutex mu_;
  bool started_;
  bool shutdown_;
  bool shutdown_notified_;  // Was notify called on the shutdown_cv_

  std::condition_variable shutdown_cv_;

  std::shared_ptr<GlobalCallbacks> global_callbacks_;

  std::vector<grpc::string> services_;
  bool has_generic_service_;

  // Pointer to the wrapped grpc_server.
  grpc_server* server_;

  std::unique_ptr<ServerInitializer> server_initializer_;

  std::unique_ptr<HealthCheckServiceInterface> health_check_service_;
  bool health_check_service_disabled_;
};

}  // namespace grpc

#endif  // GRPCXX_SERVER_H
