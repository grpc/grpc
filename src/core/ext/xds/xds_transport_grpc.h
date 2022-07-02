//
// Copyright 2022 gRPC authors.
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

#ifndef GRPC_CORE_EXT_XDS_XDS_TRANSPORT_GRPC_H
#define GRPC_CORE_EXT_XDS_XDS_TRANSPORT_GRPC_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/impl/codegen/connectivity_state.h>
#include <grpc/impl/codegen/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/xds/xds_transport.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/security/credentials/channel_creds_registry.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/lame_client.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

class GrpcXdsTransportFactory : public XdsTransportFactory {
 public:
  class GrpcXdsTransport;

  explicit GrpcXdsTransportFactory(const grpc_channel_args* args);
  ~GrpcXdsTransportFactory();

  void Orphan() override { Unref(); }

  OrphanablePtr<XdsTransport> Create(
      const XdsBootstrap::XdsServer& server,
      std::function<void(absl::Status)> on_connectivity_failure,
      absl::Status* status) override;

  grpc_pollset_set* interested_parties() const { return interested_parties_; }

 private:
  grpc_channel_args* args_;
  grpc_pollset_set* interested_parties_;
};

class GrpcXdsTransportFactory::GrpcXdsTransport
    : public XdsTransportFactory::XdsTransport {
 public:
  class GrpcStreamingCall;

  GrpcXdsTransport(GrpcXdsTransportFactory* factory,
                   const XdsBootstrap::XdsServer& server,
                   std::function<void(absl::Status)> on_connectivity_failure,
                   absl::Status* status);
  ~GrpcXdsTransport();

  void Orphan() override;

  OrphanablePtr<StreamingCall> CreateStreamingCall(
      const char* method,
      std::unique_ptr<StreamingCall::EventHandler> event_handler) override;

  void ResetBackoff() override;

 private:
  class StateWatcher;

  GrpcXdsTransportFactory* factory_;  // Not owned.
  grpc_channel* channel_;
  StateWatcher* watcher_;
};

class GrpcXdsTransportFactory::GrpcXdsTransport::GrpcStreamingCall
    : public XdsTransportFactory::XdsTransport::StreamingCall {
 public:
  GrpcStreamingCall(RefCountedPtr<GrpcXdsTransportFactory> factory,
                    grpc_channel* channel, const char* method,
                    std::unique_ptr<StreamingCall::EventHandler> event_handler);
  ~GrpcStreamingCall();

  void Orphan() override;

  void SendMessage(std::string payload) override;

  bool SendMessagePending() override;

 private:
  static void OnRequestSent(void* arg, grpc_error_handle error);
  static void OnResponseReceived(void* arg, grpc_error_handle /*error*/);
  static void OnStatusReceived(void* arg, grpc_error_handle /*error*/);

  RefCountedPtr<GrpcXdsTransportFactory> factory_;

  std::unique_ptr<StreamingCall::EventHandler> event_handler_;

  // Always non-NULL.
  grpc_call* call_;

  // recv_initial_metadata
  grpc_metadata_array initial_metadata_recv_;

  // send_message
  grpc_byte_buffer* send_message_payload_ = nullptr;
  grpc_closure on_request_sent_;

  // recv_message
  grpc_byte_buffer* recv_message_payload_ = nullptr;
  grpc_closure on_response_received_;

  // recv_trailing_metadata
  grpc_metadata_array trailing_metadata_recv_;
  grpc_status_code status_code_;
  grpc_slice status_details_;
  grpc_closure on_status_received_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_TRANSPORT_GRPC_H
