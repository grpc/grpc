// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_SDK_SERVER_AUTHZ_FILTER_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_SDK_SERVER_AUTHZ_FILTER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/security/authorization/authorization_policy_provider.h"

namespace grpc_core {

class SdkServerAuthzFilter {
 public:
  static const grpc_channel_filter kFilterVtable;

 private:
  class CallData {
   public:
    static void StartTransportStreamOpBatch(
        grpc_call_element* elem, grpc_transport_stream_op_batch* batch);
    static grpc_error_handle Init(grpc_call_element* elem,
                                  const grpc_call_element_args*);
    static void Destroy(grpc_call_element* elem,
                        const grpc_call_final_info* /*final_info*/,
                        grpc_closure* /*ignored*/);

   private:
    explicit CallData(grpc_call_element* elem);

    bool IsAuthorized(SdkServerAuthzFilter* chand);

    static void RecvInitialMetadataReady(void* arg, grpc_error_handle error);

    grpc_metadata_batch* recv_initial_metadata_batch_;
    grpc_closure* original_recv_initial_metadata_ready_;
    grpc_closure recv_initial_metadata_ready_;
  };

  SdkServerAuthzFilter(
      RefCountedPtr<grpc_auth_context> auth_context, grpc_endpoint* endpoint,
      RefCountedPtr<grpc_authorization_policy_provider> provider);

  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args);
  static void Destroy(grpc_channel_element* elem);

  RefCountedPtr<grpc_auth_context> auth_context_;
  EvaluateArgs::PerChannelArgs per_channel_evaluate_args_;
  RefCountedPtr<grpc_authorization_policy_provider> provider_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_SDK_SERVER_AUTHZ_FILTER_H
