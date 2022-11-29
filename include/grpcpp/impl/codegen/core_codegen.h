/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_CORE_CODEGEN_H
#define GRPCPP_IMPL_CODEGEN_CORE_CODEGEN_H

// IWYU pragma: private

// This file should be compiled as part of grpcpp.

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>

namespace grpc {

/// Implementation of the core codegen interface.
class CoreCodegen final : public CoreCodegenInterface {
 private:
  grpc_slice grpc_slice_split_tail(grpc_slice* s, size_t split) override;
  grpc_slice grpc_slice_split_head(grpc_slice* s, size_t split) override;
  grpc_slice grpc_slice_sub(grpc_slice s, size_t begin, size_t end) override;
  void grpc_slice_buffer_add(grpc_slice_buffer* sb, grpc_slice slice) override;
  void grpc_slice_buffer_pop(grpc_slice_buffer* sb) override;
  void grpc_slice_buffer_add_indexed(grpc_slice_buffer* sb,
                                     grpc_slice slice) override;
  grpc_slice grpc_slice_from_static_buffer(const void* buffer,
                                           size_t length) override;
  grpc_slice grpc_slice_from_copied_buffer(const void* buffer,
                                           size_t length) override;
  void grpc_metadata_array_init(grpc_metadata_array* array) override;
  void grpc_metadata_array_destroy(grpc_metadata_array* array) override;

  gpr_timespec gpr_inf_future(gpr_clock_type type) override;
  gpr_timespec gpr_time_0(gpr_clock_type type) override;

  const Status& ok() override;
  const Status& cancelled() override;

  void assert_fail(const char* failed_assertion, const char* file,
                   int line) override;
};

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CORE_CODEGEN_H
