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

#ifndef GRPC_TEST_CORE_COMPRESSION_ARGS_UTILS_H_H
#define GRPC_TEST_CORE_COMPRESSION_ARGS_UTILS_H_H

#include "src/core/lib/channel/channel_args.h"

// TODO(ctiller): when we do the channel args migration, just delete this.
const grpc_channel_args*
grpc_channel_args_set_channel_default_compression_algorithm(
    const grpc_channel_args* a, grpc_compression_algorithm algorithm);

const grpc_channel_args* grpc_channel_args_compression_algorithm_set_state(
    const grpc_channel_args** a, grpc_compression_algorithm algorithm,
    int state);

const grpc_channel_args*
grpc_channel_args_set_channel_default_compression_algorithm(
    const grpc_channel_args* a, grpc_compression_algorithm algorithm);

#endif
