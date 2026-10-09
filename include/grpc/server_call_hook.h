//
// Copyright 2026 The gRPC Authors.
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
//

// A process-wide server call hook for out-of-tree extensions, consulted where
// server initial metadata is parsed. It lets an extension refuse a call before
// any handler runs and learn the call's final status and duration. Unlike a
// ServerCallTracer it can decline a call, and it is independent of the single
// tracer factory, so it coexists with observability. A C ABI, so an extension
// links none of gRPC's types and needs no per-version rebuild.

#ifndef GRPC_SERVER_CALL_HOOK_H
#define GRPC_SERVER_CALL_HOOK_H

#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reads one request header by name, or NULL if absent; length via
// `*value_length`. The bytes are gRPC-owned, not null-terminated, and valid
// only until on_initial_metadata returns.
typedef const char* (*grpc_server_call_hook_header_lookup)(void* lookup_context,
                                                           const char* name,
                                                           size_t* value_length);

// Consulted when a call's initial metadata is parsed. Return GRPC_STATUS_OK to
// admit, any other status to refuse before any handler runs. May set `*call_data`
// to a per-call pointer the extension owns; gRPC passes it to on_call_end
// unchanged. Runs on the transport, so it acts even while a handler thread is
// stalled; must not block.
typedef grpc_status_code (*grpc_server_call_hook_on_initial_metadata_fn)(
    void* user_data, grpc_server_call_hook_header_lookup lookup,
    void* lookup_context, void** call_data);

// Called when the call ends, with the per-call pointer from on_initial_metadata
// (NULL if none) and the final status. Fires for every call that reached
// on_initial_metadata, so it is where call_data is released. Must not block.
typedef void (*grpc_server_call_hook_on_call_end_fn)(void* user_data,
                                                     void* call_data,
                                                     grpc_status_code final_status);

// Provided by the extension. Either callback may be NULL. `struct_size` lets
// the two sides version independently; gRPC reads only the fields it covers.
typedef struct {
  size_t struct_size;  // Set to sizeof(grpc_server_call_hook_vtable).
  grpc_server_call_hook_on_initial_metadata_fn on_initial_metadata;
  grpc_server_call_hook_on_call_end_fn on_call_end;
  void* user_data;
} grpc_server_call_hook_vtable;

// Smallest struct gRPC accepts: through on_initial_metadata.
#define GRPC_SERVER_CALL_HOOK_MIN_SIZE                             \
  (offsetof(grpc_server_call_hook_vtable, on_initial_metadata) +  \
   sizeof(grpc_server_call_hook_on_initial_metadata_fn))

// Installs the process-wide hook, or clears it when `vtable` is NULL. Not
// synchronized with calls in flight, so install before serving; the vtable must
// outlive every call.
GRPCAPI void grpc_server_call_hook_register(
    const grpc_server_call_hook_vtable* vtable);

#ifdef __cplusplus
}
#endif

#endif  // GRPC_SERVER_CALL_HOOK_H
