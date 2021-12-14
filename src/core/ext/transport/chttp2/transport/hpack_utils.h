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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_UTILS_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_UTILS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

// Return the size occupied by some metadata in the HPACK table.
size_t MetadataSizeInHPackTable(grpc_mdelem elem,
                                bool use_true_binary_metadata);

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_UTILS_H
