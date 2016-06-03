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

#include <map>
#include <memory>
#include <vector>

#include <grpc++/impl/server_builder_option.h>
#include <grpc++/impl/server_builder_plugin.h>
#include <grpc++/support/config.h>
#include <grpc/compression.h>

namespace grpc {

class AsyncGenericService;
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

  /// Set max message size in bytes.
  ServerBuilder& SetMaxMessageSize(int max_message_size) {
    max_message_size_ = max_message_size;
    return *this;
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

  ServerBuilder& SetOption(std::unique_ptr<ServerBuilderOption> option);

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

  /// Add a completion queue for handling asynchronous services
  /// Caller is required to keep this completion queue live until
  /// the server is destroyed.
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

  typedef std::unique_ptr<grpc::string> HostString;
  struct NamedService {
    explicit NamedService(Service* s) : service(s) {}
    NamedService(const grpc::string& h, Service* s)
        : host(new grpc::string(h)), service(s) {}
    HostString host;
    Service* service;
  };

  int max_message_size_;
  std::vector<std::unique_ptr<ServerBuilderOption>> options_;
  std::vector<std::unique_ptr<NamedService>> services_;
  std::vector<Port> ports_;
  std::vector<ServerCompletionQueue*> cqs_;
  std::shared_ptr<ServerCredentials> creds_;
  std::map<grpc::string, std::unique_ptr<ServerBuilderPlugin>> plugins_;
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
