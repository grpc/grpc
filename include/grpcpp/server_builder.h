/*
 *
 * Copyright 2015-2016 gRPC authors.
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

#ifndef GRPCPP_SERVER_BUILDER_H
#define GRPCPP_SERVER_BUILDER_H

#include <climits>
#include <map>
#include <memory>
#include <vector>

#include <grpc/compression.h>
#include <grpc/support/cpu.h>
#include <grpc/support/workaround_list.h>
#include <grpcpp/impl/channel_argument_option.h>
#include <grpcpp/impl/server_builder_option.h>
#include <grpcpp/impl/server_builder_plugin.h>
#include <grpcpp/support/config.h>

struct grpc_resource_quota;

namespace grpc {

class AsyncGenericService;
class ResourceQuota;
class CompletionQueue;
class Server;
class ServerCompletionQueue;
class ServerCredentials;
class Service;

namespace testing {
class ServerBuilderPluginTest;
}  // namespace testing

/// A builder class for the creation and startup of \a grpc::Server instances.
class ServerBuilder {
 public:
  ServerBuilder();
  virtual ~ServerBuilder();

  //////////////////////////////////////////////////////////////////////////////
  // Primary API's

  /// Return a running server which is ready for processing calls.
  /// Before calling, one typically needs to ensure that:
  ///  1. a service is registered - so that the server knows what to serve
  ///     (via RegisterService, or RegisterAsyncGenericService)
  ///  2. a listening port has been added - so the server knows where to receive
  ///     traffic (via AddListeningPort)
  ///  3. [for async api only] completion queues have been added via
  ///     AddCompletionQueue
  virtual std::unique_ptr<Server> BuildAndStart();

  /// Register a service. This call does not take ownership of the service.
  /// The service must exist for the lifetime of the \a Server instance returned
  /// by \a BuildAndStart().
  /// Matches requests with any :authority
  ServerBuilder& RegisterService(Service* service);

  /// Enlists an endpoint \a addr (port with an optional IP address) to
  /// bind the \a grpc::Server object to be created to.
  ///
  /// It can be invoked multiple times.
  ///
  /// \param addr_uri The address to try to bind to the server in URI form. If
  /// the scheme name is omitted, "dns:///" is assumed. To bind to any address,
  /// please use IPv6 any, i.e., [::]:<port>, which also accepts IPv4
  /// connections.  Valid values include dns:///localhost:1234, /
  /// 192.168.1.1:31416, dns:///[::1]:27182, etc.).
  /// \param creds The credentials associated with the server.
  /// \param selected_port[out] If not `nullptr`, gets populated with the port
  /// number bound to the \a grpc::Server for the corresponding endpoint after
  /// it is successfully bound, 0 otherwise.
  ///
  ServerBuilder& AddListeningPort(const grpc::string& addr_uri,
                                  std::shared_ptr<ServerCredentials> creds,
                                  int* selected_port = nullptr);

  /// Add a completion queue for handling asynchronous services.
  ///
  /// Best performance is typically obtained by using one thread per polling
  /// completion queue.
  ///
  /// Caller is required to shutdown the server prior to shutting down the
  /// returned completion queue. Caller is also required to drain the
  /// completion queue after shutting it down. A typical usage scenario:
  ///
  /// // While building the server:
  /// ServerBuilder builder;
  /// ...
  /// cq_ = builder.AddCompletionQueue();
  /// server_ = builder.BuildAndStart();
  ///
  /// // While shutting down the server;
  /// server_->Shutdown();
  /// cq_->Shutdown();  // Always *after* the associated server's Shutdown()!
  /// // Drain the cq_ that was created
  /// void* ignored_tag;
  /// bool ignored_ok;
  /// while (cq_->Next(&ignored_tag, &ignored_ok)) { }
  ///
  /// \param is_frequently_polled This is an optional parameter to inform gRPC
  /// library about whether this completion queue would be frequently polled
  /// (i.e. by calling \a Next() or \a AsyncNext()). The default value is
  /// 'true' and is the recommended setting. Setting this to 'false' (i.e.
  /// not polling the completion queue frequently) will have a significantly
  /// negative performance impact and hence should not be used in production
  /// use cases.
  std::unique_ptr<ServerCompletionQueue> AddCompletionQueue(
      bool is_frequently_polled = true);

  //////////////////////////////////////////////////////////////////////////////
  // Less commonly used RegisterService variants

  /// Register a service. This call does not take ownership of the service.
  /// The service must exist for the lifetime of the \a Server instance returned
  /// by \a BuildAndStart().
  /// Only matches requests with :authority \a host
  ServerBuilder& RegisterService(const grpc::string& host, Service* service);

  /// Register a generic service.
  /// Matches requests with any :authority
  /// This is mostly useful for writing generic gRPC Proxies where the exact
  /// serialization format is unknown
  ServerBuilder& RegisterAsyncGenericService(AsyncGenericService* service);

  //////////////////////////////////////////////////////////////////////////////
  // Fine control knobs

  /// Set max receive message size in bytes.
  ServerBuilder& SetMaxReceiveMessageSize(int max_receive_message_size) {
    max_receive_message_size_ = max_receive_message_size;
    return *this;
  }

  /// Set max send message size in bytes.
  ServerBuilder& SetMaxSendMessageSize(int max_send_message_size) {
    max_send_message_size_ = max_send_message_size;
    return *this;
  }

  /// \deprecated For backward compatibility.
  ServerBuilder& SetMaxMessageSize(int max_message_size) {
    return SetMaxReceiveMessageSize(max_message_size);
  }

  /// Set the support status for compression algorithms. All algorithms are
  /// enabled by default.
  ///
  /// Incoming calls compressed with an unsupported algorithm will fail with
  /// \a GRPC_STATUS_UNIMPLEMENTED.
  ServerBuilder& SetCompressionAlgorithmSupportStatus(
      grpc_compression_algorithm algorithm, bool enabled);

  /// The default compression level to use for all channel calls in the
  /// absence of a call-specific level.
  ServerBuilder& SetDefaultCompressionLevel(grpc_compression_level level);

  /// The default compression algorithm to use for all channel calls in the
  /// absence of a call-specific level. Note that it overrides any compression
  /// level set by \a SetDefaultCompressionLevel.
  ServerBuilder& SetDefaultCompressionAlgorithm(
      grpc_compression_algorithm algorithm);

  /// Set the attached buffer pool for this server
  ServerBuilder& SetResourceQuota(const ResourceQuota& resource_quota);

  ServerBuilder& SetOption(std::unique_ptr<ServerBuilderOption> option);

  /// Options for synchronous servers.
  enum SyncServerOption {
    NUM_CQS,         ///< Number of completion queues.
    MIN_POLLERS,     ///< Minimum number of polling threads.
    MAX_POLLERS,     ///< Maximum number of polling threads.
    CQ_TIMEOUT_MSEC  ///< Completion queue timeout in milliseconds.
  };

  /// Only useful if this is a Synchronous server.
  ServerBuilder& SetSyncServerOption(SyncServerOption option, int value);

  /// Add a channel argument (an escape hatch to tuning core library parameters
  /// directly)
  template <class T>
  ServerBuilder& AddChannelArgument(const grpc::string& arg, const T& value) {
    return SetOption(MakeChannelArgumentOption(arg, value));
  }

  /// For internal use only: Register a ServerBuilderPlugin factory function.
  static void InternalAddPluginFactory(
      std::unique_ptr<ServerBuilderPlugin> (*CreatePlugin)());

  /// Enable a server workaround. Do not use unless you know what the workaround
  /// does. For explanation and detailed descriptions of workarounds, see
  /// doc/workarounds.md.
  ServerBuilder& EnableWorkaround(grpc_workaround_list id);

 protected:
  /// Experimental, to be deprecated
  struct Port {
    grpc::string addr;
    std::shared_ptr<ServerCredentials> creds;
    int* selected_port;
  };

  /// Experimental, to be deprecated
  typedef std::unique_ptr<grpc::string> HostString;
  struct NamedService {
    explicit NamedService(Service* s) : service(s) {}
    NamedService(const grpc::string& h, Service* s)
        : host(new grpc::string(h)), service(s) {}
    HostString host;
    Service* service;
  };

  /// Experimental, to be deprecated
  std::vector<Port> ports() { return ports_; }

  /// Experimental, to be deprecated
  std::vector<NamedService*> services() {
    std::vector<NamedService*> service_refs;
    for (auto& ptr : services_) {
      service_refs.push_back(ptr.get());
    }
    return service_refs;
  }

  /// Experimental, to be deprecated
  std::vector<ServerBuilderOption*> options() {
    std::vector<ServerBuilderOption*> option_refs;
    for (auto& ptr : options_) {
      option_refs.push_back(ptr.get());
    }
    return option_refs;
  }

 private:
  friend class ::grpc::testing::ServerBuilderPluginTest;

  struct SyncServerSettings {
    SyncServerSettings()
        : num_cqs(1), min_pollers(1), max_pollers(2), cq_timeout_msec(10000) {}

    /// Number of server completion queues to create to listen to incoming RPCs.
    int num_cqs;

    /// Minimum number of threads per completion queue that should be listening
    /// to incoming RPCs.
    int min_pollers;

    /// Maximum number of threads per completion queue that can be listening to
    /// incoming RPCs.
    int max_pollers;

    /// The timeout for server completion queue's AsyncNext call.
    int cq_timeout_msec;
  };

  int max_receive_message_size_;
  int max_send_message_size_;
  std::vector<std::unique_ptr<ServerBuilderOption>> options_;
  std::vector<std::unique_ptr<NamedService>> services_;
  std::vector<Port> ports_;

  SyncServerSettings sync_server_settings_;

  /// List of completion queues added via \a AddCompletionQueue method.
  std::vector<ServerCompletionQueue*> cqs_;

  std::shared_ptr<ServerCredentials> creds_;
  std::vector<std::unique_ptr<ServerBuilderPlugin>> plugins_;
  grpc_resource_quota* resource_quota_;
  AsyncGenericService* generic_service_;
  struct {
    bool is_set;
    grpc_compression_level level;
  } maybe_default_compression_level_;
  struct {
    bool is_set;
    grpc_compression_algorithm algorithm;
  } maybe_default_compression_algorithm_;
  uint32_t enabled_compression_algorithms_bitset_;
};

}  // namespace grpc

#endif  // GRPCPP_SERVER_BUILDER_H
