//
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
//

#ifndef GRPC_CORE_EXT_FILTERS_RBAC_RBAC_FILTER_H
#define GRPC_CORE_EXT_FILTERS_RBAC_RBAC_FILTER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/authorization/evaluate_args.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

// Filter used when xDS server config fetcher provides a configuration with an
// HTTP RBAC filter. Also serves as the type for channel data for the filter.
class RbacFilter {
 public:
  // This channel filter is intended to be used by connections on xDS enabled
  // servers configured with RBAC. The RBAC filter fetches the RBAC policy from
  // the method config of service config returned by the ServerConfigSelector,
  // and enforces the RBAC policy.
  static const grpc_channel_filter kFilterVtable;

 private:
  class CallData {
   public:
    static grpc_error_handle Init(grpc_call_element* elem,
                                  const grpc_call_element_args* args);
    static void Destroy(grpc_call_element* elem,
                        const grpc_call_final_info* /* final_info */,
                        grpc_closure* /* then_schedule_closure */);
    static void StartTransportStreamOpBatch(grpc_call_element* elem,
                                            grpc_transport_stream_op_batch* op);

   private:
    CallData(grpc_call_element* elem, const grpc_call_element_args& args);
    static void RecvInitialMetadataReady(void* user_data,
                                         grpc_error_handle error);

    grpc_call_context_element* call_context_;
    // State for keeping track of recv_initial_metadata
    grpc_metadata_batch* recv_initial_metadata_ = nullptr;
    grpc_closure* original_recv_initial_metadata_ready_ = nullptr;
    grpc_closure recv_initial_metadata_ready_;
  };

  RbacFilter(size_t index,
             EvaluateArgs::PerChannelArgs per_channel_evaluate_args);
  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args);
  static void Destroy(grpc_channel_element* elem);

  // The index of this filter instance among instances of the same filter.
  size_t index_;
  // Assigned index for service config data from the parser.
  const size_t service_config_parser_index_;
  // Per channel args used for authorization.
  EvaluateArgs::PerChannelArgs per_channel_evaluate_args_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_RBAC_RBAC_FILTER_H
