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

#include "src/core/util/env.h"
#include "src/core/util/tchar.h"

namespace grpc_core {

std::optional<std::string> GetEnv(const char* name) {
  auto tname = CharToTchar(name);
  DWORD ret = GetEnvironmentVariable(tname.c_str(), NULL, 0);
  if (ret == 0) return std::nullopt;
  std::unique_ptr<TCHAR[]> tresult(new TCHAR[ret]);
  ret =
      GetEnvironmentVariable(tname.c_str(), tresult.get(), ret * sizeof(TCHAR));
  if (ret == 0) return std::nullopt;
  return TcharToChar(tresult.get());
}

void SetEnv(const char* name, const char* value) {
  BOOL res = SetEnvironmentVariable(CharToTchar(name).c_str(),
                                    CharToTchar(value).c_str());
  if (!res) abort();
}

void UnsetEnv(const char* name) {
  BOOL res = SetEnvironmentVariable(CharToTchar(name).c_str(), NULL);
  if (!res) abort();
}

}  // namespace grpc_core

#endif  // GPR_WINDOWS_ENV
