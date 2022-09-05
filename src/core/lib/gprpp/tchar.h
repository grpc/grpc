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

#ifndef GRPC_CORE_LIB_GPRPP_TCHAR_H
#define GRPC_CORE_LIB_GPRPP_TCHAR_H

#include <grpc/support/port_platform.h>

namespace grpc_core {

#ifdef GPR_WINDOWS
using TcharString = std::basic_string<TCHAR>;

TcharString CharToTchar(std::string input);
std::string TcharToChar(TcharString input);
#endif

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_TCHAR_H
