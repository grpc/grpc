//
//
// Copyright 2024 gRPC authors.
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

#include <grpc/support/port_platform.h>
#include <grpc/support/alloc.h>

#include <cstddef>

#include "src/core/lib/security/context/connection_auth_context.h"


#include "src/core/util/alloc.h"
#include "src/core/util/orphanable.h"

namespace grpc_core {

namespace {

void* ConnectionAuthContextStorage() {
  size_t base_size = sizeof(ConnectionAuthContext) +
                     GPR_ROUND_UP_TO_ALIGNMENT_SIZE(
                         auth_context_detail::BaseAuthPropertiesTraits::Size());
  static constexpr size_t alignment =
      (GPR_CACHELINE_SIZE > GPR_MAX_ALIGNMENT &&
       GPR_CACHELINE_SIZE % GPR_MAX_ALIGNMENT == 0)
          ? GPR_CACHELINE_SIZE
          : GPR_MAX_ALIGNMENT;
  return gpr_malloc_aligned(base_size, alignment);
}

}  // namespace

OrphanablePtr<ConnectionAuthContext> ConnectionAuthContext::Create() {
  void* p = ConnectionAuthContextStorage();
  return OrphanablePtr<ConnectionAuthContext>(new (p) ConnectionAuthContext());
}

ConnectionAuthContext::ConnectionAuthContext() {
  for (size_t i = 0;
       i < auth_context_detail::BaseAuthPropertiesTraits::NumAuthProperties();
       ++i) {
    registered_properties()[i] = nullptr;
  }
}

void ConnectionAuthContext::Orphan() {
  this->~ConnectionAuthContext();
  gpr_free_aligned(const_cast<ConnectionAuthContext*>(this));
}

ConnectionAuthContext::~ConnectionAuthContext() {
  for (size_t i = 0;
       i < auth_context_detail::BaseAuthPropertiesTraits::NumAuthProperties();
       ++i) {
    auth_context_detail::BaseAuthPropertiesTraits::Destroy(
        i, registered_properties()[i]);
  }
}

}  // namespace grpc_core
