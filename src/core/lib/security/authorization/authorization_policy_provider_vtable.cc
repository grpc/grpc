// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

#include "src/core/lib/security/authorization/authorization_policy_provider.h"

namespace {

void* provider_copy(void* p) {
  grpc_authorization_policy_provider* provider =
      static_cast<grpc_authorization_policy_provider*>(p);
  provider->Ref().release();
  return provider;
}

void provider_destroy(void* p) {
  grpc_authorization_policy_provider* provider =
      static_cast<grpc_authorization_policy_provider*>(p);
  provider->Unref();
}

int provider_cmp(void* p, void* q) { return GPR_ICMP(p, q); }

}  // namespace

// Wrapper APIs declared in grpc.h

const grpc_arg_pointer_vtable* grpc_authorization_policy_provider_arg_vtable() {
  static const grpc_arg_pointer_vtable vtable = {
      provider_copy, provider_destroy, provider_cmp};
  return &vtable;
}
