//
//
// Copyright 2018 gRPC authors.
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

#ifdef GPR_WINDOWS

#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <shellapi.h>
#include <stdio.h>
#include <tchar.h>
#include <windows.h>

#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"
#include "src/core/util/crash.h"

namespace grpc_core {
namespace internal {

bool check_bios_data(const char*) { return false; }

bool check_windows_registry_product_name(HKEY root_key,
                                         const char* reg_key_path,
                                         const char* reg_key_name) {
  const size_t kProductNameBufferSize = 256;
  char const expected_substr[] = "Google";

  // Get the size of the string first to allocate our buffer. This includes
  // enough space for the trailing NUL character that will be included.
  DWORD buffer_size{};
  auto rc = ::RegGetValueA(
      root_key, reg_key_path, reg_key_name, RRF_RT_REG_SZ,
      nullptr,        // We know the type will be REG_SZ.
      nullptr,        // We're only fetching the size; no buffer given yet.
      &buffer_size);  // Fetch the size in bytes of the value, if it exists.
  if (rc != 0) {
    return false;
  }

  if (buffer_size > kProductNameBufferSize) {
    return false;
  }

  // Retrieve the product name string.
  char buffer[kProductNameBufferSize];
  buffer_size = kProductNameBufferSize;
  rc = ::RegGetValueA(
      root_key, reg_key_path, reg_key_name, RRF_RT_REG_SZ,
      nullptr,                     // We know the type will be REG_SZ.
      static_cast<void*>(buffer),  // Fetch the string value this time.
      &buffer_size);  // The string size in bytes, not including trailing NUL.
  if (rc != 0) {
    return false;
  }

  return strstr(buffer, expected_substr) != nullptr;
}

}  // namespace internal
}  // namespace grpc_core

static bool g_compute_engine_detection_done = false;
static bool g_is_on_compute_engine = false;
static gpr_mu g_mu;
static gpr_once g_once = GPR_ONCE_INIT;

static void init_mu(void) { gpr_mu_init(&g_mu); }

bool grpc_alts_is_running_on_gcp() {
  char const reg_key_path[] = "SYSTEM\\HardwareConfig\\Current\\";
  char const reg_key_name[] = "SystemProductName";

  gpr_once_init(&g_once, init_mu);
  gpr_mu_lock(&g_mu);
  if (!g_compute_engine_detection_done) {
    g_is_on_compute_engine =
        grpc_core::internal::check_windows_registry_product_name(
            HKEY_LOCAL_MACHINE, reg_key_path, reg_key_name);
    g_compute_engine_detection_done = true;
  }
  gpr_mu_unlock(&g_mu);
  return g_is_on_compute_engine;
}

#endif  // GPR_WINDOWS
