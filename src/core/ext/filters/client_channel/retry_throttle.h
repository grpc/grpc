/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RETRY_THROTTLE_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RETRY_THROTTLE_H

#include <stdbool.h>

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

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RETRY_THROTTLE_H */
