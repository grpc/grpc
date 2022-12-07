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

#ifndef GRPC_NO_BINDER

#include "src/core/ext/transport/binder/wire_format/transaction.h"

namespace grpc_binder {

ABSL_CONST_INIT const int kFlagPrefix = 0x1;
ABSL_CONST_INIT const int kFlagMessageData = 0x2;
ABSL_CONST_INIT const int kFlagSuffix = 0x4;
ABSL_CONST_INIT const int kFlagOutOfBandClose = 0x8;
ABSL_CONST_INIT const int kFlagExpectSingleMessage = 0x10;
ABSL_CONST_INIT const int kFlagStatusDescription = 0x20;
ABSL_CONST_INIT const int kFlagMessageDataIsParcelable = 0x40;
ABSL_CONST_INIT const int kFlagMessageDataIsPartial = 0x80;

}  // namespace grpc_binder
#endif
