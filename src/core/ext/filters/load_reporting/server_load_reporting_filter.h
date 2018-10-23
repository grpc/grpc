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

#ifndef GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_SERVER_LOAD_REPORTING_FILTER_H
#define GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_SERVER_LOAD_REPORTING_FILTER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/cpp/common/channel_filter.h"

namespace grpc {

class ServerLoadReportingChannelData : public ChannelData {
 public:
  grpc_error* Init(grpc_channel_element* elem,
                   grpc_channel_element_args* args) override;

  // Getters.
  const char* peer_identity() { return peer_identity_; }
  size_t peer_identity_len() { return peer_identity_len_; }

 private:
  // The peer's authenticated identity.
  char* peer_identity_ = nullptr;
  size_t peer_identity_len_ = 0;
};

class ServerLoadReportingCallData : public CallData {
 public:
  grpc_error* Init(grpc_call_element* elem,
                   const grpc_call_element_args* args) override;

  void Destroy(grpc_call_element* elem, const grpc_call_final_info* final_info,
               grpc_closure* then_call_closure) override;

  void StartTransportStreamOpBatch(grpc_call_element* elem,
                                   TransportStreamOpBatch* op) override;

 private:
  // From the peer_string_ in calld, extracts the client IP string (owned by
  // caller), e.g., "01020a0b". Upon failure, set the output pointer to null and
  // size to zero.
  void GetCensusSafeClientIpString(char** client_ip_string, size_t* size);

  // Concatenates the client IP address and the load reporting token, then
  // stores the result into the call data.
  void StoreClientIpAndLrToken(const char* lr_token, size_t lr_token_len);

  // This matches the classification of the status codes in
  // googleapis/google/rpc/code.proto.
  static const char* GetStatusTagForStatus(grpc_status_code status);

  // Records the call start.
  static void RecvInitialMetadataReady(void* arg, grpc_error* err);

  // From the initial metadata, extracts the service_method_, target_host_, and
  // client_ip_and_lr_token_.
  static grpc_filtered_mdelem RecvInitialMetadataFilter(void* user_data,
                                                        grpc_mdelem md);

  // Records the other call metrics.
  static grpc_filtered_mdelem SendTrailingMetadataFilter(void* user_data,
                                                         grpc_mdelem md);

  // The peer string (a member of the recv_initial_metadata op). Note that
  // gpr_atm itself is a pointer type here, making "peer_string_" effectively a
  // double pointer.
  const gpr_atm* peer_string_;

  // The received initial metadata (a member of the recv_initial_metadata op).
  // When it is ready, we will extract some data from it via
  // recv_initial_metadata_ready_ closure, before the original
  // recv_initial_metadata_ready closure.
  grpc_metadata_batch* recv_initial_metadata_;

  // The original recv_initial_metadata closure, which is wrapped by our own
  // closure (recv_initial_metadata_ready_) to capture the incoming initial
  // metadata.
  grpc_closure* original_recv_initial_metadata_ready_;

  // The closure that wraps the original closure. Scheduled when
  // recv_initial_metadata_ is ready.
  grpc_closure recv_initial_metadata_ready_;

  // Corresponds to the :path header.
  grpc_slice service_method_;

  // The backend host that the client thinks it's talking to. This may be
  // different from the actual backend in the case of, for example,
  // load-balanced targets. We store a copy of the metadata slice in order to
  // lowercase it. */
  char* target_host_;
  size_t target_host_len_;

  // The client IP address (including a length prefix) and the load reporting
  // token.
  char* client_ip_and_lr_token_;
  size_t client_ip_and_lr_token_len_;
};

}  // namespace grpc

#endif /* GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_SERVER_LOAD_REPORTING_FILTER_H \
        */
