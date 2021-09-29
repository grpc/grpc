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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/incoming_metadata.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/internal.h"

grpc_error_handle grpc_chttp2_incoming_metadata_buffer_add(
    grpc_chttp2_incoming_metadata_buffer* buffer, grpc_mdelem elem) {
  buffer->size += GRPC_MDELEM_LENGTH(elem);
  grpc_linked_mdelem* storage;
  if (buffer->count < buffer->kPreallocatedMDElem) {
    storage = &buffer->preallocated_mdelems[buffer->count];
    buffer->count++;
  } else {
    storage = static_cast<grpc_linked_mdelem*>(
        buffer->arena->Alloc(sizeof(grpc_linked_mdelem)));
  }
  storage->md = elem;
  return buffer->batch.LinkTail(storage);
}

grpc_error_handle grpc_chttp2_incoming_metadata_buffer_replace_or_add(
    grpc_chttp2_incoming_metadata_buffer* buffer, grpc_slice key,
    grpc_slice value) {
  if (buffer->batch.ReplaceIfExists(key, value)) return GRPC_ERROR_NONE;
  return grpc_chttp2_incoming_metadata_buffer_add(
      buffer, grpc_mdelem_from_slices(key, value));
}

void grpc_chttp2_incoming_metadata_buffer_set_deadline(
    grpc_chttp2_incoming_metadata_buffer* buffer, grpc_millis deadline) {
  if (deadline != GRPC_MILLIS_INF_FUTURE) {
    buffer->batch.Set(grpc_core::GrpcTimeoutMetadata(), deadline);
  } else {
    buffer->batch.Remove(grpc_core::GrpcTimeoutMetadata());
  }
}

void grpc_chttp2_incoming_metadata_buffer_publish(
    grpc_chttp2_incoming_metadata_buffer* buffer, grpc_metadata_batch* batch) {
  *batch = std::move(buffer->batch);
}
