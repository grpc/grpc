/*
 *
 * Copyright 2015-2016, Google Inc.
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

#ifndef GRPCXX_SERVER_BUILDER_H
#define GRPCXX_SERVER_BUILDER_H

#include <climits>
#include <map>
#include <memory>
#include <vector>

#include <grpc++/impl/channel_argument_option.h>
#include <grpc++/impl/server_builder_option.h>
#include <grpc++/impl/server_builder_plugin.h>
#include <grpc++/support/config.h>
#include <grpc/compression.h>
#include <grpc/support/cpu.h>
#include <grpc/support/useful.h>

struct grpc_resource_quota;

namespace grpc {

class AsyncGenericService;
class ResourceQuota;
class CompletionQueue;
class RpcService;
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
  ~ServerBuilder();

  enum SyncServerOption { NUM_CQS, MIN_POLLERS, MAX_POLLERS, CQ_TIMEOUT_MSEC };

  /// Register a service. This call does not take ownership of the service.
  /// The service must exist for the lifetime of the \a Server instance returned
  /// by \a BuildAndStart().
  /// Matches requests with any :authority
  ServerBuilder& RegisterService(Service* service);

  /// Register a generic service.
  /// Matches requests with any :authority
  ServerBuilder& RegisterAsyncGenericService(AsyncGenericService* service);

  /// Register a service. This call does not take ownership of the service.
  /// The service must exist for the lifetime of the \a Server instance returned
  /// by BuildAndStart().
  /// Only matches requests with :authority \a host
  ServerBuilder& RegisterService(const grpc::string& host, Service* service);

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
  /// GRPC_STATUS_UNIMPLEMENTED.
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

  /// Only useful if this is a Synchronous server.
  ServerBuilder& SetSyncServerOption(SyncServerOption option, int value);

  /// Add a channel argument (an escape hatch to tuning core library parameters
  /// directly)
  template <class T>
  ServerBuilder& AddChannelArgument(const grpc::string& arg, const T& value) {
    return SetOption(MakeChannelArgumentOption(arg, value));
  }

  /// Tries to bind \a server to the given \a addr.
  ///
  /// It can be invoked multiple times.
  ///
  /// \param addr The address to try to bind to the server (eg, localhost:1234,
  /// 192.168.1.1:31416, [::1]:27182, etc.).
  /// \params creds The credentials associated with the server.
  /// \param selected_port[out] Upon success, updated to contain the port
  /// number. \a nullptr otherwise.
  ///
  // TODO(dgq): the "port" part seems to be a misnomer.
  ServerBuilder& AddListeningPort(const grpc::string& addr,
                                  std::shared_ptr<ServerCredentials> creds,
                                  int* selected_port = nullptr);

  /// Add a completion queue for handling asynchronous services.
  ///
  /// Caller is required to shutdown the server prior to shutting down the
  /// returned completion queue. A typical usage scenario:
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
  ///
  /// \param is_frequently_polled This is an optional parameter to inform GRPC
  /// library about whether this completion queue would be frequently polled
  /// (i.e by calling Next() or AsyncNext()). The default value is 'true' and is
  /// the recommended setting. Setting this to 'false' (i.e not polling the
  /// completion queue frequently) will have a significantly negative
  /// performance impact and hence should not be used in production use cases.
  std::unique_ptr<ServerCompletionQueue> AddCompletionQueue(
      bool is_frequently_polled = true);

  /// Return a running server which is ready for processing calls.
  std::unique_ptr<Server> BuildAndStart();

  /// For internal use only: Register a ServerBuilderPlugin factory function.
  static void InternalAddPluginFactory(
      std::unique_ptr<ServerBuilderPlugin> (*CreatePlugin)());

 private:
  friend class ::grpc::testing::ServerBuilderPluginTest;

  struct Port {
    grpc::string addr;
    std::shared_ptr<ServerCredentials> creds;
    int* selected_port;
  };

  struct SyncServerSettings {
    SyncServerSettings()
        : num_cqs(1),
          min_pollers(1),
          max_pollers(INT_MAX),
          cq_timeout_msec(1000) {}

    // Number of server completion queues to create to listen to incoming RPCs.
    int num_cqs;

    // Minimum number of threads per completion queue that should be listening
    // to incoming RPCs.
    int min_pollers;

    // Maximum number of threads per completion queue that can be listening to
    // incoming RPCs.
    int max_pollers;

    // The timeout for server completion queue's AsyncNext call.
    int cq_timeout_msec;
  };

  typedef std::unique_ptr<grpc::string> HostString;
  struct NamedService {
    explicit NamedService(Service* s) : service(s) {}
    NamedService(const grpc::string& h, Service* s)
        : host(new grpc::string(h)), service(s) {}
    HostString host;
    Service* service;
  };

  int max_receive_message_size_;
  int max_send_message_size_;
  std::vector<std::unique_ptr<ServerBuilderOption>> options_;
  std::vector<std::unique_ptr<NamedService>> services_;
  std::vector<Port> ports_;

  SyncServerSettings sync_server_settings_;

  // List of completion queues added via AddCompletionQueue() method
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

#endif  // GRPCXX_SERVER_BUILDER_H
