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

#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/ext/filters/client_channel/xds/sds.h"
#include "src/core/ext/filters/client_channel/xds/xds_channel_args.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

void TlsContextManager::Init() {}

RefCountedPtr<SslContextProvider> TlsContextManager::CreateOrGetProvider(
    const XdsApi::SecurityServiceConfig& config) {
  return nullptr;
}

void SslContextProviderImpl::Orphan() {}

grpc_arg SslContextProviderImpl::ChannelArg() {
  static const grpc_arg_pointer_vtable vtable = {Copy, Destroy, Compare};

  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = const_cast<char*>(GRPC_ARG_XDS_SSL_CONTEXT_PROVIDER);
  arg.value.pointer.p = static_cast<void*>(this);
  arg.value.pointer.vtable = &vtable;

  return arg;
}

void* SslContextProviderImpl::Copy(void* p) { return p; }

void SslContextProviderImpl::Destroy(void* p) {}

int SslContextProviderImpl::Compare(void* p, void* q) { return p == q; }

void XdsExtractContextManager(
    const grpc_channel_args* channel_args,
    TlsContextManager** tls_context_manager,
    grpc_arg_pointer_vtable* tls_context_manager_vtable,
    RefCountedPtr<SslContextProvider>* ssl_context_provider) {
  const grpc_arg* arg =
      grpc_channel_args_find(channel_args, GRPC_ARG_XDS_TLS_CONTEXT_MANAGER);
  if (arg != nullptr && arg->type == GRPC_ARG_POINTER) {
    if (GPR_UNLIKELY(arg->value.pointer.p != *tls_context_manager)) {
      tls_context_manager_vtable->destroy(*tls_context_manager);
      arg->value.pointer.vtable->copy(arg->value.pointer.p);
      *tls_context_manager =
          static_cast<TlsContextManager*>(arg->value.pointer.p);
      *tls_context_manager_vtable = *arg->value.pointer.vtable;
      ssl_context_provider->reset();
    }
  } else {
    if (*tls_context_manager != nullptr) {
      tls_context_manager_vtable->destroy(*tls_context_manager);
      *tls_context_manager = nullptr;
      ssl_context_provider->reset();
    }
  }
}

void XdsConfigureSslContextProvider(
    TlsContextManager* tls_context_manager,
    const XdsApi::CdsUpdate& cluster_data,
    RefCountedPtr<SslContextProvider>* ssl_context_provider) {
  if (tls_context_manager != nullptr &&
      cluster_data.security_service_config.has_value()) {
    // Use a credential server for credential reloading and peer validation.
    *ssl_context_provider = tls_context_manager->CreateOrGetProvider(
        cluster_data.security_service_config.value());
  }
}

grpc_channel_args* XdsAppendChildPolicyArgs(
    const grpc_channel_args* channel_args,
    RefCountedPtr<SslContextProvider> ssl_context_provider) {
  if (ssl_context_provider == nullptr) {
    return nullptr;
  } else {
    grpc_arg arg = ssl_context_provider->impl()->ChannelArg();
    return grpc_channel_args_copy_and_add(channel_args, &arg, 1);
  }
}

}  // namespace grpc_core
