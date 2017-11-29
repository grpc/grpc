/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_SECURITY_TRANSPORT_LB_TARGETS_INFO_H
#define GRPC_CORE_LIB_SECURITY_TRANSPORT_LB_TARGETS_INFO_H

#include "src/core/lib/slice/slice_hash_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Return a channel argument containing \a targets_info. */
grpc_arg grpc_lb_targets_info_create_channel_arg(
    grpc_slice_hash_table* targets_info);

/** Return the instance of targets info in \a args or NULL */
grpc_slice_hash_table* grpc_lb_targets_info_find_in_args(
    const grpc_channel_args* args);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SECURITY_TRANSPORT_LB_TARGETS_INFO_H */
