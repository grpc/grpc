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

#include <memory>
#include <vector>

#include <grpc++/impl/server_builder_option.h>
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

/// A builder class for the creation and startup of \a grpc::Server instances.
class ServerBuilder {
 public:
  ServerBuilder();

  /// Register a service. This call does not take ownership of the service.
  /// The service must exist for the lifetime of the \a Server instance returned
  /// by \a BuildAndStart().
  /// Matches requests with any :authority
  void RegisterService(Service* service);

  /// Register a generic service.
  /// Matches requests with any :authority
  void RegisterAsyncGenericService(AsyncGenericService* service);

  /// Register a service. This call does not take ownership of the service.
  /// The service must exist for the lifetime of the \a Server instance returned
  /// by BuildAndStart().
  /// Only matches requests with :authority \a host
  void RegisterService(const grpc::string& host, Service* service);

  /// Set max message size in bytes.
  void SetMaxMessageSize(int max_message_size) {
    max_message_size_ = max_message_size;
  }

  /// Set the compression options to be used by the server.
  void SetCompressionOptions(const grpc_compression_options& options) {
    compression_options_ = options;
  }

  void SetOption(std::unique_ptr<ServerBuilderOption> option);

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
  void AddListeningPort(const grpc::string& addr,
                        std::shared_ptr<ServerCredentials> creds,
                        int* selected_port = nullptr);

  /// Add a completion queue for handling asynchronous services
  /// Caller is required to keep this completion queue live until
  /// the server is destroyed.
  std::unique_ptr<ServerCompletionQueue> AddCompletionQueue();

  /// Return a running server which is ready for processing calls.
  std::unique_ptr<Server> BuildAndStart();

 private:
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
  grpc_compression_options compression_options_;
  std::vector<std::unique_ptr<ServerBuilderOption>> options_;
  std::vector<std::unique_ptr<NamedService>> services_;
  std::vector<Port> ports_;
  std::vector<ServerCompletionQueue*> cqs_;
  std::shared_ptr<ServerCredentials> creds_;
  AsyncGenericService* generic_service_;
};

}  // namespace grpc

#endif  // GRPCXX_SERVER_BUILDER_H
