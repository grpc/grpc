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

#ifndef GRPC_IMPL_CODEGEN_MESSAGE_SEGMENT_H
#define GRPC_IMPL_CODEGEN_MESSAGE_SEGMENT_H

#include <grpc/impl/codegen/port_platform.h>
#include <grpc/impl/codegen/slice.h>

typedef struct grpc_wrseg grpc_wrseg;
typedef struct grpc_wrseg_vtable grpc_wrseg_vtable;

struct grpc_wrseg {
  const grpc_wrseg_vtable *vtable;
};

struct grpc_wrseg_vtable {
  void (*append_to)(grpc_wrseg *seg, grpc_slice_buffer *sb);
};

typedef struct grpc_rdseg grpc_rdseg;
typedef struct grpc_rdseg_vtable grpc_rdseg_vtable;

struct grpc_rdseg {
  const grpc_rdseg_vtable *vtable;
};

struct grpc_rdseg_data {
  grpc_slice_buffer *slice_buffer;
  uint64_t min_read;
  uint64_t max_read;
};

struct grpc_rdseg_vtable {
  void (*begin_read)(grpc_rdseg *seg, grpc_rdseg_data *data);
  void (*end_read)(grpc_rdseg *seg, const grpc_rdseg_data *data);
};

#endif /* GRPC_IMPL_CODEGEN_MESSAGE_SEGMENT_H */
