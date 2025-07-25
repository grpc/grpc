// Copyright 2025 The gRPC Authors
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

#ifndef GRPC_SRC_CPP_LATENT_SEE_LATENT_SEE_CLIENT_H
#define GRPC_SRC_CPP_LATENT_SEE_LATENT_SEE_CLIENT_H

#include "src/core/util/latent_see.h"
#include "src/proto/grpc/channelz/v2/latent_see.grpc.pb.h"

namespace grpc {

Status FetchLatentSee(channelz::v2::LatentSee::Stub* stub, double sample_time,
                      grpc_core::latent_see::Output* output);
}

#endif  // GRPC_SRC_CPP_LATENT_SEE_LATENT_SEE_CLIENT_H
