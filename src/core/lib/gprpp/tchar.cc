// Copyright 2022 gRPC authors.
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

#include "src/core/lib/gprpp/tchar.h"

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

namespace grpc_core {

#if defined UNICODE || defined _UNICODE
TcharString CharToTchar(std::string input) {
  int needed = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, NULL, 0);
  if (needed <= 0) return TcharString();
  TcharString ret(needed, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1,
                      const_cast<LPTSTR>(ret.data()), needed);
  return ret;
}

std::string TcharToChar(TcharString input) {
  int needed =
      WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, NULL, 0, NULL, NULL);
  if (needed <= 0) return std::string();
  std::string ret(needed, '\0');
  WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1,
                      const_cast<LPSTR>(ret.data()), needed, NULL, NULL);
  return ret;
}
#else
TcharString CharToTchar(std::string input) { return input; }
std::string TcharToChar(TcharString input) { return input; }
#endif

}  // namespace grpc_core

#endif
