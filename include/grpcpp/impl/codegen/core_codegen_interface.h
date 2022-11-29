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

#ifndef GRPCPP_IMPL_CODEGEN_CORE_CODEGEN_INTERFACE_H
#define GRPCPP_IMPL_CODEGEN_CORE_CODEGEN_INTERFACE_H

// IWYU pragma: private

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/sync.h>
#include <grpcpp/support/config.h>
#include <grpcpp/support/status.h>

namespace grpc {

/// Interface between the codegen library and the minimal subset of core
/// features required by the generated code.
///
/// All undocumented methods are simply forwarding the call to their namesakes.
/// Please refer to their corresponding documentation for details.
///
/// \warning This interface should be considered internal and private.
class CoreCodegenInterface {
 public:
  virtual ~CoreCodegenInterface() = default;

  /// Upon a failed assertion, log the error.
  virtual void assert_fail(const char* failed_assertion, const char* file,
                           int line) = 0;

  virtual void grpc_slice_unref(grpc_slice slice) = 0;
  virtual grpc_slice grpc_slice_ref(grpc_slice slice) = 0;
  virtual grpc_slice grpc_slice_split_tail(grpc_slice* s, size_t split) = 0;
  virtual grpc_slice grpc_slice_split_head(grpc_slice* s, size_t split) = 0;
  virtual grpc_slice grpc_slice_sub(grpc_slice s, size_t begin, size_t end) = 0;
  virtual void grpc_slice_buffer_add(grpc_slice_buffer* sb,
                                     grpc_slice slice) = 0;
  virtual void grpc_slice_buffer_add_indexed(grpc_slice_buffer* sb,
                                             grpc_slice slice) = 0;
  virtual void grpc_slice_buffer_pop(grpc_slice_buffer* sb) = 0;
  virtual grpc_slice grpc_slice_from_static_buffer(const void* buffer,
                                                   size_t length) = 0;
  virtual grpc_slice grpc_slice_from_copied_buffer(const void* buffer,
                                                   size_t length) = 0;

  virtual void grpc_metadata_array_init(grpc_metadata_array* array) = 0;
  virtual void grpc_metadata_array_destroy(grpc_metadata_array* array) = 0;

  virtual const Status& ok() = 0;
  virtual const Status& cancelled() = 0;

  virtual gpr_timespec gpr_inf_future(gpr_clock_type type) = 0;
  virtual gpr_timespec gpr_time_0(gpr_clock_type type) = 0;
};

extern CoreCodegenInterface* g_core_codegen_interface;

/// Codegen specific version of \a GPR_ASSERT.
#define GPR_CODEGEN_ASSERT(x)                                              \
  do {                                                                     \
    if (GPR_UNLIKELY(!(x))) {                                              \
      grpc::g_core_codegen_interface->assert_fail(#x, __FILE__, __LINE__); \
    }                                                                      \
  } while (0)

/// Codegen specific version of \a GPR_DEBUG_ASSERT.
#ifndef NDEBUG
#define GPR_CODEGEN_DEBUG_ASSERT(x) GPR_CODEGEN_ASSERT(x)
#else
#define GPR_CODEGEN_DEBUG_ASSERT(x) \
  do {                              \
  } while (0)
#endif

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CORE_CODEGEN_INTERFACE_H
