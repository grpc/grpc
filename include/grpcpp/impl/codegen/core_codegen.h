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
  const Status& ok() override;
  const Status& cancelled() override;

  void assert_fail(const char* failed_assertion, const char* file,
                   int line) override;
};

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CORE_CODEGEN_H
