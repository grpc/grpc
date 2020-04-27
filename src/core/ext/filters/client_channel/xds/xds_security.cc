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

#include "src/core/ext/filters/client_channel/xds/xds_security.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

void TlsContextManager::Init() {}

void XdsExtractContextManager(
    const grpc_channel_args* channel_args,
    TlsContextManager** tls_context_manager,
    grpc_arg_pointer_vtable* tls_context_manager_vtable,
    RefCountedPtr<SslContextProvider> ssl_context_provider) {}

void XdsConfigureSslContextProvider(
    const TlsContextManager* tls_context_manager,
    const XdsApi::CdsUpdate& cluster_data,
    RefCountedPtr<SslContextProvider>* ssl_context_provider) {}

grpc_channel_args* XdsAppendChildPolicyArgs(
    const grpc_channel_args* channel_args,
    RefCountedPtr<SslContextProvider> ssl_context_provider) {
  return nullptr;
}

}  // namespace grpc_core
