/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INCOMING_METADATA_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INCOMING_METADATA_H

#include "src/core/lib/transport/transport.h"

typedef struct {
  gpr_arena *arena;
  grpc_metadata_batch batch;
  size_t size;  // total size of metadata
} grpc_chttp2_incoming_metadata_buffer;

/** assumes everything initially zeroed */
void grpc_chttp2_incoming_metadata_buffer_init(
    grpc_chttp2_incoming_metadata_buffer *buffer, gpr_arena *arena);
void grpc_chttp2_incoming_metadata_buffer_destroy(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_metadata_buffer *buffer);
void grpc_chttp2_incoming_metadata_buffer_publish(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_metadata_buffer *buffer,
    grpc_metadata_batch *batch);

grpc_error *grpc_chttp2_incoming_metadata_buffer_add(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_metadata_buffer *buffer,
    grpc_mdelem elem) GRPC_MUST_USE_RESULT;
grpc_error *grpc_chttp2_incoming_metadata_buffer_replace_or_add(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_metadata_buffer *buffer,
    grpc_mdelem elem) GRPC_MUST_USE_RESULT;
void grpc_chttp2_incoming_metadata_buffer_set_deadline(
    grpc_chttp2_incoming_metadata_buffer *buffer, gpr_timespec deadline);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INCOMING_METADATA_H */
