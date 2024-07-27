//
//
// Copyright 2015 gRPC authors.
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
//

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/debug/trace.h"

void grpc_metadata_array_init(grpc_metadata_array* array) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_metadata_array_init(array=" << array << ")";
  memset(array, 0, sizeof(*array));
}

void grpc_metadata_array_destroy(grpc_metadata_array* array) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_metadata_array_destroy(array=" << array << ")";
  gpr_free(array->metadata);
}
