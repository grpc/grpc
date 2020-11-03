//
// Copyright 2020 gRPC authors.
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

#ifndef GRPCPP_SECURITY_TLS_CERTIFICATE_PROVIDER_H
#define GRPCPP_SECURITY_TLS_CERTIFICATE_PROVIDER_H

#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/grpc_library.h>
#include <grpcpp/support/config.h>

#include <memory>
#include <vector>

// TODO(yihuazhang): remove the forward declaration here and include
// <grpc/grpc_security.h> directly once the insecure builds are cleaned up.
typedef struct grpc_tls_certificate_provider grpc_tls_certificate_provider;

namespace grpc {
namespace experimental {

// Interface for a class that handles the process to fetch credential data.
// Implementations should be a wrapper class of an internal provider
// implementation.
class CertificateProviderInterface {
 public:
  virtual ~CertificateProviderInterface() = default;
  virtual grpc_tls_certificate_provider* c_provider() = 0;
};

// A struct that stores the credential data presented to the peer in handshake
// to show local identity. The private_key and certificate_chain should always
// match.
struct IdentityKeyCertPair {
  std::string private_key;
  std::string certificate_chain;
};

// A basic CertificateProviderInterface implementation that will load credential
// data from static string during initialization. This provider will always
// return the same cert data for all cert names, and reloading is not supported.
class StaticDataCertificateProvider : public CertificateProviderInterface {
 public:
  StaticDataCertificateProvider(
      const std::string& root_certificate,
      const std::vector<IdentityKeyCertPair>& identity_key_cert_pairs);

  StaticDataCertificateProvider(const std::string& root_certificate)
      : StaticDataCertificateProvider(root_certificate, {}) {}

  StaticDataCertificateProvider(
      const std::vector<IdentityKeyCertPair>& identity_key_cert_pairs)
      : StaticDataCertificateProvider("", identity_key_cert_pairs) {}

  ~StaticDataCertificateProvider();

  grpc_tls_certificate_provider* c_provider() override { return c_provider_; }

 private:
  grpc_tls_certificate_provider* c_provider_ = nullptr;
};

// A CertificateProviderInterface implementation that will watch the credential
// changes on the file system. This provider will always return the up-to-date
// cert data for all the cert names callers set through |TlsCredentialsOptions|.
// Several things to note:
// 1. This API only supports one key-cert file and hence one set of identity
// key-cert pair, so SNI(Server Name Indication) is not supported.
// 2. The private key and identity certificates should always match. This API
// guarantees atomic read, and it is the callers' responsibility to do atomic
// updates. There are many ways to atomically update the key and certs in the
// file system. To name a few:
//   1)  creating a new directory, renaming the old directory to a new name, and
//   then renaming the new directory to the original name of the old directory.
//   2)  using a symlink for the directory. When need to change, put new
//   credential data in a new directory, and change symlink.

// identity_key_cert_directory is the directory used to store the private key
// and identity certificate chain.
// private_key_file_name is the file name of the private key in
// |identity_key_cert_directory|. identity_certificate_file_name is the file
// name of the identity certificate chain in |identity_key_cert_directory|.
// root_cert_full_path is the full path to the root certificate bundle.
// refresh_interval_sec is the refreshing interval that we will check the files
// for updates.
class FileWatcherCertificateProvider final
    : public CertificateProviderInterface {
 public:
  FileWatcherCertificateProvider(
      const std::string& identity_key_cert_directory,
      const std::string& private_key_file_name,
      const std::string& identity_certificate_file_name,
      const std::string& root_cert_full_path,
      unsigned int refresh_interval_sec);

  FileWatcherCertificateProvider(
      const std::string& identity_key_cert_directory,
      const std::string& private_key_file_name,
      const std::string& identity_certificate_file_name,
      unsigned int refresh_interval_sec)
      : FileWatcherCertificateProvider(
            identity_key_cert_directory, private_key_file_name,
            identity_certificate_file_name, "", refresh_interval_sec) {}

  FileWatcherCertificateProvider(const std::string& root_cert_full_path,
                                 unsigned int refresh_interval_sec)
      : FileWatcherCertificateProvider("", "", "", root_cert_full_path,
                                       refresh_interval_sec) {}

  ~FileWatcherCertificateProvider();

  grpc_tls_certificate_provider* c_provider() override { return c_provider_; }

 private:
  grpc_tls_certificate_provider* c_provider_ = nullptr;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_TLS_CERTIFICATE_PROVIDER_H
