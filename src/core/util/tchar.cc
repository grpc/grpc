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

#include "src/core/util/tchar.h"

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

namespace grpc_core {

#if defined UNICODE || defined _UNICODE
TcharString CharToTchar(std::string input) {
  if (input.size() > INT_MAX) return TcharString();
  int len = static_cast<int>(input.size());
  int needed = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), len, nullptr, 0);
  if (needed <= 0) return TcharString();
  TcharString ret(needed, L'\0');
  int result =
      MultiByteToWideChar(CP_UTF8, 0, input.c_str(), len, ret.data(), needed);
  if (result <= 0) return TcharString();
  return ret;
}

std::string TcharToChar(TcharString input) {
  if (input.size() > INT_MAX) return std::string();
  int len = static_cast<int>(input.size());
  int needed = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), len, nullptr, 0,
                                   nullptr, nullptr);
  if (needed <= 0) return std::string();
  std::string ret(needed, '\0');
  int result = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), len, ret.data(),
                                   needed, nullptr, nullptr);
  if (result <= 0) return std::string();
  return ret;
}
#else
TcharString CharToTchar(std::string input) { return input; }
std::string TcharToChar(TcharString input) { return input; }
#endif

}  // namespace grpc_core

#endif
