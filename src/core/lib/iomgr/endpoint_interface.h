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

#ifndef GRPC_CORE_LIB_IOMGR_ENDPOINT_H
#define GRPC_CORE_LIB_IOMGR_ENDPOINT_H

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/time.h>
#include "src/core/lib/iomgr/polling_interface.h"
#include "src/core/lib/iomgr/resource_quota.h"

namespace grpc_core {

// An endpoint caps a streaming channel between two communicating processes.
// Examples may be: a tcp socket, <stdin+stdout>, or some shared memory.
class Endpoint {
 public:
  // When data is available on the connection, calls the callback with slices.
  // Callback success indicates that the endpoint can accept more reads,
  // failure indicates the endpoint is closed. Valid slices may be placed into
  // \a slices even when the callback is invoked with error != GRPC_ERROR_NONE.
  virtual void Read(grpc_exec_ctx* exec_ctx, grpc_slice_buffer* slices,
                    grpc_closure* cb) = 0;

  // Write slices out to the socket.
  //
  // If the connection is ready for more data after the end of the call, it
  // returns GRPC_ENDPOINT_DONE.
  // Otherwise it returns GRPC_ENDPOINT_PENDING and calls cb when the
  // connection is ready for more data.
  // \a slices may be mutated at will by the endpoint until cb is called.
  // No guarantee is made to the content of slices after a write EXCEPT that
  // it is a valid slice buffer.
  virtual void Write(grpc_exec_ctx* exec_ctx, grpc_slice_buffer* slices,
                     grpc_closure* cb) = 0;

  // Causes any pending and future read/write callbacks to schedule immediately
  // with error==why
  virtual void Shutdown(grpc_exec_ctx* exec_ctx, grpc_error* why) = 0;

  // Get the Pollable associated with this Endpoint
  virtual Pollable* GetPollable() = 0;

  // Get the grpc_resource_user associated with this Endpoint
  virtual grpc_resource_user* GetResourceUser() = 0;

  // Fetch the peer uri associated with this endpoint (must be freed with
  // gpr_free)
  virtual char* GetPeer() = 0;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_ENDPOINT_H */
