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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RETRY_THROTTLE_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RETRY_THROTTLE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Tracks retry throttling data for an individual server name.
typedef struct grpc_server_retry_throttle_data grpc_server_retry_throttle_data;

/// Records a failure.  Returns true if it's okay to send a retry.
bool grpc_server_retry_throttle_data_record_failure(
    grpc_server_retry_throttle_data* throttle_data);
/// Records a success.
void grpc_server_retry_throttle_data_record_success(
    grpc_server_retry_throttle_data* throttle_data);

grpc_server_retry_throttle_data* grpc_server_retry_throttle_data_ref(
    grpc_server_retry_throttle_data* throttle_data);
void grpc_server_retry_throttle_data_unref(
    grpc_server_retry_throttle_data* throttle_data);

/// Initializes global map of failure data for each server name.
void grpc_retry_throttle_map_init();
/// Shuts down global map of failure data for each server name.
void grpc_retry_throttle_map_shutdown();

/// Returns a reference to the failure data for \a server_name, creating
/// a new entry if needed.
/// Caller must eventually unref via \a grpc_server_retry_throttle_data_unref().
grpc_server_retry_throttle_data* grpc_retry_throttle_map_get_data_for_server(
    const char* server_name, int max_milli_tokens, int milli_token_ratio);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RETRY_THROTTLE_H */
