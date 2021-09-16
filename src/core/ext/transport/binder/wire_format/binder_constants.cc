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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/binder/wire_format/binder_constants.h"

#ifndef GPR_SUPPORT_BINDER_TRANSPORT

const int FIRST_CALL_TRANSACTION = 0x00000001;
const int LAST_CALL_TRANSACTION = 0x00FFFFFF;

#endif  // GPR_SUPPORT_BINDER_TRANSPORT

namespace grpc_binder {

const int kFirstCallId = FIRST_CALL_TRANSACTION + 1000;

}  // namespace grpc_binder
