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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS_ENV

#include <windows.h>

#include <memory>

#include "absl/cleanup/cleanup.h"
#include "src/core/util/env.h"
#include "src/core/util/no_destruct.h"
#include "src/core/util/sync.h"
#include "src/core/util/tchar.h"

namespace grpc_core {

namespace {
NoDestruct<Mutex> g_mu;
bool g_test_only = false;
}  // namespace

void SetTestOnlyEnvSynchronize() { g_test_only = true; }

std::optional<std::string> GetEnv(const char* name)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  auto tname = CharToTchar(name);
  if (g_test_only) {
    g_mu->Lock();
  }
  auto cleanup = absl::MakeCleanup([]() {
    if (g_test_only) {
      g_mu->Unlock();
    }
  });
  DWORD ret = GetEnvironmentVariable(tname.c_str(), NULL, 0);
  if (ret == 0) return std::nullopt;
  std::unique_ptr<TCHAR[]> tresult(new TCHAR[ret]);
  ret =
      GetEnvironmentVariable(tname.c_str(), tresult.get(), ret * sizeof(TCHAR));
  if (ret == 0) return std::nullopt;
  return TcharToChar(tresult.get());
}

void SetEnv(const char* name,
            const char* value) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  if (g_test_only) {
    g_mu->Lock();
  }
  BOOL res = SetEnvironmentVariable(CharToTchar(name).c_str(),
                                    CharToTchar(value).c_str());
  if (g_test_only) {
    g_mu->Unlock();
  }
  if (!res) abort();
}

void UnsetEnv(const char* name) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  if (g_test_only) {
    g_mu->Lock();
  }
  BOOL res = SetEnvironmentVariable(CharToTchar(name).c_str(), NULL);
  if (g_test_only) {
    g_mu->Unlock();
  }
  if (!res) abort();
}

}  // namespace grpc_core

#endif  // GPR_WINDOWS_ENV
