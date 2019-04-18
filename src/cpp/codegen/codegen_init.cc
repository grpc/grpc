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

#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/grpc_library.h>

/// Null-initializes the global gRPC variables for the codegen library. These
/// stay null in the absence of grpc++ library. In this case, no gRPC
/// features such as the ability to perform calls will be available. Trying to
/// perform them would result in a segmentation fault when trying to deference
/// the following nulled globals. These should be associated with actual
/// as part of the instantiation of a \a grpc::GrpcLibraryInitializer variable.

grpc::CoreCodegenInterface* grpc::g_core_codegen_interface;
grpc::GrpcLibraryInterface* grpc::g_glip;
