#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"

#if !defined(GPR_LINUX) && !defined(GPR_WINDOWS)

#include <grpc/support/log.h>

namespace grpc_core {
namespace internal {

bool check_bios_data(const char* bios_data_file) { return false; }

}  // namespace internal
}  // namespace grpc_core

bool is_running_on_gcp() {
  gpr_log(GPR_ERROR,
          "Platforms other than Linux and Windows are not supported");
  return grpc_core::internal::check_bios_data(nullptr);
}

#endif  // !defined(LINUX) && !defined(GPR_WINDOWS)
