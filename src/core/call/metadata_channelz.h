// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CALL_METADATA_CHANNELZ_H
#define GRPC_SRC_CORE_CALL_METADATA_CHANNELZ_H

#include "src/core/call/metadata.h"
#include "src/core/channelz/property_list.h"

namespace grpc_core {

channelz::PropertyList MetadataToChannelzProperties(
    const Arena::PoolPtr<grpc_metadata_batch>& md);
channelz::PropertyList MetadataToChannelzProperties(
    const grpc_metadata_batch& md);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CALL_METADATA_CHANNELZ_H
