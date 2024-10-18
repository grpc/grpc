#include "src/core/lib/security/context/connection_auth_context.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <new>

#include "src/core/util/alloc.h"
#include "absl/log/log.h"

namespace grpc_core {

namespace {

void* AuthPropertiesMapStorage() {
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
  void* p = AuthPropertiesMapStorage();
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
