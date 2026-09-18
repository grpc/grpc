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

#ifndef GRPC_SRC_CORE_CHANNELZ_ZVIZ_ENTITY_H
#define GRPC_SRC_CORE_CHANNELZ_ZVIZ_ENTITY_H

#include "src/core/channelz/zviz/environment.h"
#include "src/core/channelz/zviz/layout.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"

namespace grpc_zviz {

void Format(Environment& env, const grpc::channelz::v2::Entity& entity,
            layout::Element& element);

}  // namespace grpc_zviz

#endif  // GRPC_SRC_CORE_CHANNELZ_ZVIZ_ENTITY_H
