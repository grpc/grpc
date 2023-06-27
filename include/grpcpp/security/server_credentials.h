//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#ifndef GRPCPP_SECURITY_SERVER_CREDENTIALS_H
#define GRPCPP_SECURITY_SERVER_CREDENTIALS_H

#include <memory>
#include <vector>

#include <grpc/grpc_security_constants.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/security/auth_metadata_processor.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/support/config.h>

struct grpc_server;

namespace grpc {

class Server;
class ServerCredentials;
class SecureServerCredentials;
/// Options to create ServerCredentials with SSL
struct SslServerCredentialsOptions {
  /// \warning Deprecated
  SslServerCredentialsOptions()
      : force_client_auth(false),
        client_certificate_request(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE) {}
  explicit SslServerCredentialsOptions(
      grpc_ssl_client_certificate_request_type request_type)
      : force_client_auth(false), client_certificate_request(request_type) {}

  struct PemKeyCertPair {
    std::string private_key;
    std::string cert_chain;
  };
  std::string pem_root_certs;
  std::vector<PemKeyCertPair> pem_key_cert_pairs;
  /// \warning Deprecated
  bool force_client_auth;

  /// If both \a force_client_auth and \a client_certificate_request
  /// fields are set, \a force_client_auth takes effect, i.e.
  /// \a REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
  /// will be enforced.
  grpc_ssl_client_certificate_request_type client_certificate_request;

  // If true, the SSL server sends a list of CA names to the client in the
  // ServerHello. This list of CA names is extracted from the server's trust
  // bundle, and the client may use this lint as a hint to decide which
  // certificate it should send to the server.
  //
  // By default, it is set to true.
  //
  // WARNING: This is an extremely dangerous option and users are encouraged to
  // explicitly set it to false. If the server's trust bundle is sufficiently large, then setting this bit to true will result in
  // the server being unable to generate a ServerHello, and hence the server
  // will be unusable. 
  bool set_client_ca_list;
};

/// Builds Xds ServerCredentials given fallback credentials
std::shared_ptr<ServerCredentials> XdsServerCredentials(
    const std::shared_ptr<ServerCredentials>& fallback_credentials);

/// Wrapper around \a grpc_server_credentials, a way to authenticate a server.
class ServerCredentials : private grpc::internal::GrpcLibrary {
 public:
  /// This method is not thread-safe and has to be called before the server is
  /// started. The last call to this function wins.
  virtual void SetAuthMetadataProcessor(
      const std::shared_ptr<grpc::AuthMetadataProcessor>& processor) = 0;

 private:
  friend class Server;

  // We need this friend declaration for access to Insecure() and
  // AsSecureServerCredentials(). When these two functions are no longer
  // necessary, this friend declaration can be removed too.
  friend std::shared_ptr<ServerCredentials> grpc::XdsServerCredentials(
      const std::shared_ptr<ServerCredentials>& fallback_credentials);

  /// Tries to bind \a server to the given \a addr (eg, localhost:1234,
  /// 192.168.1.1:31416, [::1]:27182, etc.)
  ///
  /// \return bound port number on success, 0 on failure.
  // TODO(dgq): the "port" part seems to be a misnomer.
  virtual int AddPortToServer(const std::string& addr, grpc_server* server) = 0;

  // TODO(yashykt): This is a hack since InsecureServerCredentials() cannot use
  // grpc_insecure_server_credentials_create() and should be removed after
  // insecure builds are removed from gRPC.
  virtual bool IsInsecure() const { return false; }

  // TODO(yashkt): This is a hack that should be removed once we remove insecure
  // builds and the indirect method of adding ports to a server.
  virtual SecureServerCredentials* AsSecureServerCredentials() {
    return nullptr;
  }
};

/// Builds SSL ServerCredentials given SSL specific options
std::shared_ptr<ServerCredentials> SslServerCredentials(
    const grpc::SslServerCredentialsOptions& options);

std::shared_ptr<ServerCredentials> InsecureServerCredentials();

namespace experimental {

/// Options to create ServerCredentials with ALTS
struct AltsServerCredentialsOptions {
  /// Add fields if needed.
};

/// Builds ALTS ServerCredentials given ALTS specific options
std::shared_ptr<ServerCredentials> AltsServerCredentials(
    const AltsServerCredentialsOptions& options);

/// Builds Local ServerCredentials.
std::shared_ptr<ServerCredentials> AltsServerCredentials(
    const AltsServerCredentialsOptions& options);

std::shared_ptr<ServerCredentials> LocalServerCredentials(
    grpc_local_connect_type type);

/// Builds TLS ServerCredentials given TLS options.
std::shared_ptr<ServerCredentials> TlsServerCredentials(
    const experimental::TlsServerCredentialsOptions& options);

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_SERVER_CREDENTIALS_H
