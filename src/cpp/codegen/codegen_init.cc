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

#include <grpcpp/impl/codegen/codegen_init.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/grpc_library.h>

/// Null-initializes the global gRPC variables for the codegen library. These
/// stay null in the absence of of grpc++ library. In this case, no gRPC
/// features such as the ability to perform calls will be available. Trying to
/// perform them would result in a segmentation fault when trying to deference
/// the following nulled globals. These should be associated with actual
/// as part of the instantiation of a \a grpc::GrpcLibraryInitializer variable.

grpc::CoreCodegenInterface* g_core_codegen_interface = nullptr;
grpc::GrpcLibraryInterface* g_glip = nullptr;

// Implement getters and setters for interfaces
grpc::CoreCodegenInterface* grpc::get_g_core_codegen_interface() {
    return g_core_codegen_interface;
}

grpc::GrpcLibraryInterface* grpc::get_g_glip() {
    return g_glip;
}

void grpc::init_g_core_codegen_interface(grpc::CoreCodegenInterface* ifc_ptr) {
    if (g_core_codegen_interface == nullptr) {
        g_core_codegen_interface = ifc_ptr;
    }
}

void grpc::init_g_glip(grpc::GrpcLibraryInterface* ifc_ptr) {
    if (g_glip == nullptr) {
        g_glip = ifc_ptr;
    }
}
