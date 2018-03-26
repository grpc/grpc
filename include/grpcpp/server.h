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

#ifndef GRPCPP_SERVER_H
#define GRPCPP_SERVER_H

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

#include <grpc/compression.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/impl/call.h>
#include <grpcpp/impl/codegen/grpc_library.h>
#include <grpcpp/impl/codegen/server_interface.h>
#include <grpcpp/impl/rpc_service_method.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/config.h>
#include <grpcpp/support/status.h>

struct grpc_server;

namespace grpc {

class AsyncGenericService;
class HealthCheckServiceInterface;
class ServerContext;
class ServerInitializer;

/// Represents a gRPC server.
///
/// Use a \a grpc::ServerBuilder to create, configure, and start
/// \a Server instances.
class Server : public ServerInterface, private GrpcLibraryCodegen {
 public:
  ~Server();

  /// Block until the server shuts down.
  ///
  /// \warning The server must be either shutting down or some other thread must
  /// call \a Shutdown for this function to ever return.
  void Wait() override;

  /// Global callbacks are a set of hooks that are called when server
  /// events occur.  \a SetGlobalCallbacks method is used to register
  /// the hooks with gRPC.  Note that
  /// the \a GlobalCallbacks instance will be shared among all
  /// \a Server instances in an application and can be set exactly
  /// once per application.
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
    virtual void AddPort(Server* server, const grpc::string& addr,
                         ServerCredentials* creds, int port) {}
  };
  /// Set the global callback object. Can only be called once per application.
  /// Does not take ownership of callbacks, and expects the pointed to object
  /// to be alive until all server objects in the process have been destroyed.
  /// The same \a GlobalCallbacks object will be used throughout the
  /// application and is shared among all \a Server objects.
  static void SetGlobalCallbacks(GlobalCallbacks* callbacks);

  /// Returns a \em raw pointer to the underlying \a grpc_server instance.
  /// EXPERIMENTAL:  for internal/test use only
  grpc_server* c_server();

  /// Returns the health check service.
  HealthCheckServiceInterface* GetHealthCheckService() const {
    return health_check_service_.get();
  }

  /// Establish a channel for in-process communication
  std::shared_ptr<Channel> InProcessChannel(const ChannelArguments& args);

 protected:
  /// Register a service. This call does not take ownership of the service.
  /// The service must exist for the lifetime of the Server instance.
  bool RegisterService(const grpc::string* host, Service* service) override;

  /// Try binding the server to the given \a addr endpoint
  /// (port, and optionally including IP address to bind to).
  ///
  /// It can be invoked multiple times. Should be used before
  /// starting the server.
  ///
  /// \param addr The address to try to bind to the server (eg, localhost:1234,
  /// 192.168.1.1:31416, [::1]:27182, etc.).
  /// \param creds The credentials associated with the server.
  ///
  /// \return bound port number on success, 0 on failure.
  ///
  /// \warning It is an error to call this method on an already started server.
  int AddListeningPort(const grpc::string& addr,
                       ServerCredentials* creds) override;

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

  /// Start the server.
  ///
  /// \param cqs Completion queues for handling asynchronous services. The
  /// caller is required to keep all completion queues live until the server is
  /// destroyed.
  /// \param num_cqs How many completion queues does \a cqs hold.
  void Start(ServerCompletionQueue** cqs, size_t num_cqs) override;

  grpc_server* server() override { return server_; };

 private:
  friend class AsyncGenericService;
  friend class ServerBuilder;
  friend class ServerInitializer;

  class SyncRequest;
  class UnimplementedAsyncRequest;
  class UnimplementedAsyncResponse;

  /// SyncRequestThreadManager is an implementation of ThreadManager. This class
  /// is responsible for polling for incoming RPCs and calling the RPC handlers.
  /// This is only used in case of a Sync server (i.e a server exposing a sync
  /// interface)
  class SyncRequestThreadManager;

  /// Register a generic service. This call does not take ownership of the
  /// service. The service must exist for the lifetime of the Server instance.
  void RegisterAsyncGenericService(AsyncGenericService* service) override;

  void PerformOpsOnCall(internal::CallOpSetInterface* ops,
                        internal::Call* call) override;

  void ShutdownInternal(gpr_timespec deadline) override;

  int max_receive_message_size() const override {
    return max_receive_message_size_;
  };

  ServerInitializer* initializer();

  const int max_receive_message_size_;

  /// The following completion queues are ONLY used in case of Sync API
  /// i.e. if the server has any services with sync methods. The server uses
  /// these completion queues to poll for new RPCs
  std::shared_ptr<std::vector<std::unique_ptr<ServerCompletionQueue>>>
      sync_server_cqs_;

  /// List of \a ThreadManager instances (one for each cq in
  /// the \a sync_server_cqs)
  std::vector<std::unique_ptr<SyncRequestThreadManager>> sync_req_mgrs_;

  // Server status
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

#endif  // GRPCPP_SERVER_H
