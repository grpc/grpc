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

#ifndef GRPC_SRC_CORE_EXT_FILTER_CLIENT_CHANNEL_XDS_SDS_H
#define GRPC_SRC_CORE_EXT_FILTER_CLIENT_CHANNEL_XDS_SDS_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <unordered_map>

#include "src/core/ext/filters/client_channel/xds/xds_api.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

class SslContextProviderImpl;
class SslContextProvider;

/// A global cache that holds the SslContextManager instances.
class TlsContextManager {
 public:
  /// The helper allows an SslContextProvider to remove itself from the
  /// TlsContextManager when all its references are gone.
  class Helper {
   public:
    /// Helper is constructed with TlsContextManager's lock being acquired.
    /// Don't call parent's methods in the constructor.
    explicit Helper(TlsContextManager* parent);

    /// Remove an SslContextManager from the cache.
    void RemoveProvider(const XdsApi::SecurityServiceConfig& tls_context);

   private:
    TlsContextManager* parent_;
  };

  /// Global initialization. Create the singleton instance.
  static void Init();

  /// Find an SslContextProvider object corresponding to a specific
  /// configuration. Return the object if it is found. Otherwise, create a new
  /// SslContextProvider object corresponding to the configuration, add it in
  /// the cache, then return the new object.
  RefCountedPtr<SslContextProvider> CreateOrGetProvider(
      const XdsApi::SecurityServiceConfig& tls_context);

 private:
  // Only allows singleton construction.
  TlsContextManager();

  /// The global singleton object.
  static TlsContextManager* singleton_;

  /// The cache to hold the SslContextProvider instances.
  std::unordered_map<XdsApi::SecurityServiceConfig, SslContextProvider*,
                     XdsApi::SecurityServiceConfigHasher>
      map_;

  /// Mutex to protect the cache (mu_).
  Mutex mu_;
};

/// This is a wrapper class for SslContextProviderImpl. It allows multiple
/// owners to the underlying SslContextProviderImpl object.
class SslContextProvider : public RefCounted<SslContextProvider> {
 public:
  SslContextProvider(const XdsApi::SecurityServiceConfig& tls_context,
                     std::unique_ptr<TlsContextManager::Helper> helper);
  ~SslContextProvider();

  SslContextProviderImpl* impl() const { return impl_.get(); }

 private:
  OrphanablePtr<SslContextProviderImpl> impl_;
  std::unique_ptr<TlsContextManager::Helper> helper_;
};

/// The context that holds the current TLS configurations, including the
/// credentials and the peer validation config. Users of the class can obtain
/// the latest configurations in real time.
class SslContextProviderImpl
    : public InternallyRefCounted<SslContextProviderImpl> {
 public:
  /// Configuration for TLS credentials.
  struct SslContext {
    std::string pem_root_certs;
    std::string pem_private_key;
    std::string pem_cert_chain;
  };

  void Orphan() override;

  /// Configuration for peer validation.
  struct AclContext {
    std::vector<std::string> valid_subject_names;
  };

  /// Starts a watcher on the client to receive the updates of the
  /// configurations.
  explicit SslContextProviderImpl(
      const XdsApi::SecurityServiceConfig& tls_context);

  /// Get the tls_context object associated with this provider.
  const XdsApi::SecurityServiceConfig& tls_context() const;

  /// Fetch the latest TLS credentials. The operation can be either sync or
  /// async. If the latest TLS credentials is available at the time of request,
  /// the latest credentials are copied to parameter context and the method
  /// returns true. Otherwise, the method returns false; the credentials are
  /// copied to context later when it is available, then the callback cb is
  /// called.
  bool GetSslContext(grpc_closure* cb, SslContext* context);

  /// Fetch the latest peer validation config. The operation can be either sync
  /// or async. If the latest peer validation config is available at the time of
  /// request, the latest config is copied to parameter context and the method
  /// returns true. Otherwise, the method returns false; the config is copied to
  /// context later when it is available, then the callback cb is called.
  bool GetAclContext(grpc_closure* cb, AclContext* context);

  /// Return a grpc_arg object that holds a pointer to the
  /// SslContextProviderImpl instance.
  grpc_arg ChannelArg();

 private:
  /// Helper method to merge the default peer validation config (acquired from
  /// CDS response) with any new config acquired from the credential server.
  void MergeAclContexts();

  // Vtable for channel arg.
  static void* Copy(void* p);
  static void Destroy(void* p);
  static int Compare(void* p, void* q);

  XdsApi::SecurityServiceConfig tls_context_;
  // Protects latest_ssl_context_ and latest_acl_context_.
  Mutex mu_;
  /// Default peer validation config.
  AclContext default_acl_context_;
  /// Lastest peer validation config, which is already merged with the default
  /// config.
  AclContext latest_acl_context_;
  /// Latest credentials received from credential server.
  SslContext latest_ssl_context_;
};

/// Extract the TlsContextManager instance from channel args.
void XdsExtractContextManager(
    const grpc_channel_args* channel_args,
    TlsContextManager** tls_context_manager,
    grpc_arg_pointer_vtable* tls_context_manager_vtable,
    RefCountedPtr<SslContextProvider>* ssl_context_provider);

/// Update SslContextProvider reference based on whether tls_context_manager is
/// non-null and the CDS update contains TLS based configurations.
/// If tls_context_manager is not null and cluster_data contains credential
/// configurations, get a SslContextProvider corresponding to the credential
/// configurations from tls_context_manager.
void XdsConfigureSslContextProvider(
    TlsContextManager* tls_context_manager,
    const XdsApi::CdsUpdate& cluster_data,
    RefCountedPtr<SslContextProvider>* ssl_context_provider);

/// If ssl_context_provider is not null, add it's pointer as a channel arg to
/// channel_args and return the result as a new grpc_channel_args object. If
/// ssl_context_provider is null, return nullptr.
grpc_channel_args* XdsAppendChildPolicyArgs(
    const grpc_channel_args* channel_args,
    RefCountedPtr<SslContextProvider> ssl_context_provider);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTER_CLIENT_CHANNEL_XDS_SDS_H
