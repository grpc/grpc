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

// A process-wide hook that can refuse a server call before any handler runs
// and learns how each call it saw ended. It coexists with a ServerCallTracer.

#ifndef GRPC_SERVER_CALL_HOOK_H
#define GRPC_SERVER_CALL_HOOK_H

#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Valid only during on_call_start, for its `call`.
typedef struct {
  // A request header or ":path": 1 and its bytes (not NUL-terminated, valid
  // until on_call_start returns; repeated values joined with ','), or 0.
  int (*get)(void* call, const char* key, size_t key_len, const char** value,
             size_t* value_len);
  // A response header, copied and sent only if the call is refused; 0 for a key
  // that is not lowercase token characters or is grpc-status, grpc-message,
  // content-type or te, a non-binary value with a control byte, or a value its
  // key's trait cannot parse.
  int (*add)(void* call, const char* key, size_t key_len, const char* value,
             size_t value_len);
} grpc_server_call_hook_ops;

typedef struct {
  size_t struct_size;  // sizeof(grpc_server_call_hook)
  void* user_data;
  // Called once initial metadata is parsed; must not block. GRPC_STATUS_OK
  // admits; anything else refuses with that status and `*message` (copied;
  // "call refused" when unset). `*call_word` starts at 0; on_call_end gets it.
  grpc_status_code (*on_call_start)(void* user_data,
                                    const grpc_server_call_hook_ops* ops,
                                    void* call, uint64_t* call_word,
                                    const char** message, size_t* message_len);
  // Once per call on_call_start saw, with its final status and the
  // microseconds since on_call_start. Must not block.
  void (*on_call_end)(void* user_data, uint64_t call_word,
                      grpc_status_code status, int64_t duration_us);
} grpc_server_call_hook;

// Installs the hook, or clears it with NULL, before serving; it must outlive
// every call. A hook with a smaller struct_size than this header's is ignored.
GRPCAPI void grpc_server_call_hook_register(const grpc_server_call_hook* hook);

#ifdef __cplusplus
}
#endif

#endif  // GRPC_SERVER_CALL_HOOK_H
