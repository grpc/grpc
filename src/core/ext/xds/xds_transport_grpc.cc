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

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_transport_grpc.h"

#include <string.h>

#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include "absl/strings/str_cat.h"

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/security/credentials/channel_creds_registry.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/init_internally.h"
#include "src/core/lib/surface/lame_client.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

//
// GrpcXdsTransportFactory::GrpcXdsTransport::GrpcStreamingCall
//

GrpcXdsTransportFactory::GrpcXdsTransport::GrpcStreamingCall::GrpcStreamingCall(
    RefCountedPtr<GrpcXdsTransportFactory> factory, Channel* channel,
    const char* method,
    std::unique_ptr<StreamingCall::EventHandler> event_handler)
    : factory_(std::move(factory)), event_handler_(std::move(event_handler)) {
  // Create call.
  call_ = channel->CreateCall(
      /*parent_call=*/nullptr, GRPC_PROPAGATE_DEFAULTS, /*cq=*/nullptr,
      factory_->interested_parties(), Slice::FromStaticString(method),
      /*authority=*/absl::nullopt, Timestamp::InfFuture(),
      /*registered_method=*/true);
  GPR_ASSERT(call_ != nullptr);
  // Init data associated with the call.
  grpc_metadata_array_init(&initial_metadata_recv_);
  grpc_metadata_array_init(&trailing_metadata_recv_);
  // Initialize closure to be used for sending messages.
  GRPC_CLOSURE_INIT(&on_request_sent_, OnRequestSent, this, nullptr);
  // Start ops on the call.
  grpc_call_error call_error;
  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  // Send initial metadata.
  grpc_op* op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY |
              GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
  op->reserved = nullptr;
  ++op;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  ++op;
  // Ref will be released in the callback
  GRPC_CLOSURE_INIT(
      &on_recv_initial_metadata_, OnRecvInitialMetadata,
      this->Ref(DEBUG_LOCATION, "OnRecvInitialMetadata").release(), nullptr);
  call_error = grpc_call_start_batch_and_execute(
      call_, ops, static_cast<size_t>(op - ops), &on_recv_initial_metadata_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Start a batch for recv_trailing_metadata.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv_;
  op->data.recv_status_on_client.status = &status_code_;
  op->data.recv_status_on_client.status_details = &status_details_;
  op->flags = 0;
  op->reserved = nullptr;
  ++op;
  // This callback signals the end of the call, so it relies on the initial
  // ref instead of a new ref. When it's invoked, it's the initial ref that is
  // unreffed.
  GRPC_CLOSURE_INIT(&on_status_received_, OnStatusReceived, this, nullptr);
  call_error = grpc_call_start_batch_and_execute(
      call_, ops, static_cast<size_t>(op - ops), &on_status_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  GRPC_CLOSURE_INIT(&on_response_received_, OnResponseReceived, this, nullptr);
}

GrpcXdsTransportFactory::GrpcXdsTransport::GrpcStreamingCall::
    ~GrpcStreamingCall() {
  grpc_metadata_array_destroy(&trailing_metadata_recv_);
  grpc_byte_buffer_destroy(send_message_payload_);
  grpc_byte_buffer_destroy(recv_message_payload_);
  CSliceUnref(status_details_);
  GPR_ASSERT(call_ != nullptr);
  grpc_call_unref(call_);
}

void GrpcXdsTransportFactory::GrpcXdsTransport::GrpcStreamingCall::Orphan() {
  GPR_ASSERT(call_ != nullptr);
  // If we are here because xds_client wants to cancel the call,
  // OnStatusReceived() will complete the cancellation and clean up.
  // Otherwise, we are here because xds_client has to orphan a failed call,
  // in which case the following cancellation will be a no-op.
  grpc_call_cancel_internal(call_);
  // Note that the initial ref is held by OnStatusReceived(), so the
  // corresponding unref happens there instead of here.
}

void GrpcXdsTransportFactory::GrpcXdsTransport::GrpcStreamingCall::SendMessage(
    std::string payload) {
  // Create payload.
  grpc_slice slice = grpc_slice_from_cpp_string(std::move(payload));
  send_message_payload_ = grpc_raw_byte_buffer_create(&slice, 1);
  CSliceUnref(slice);
  // Send the message.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message = send_message_payload_;
  Ref(DEBUG_LOCATION, "OnRequestSent").release();
  grpc_call_error call_error =
      grpc_call_start_batch_and_execute(call_, &op, 1, &on_request_sent_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void GrpcXdsTransportFactory::GrpcXdsTransport::GrpcStreamingCall::
    StartRecvMessage() {
  Ref(DEBUG_LOCATION, "StartRecvMessage").release();
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_MESSAGE;
  op.data.recv_message.recv_message = &recv_message_payload_;
  GPR_ASSERT(call_ != nullptr);
  const grpc_call_error call_error =
      grpc_call_start_batch_and_execute(call_, &op, 1, &on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void GrpcXdsTransportFactory::GrpcXdsTransport::GrpcStreamingCall::
    OnRecvInitialMetadata(void* arg, grpc_error_handle /*error*/) {
  RefCountedPtr<GrpcStreamingCall> self(static_cast<GrpcStreamingCall*>(arg));
  grpc_metadata_array_destroy(&self->initial_metadata_recv_);
}

void GrpcXdsTransportFactory::GrpcXdsTransport::GrpcStreamingCall::
    OnRequestSent(void* arg, grpc_error_handle error) {
  RefCountedPtr<GrpcStreamingCall> self(static_cast<GrpcStreamingCall*>(arg));
  // Clean up the sent message.
  grpc_byte_buffer_destroy(self->send_message_payload_);
  self->send_message_payload_ = nullptr;
  // Invoke request handler.
  self->event_handler_->OnRequestSent(error.ok());
}

void GrpcXdsTransportFactory::GrpcXdsTransport::GrpcStreamingCall::
    OnResponseReceived(void* arg, grpc_error_handle /*error*/) {
  RefCountedPtr<GrpcStreamingCall> self(static_cast<GrpcStreamingCall*>(arg));
  // If there was no payload, then we received status before we received
  // another message, so we stop reading.
  if (self->recv_message_payload_ != nullptr) {
    // Process the response.
    grpc_byte_buffer_reader bbr;
    grpc_byte_buffer_reader_init(&bbr, self->recv_message_payload_);
    grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
    grpc_byte_buffer_reader_destroy(&bbr);
    grpc_byte_buffer_destroy(self->recv_message_payload_);
    self->recv_message_payload_ = nullptr;
    self->event_handler_->OnRecvMessage(StringViewFromSlice(response_slice));
    CSliceUnref(response_slice);
  }
}

void GrpcXdsTransportFactory::GrpcXdsTransport::GrpcStreamingCall::
    OnStatusReceived(void* arg, grpc_error_handle /*error*/) {
  RefCountedPtr<GrpcStreamingCall> self(static_cast<GrpcStreamingCall*>(arg));
  self->event_handler_->OnStatusReceived(
      absl::Status(static_cast<absl::StatusCode>(self->status_code_),
                   StringViewFromSlice(self->status_details_)));
}

//
// GrpcXdsTransportFactory::GrpcXdsTransport::StateWatcher
//

class GrpcXdsTransportFactory::GrpcXdsTransport::StateWatcher final
    : public AsyncConnectivityStateWatcherInterface {
 public:
  explicit StateWatcher(
      std::function<void(absl::Status)> on_connectivity_failure)
      : on_connectivity_failure_(std::move(on_connectivity_failure)) {}

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                 const absl::Status& status) override {
    if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      on_connectivity_failure_(absl::Status(
          status.code(),
          absl::StrCat("channel in TRANSIENT_FAILURE: ", status.message())));
    }
  }

  std::function<void(absl::Status)> on_connectivity_failure_;
};

//
// GrpcXdsClient::GrpcXdsTransport
//

namespace {

OrphanablePtr<Channel> CreateXdsChannel(
    const ChannelArgs& args, const GrpcXdsBootstrap::GrpcXdsServer& server) {
  RefCountedPtr<grpc_channel_credentials> channel_creds =
      CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
          server.channel_creds_config());
  return OrphanablePtr<Channel>(Channel::FromC(grpc_channel_create(
      server.server_uri().c_str(), channel_creds.get(), args.ToC().get())));
}

}  // namespace

GrpcXdsTransportFactory::GrpcXdsTransport::GrpcXdsTransport(
    GrpcXdsTransportFactory* factory, const XdsBootstrap::XdsServer& server,
    std::function<void(absl::Status)> on_connectivity_failure,
    absl::Status* status)
    : factory_(factory) {
  channel_ = CreateXdsChannel(
      factory->args_,
      static_cast<const GrpcXdsBootstrap::GrpcXdsServer&>(server));
  GPR_ASSERT(channel_ != nullptr);
  if (channel_->IsLame()) {
    *status = absl::UnavailableError("xds client has a lame channel");
  } else {
    watcher_ = new StateWatcher(std::move(on_connectivity_failure));
    channel_->AddConnectivityWatcher(
        GRPC_CHANNEL_IDLE,
        OrphanablePtr<AsyncConnectivityStateWatcherInterface>(watcher_));
  }
}

void GrpcXdsTransportFactory::GrpcXdsTransport::Orphan() {
  if (!channel_->IsLame()) {
    channel_->RemoveConnectivityWatcher(watcher_);
  }
  // Do an async hop before unreffing.  This avoids a deadlock upon
  // shutdown in the case where the xDS channel is itself an xDS channel
  // (e.g., when using one control plane to find another control plane).
  grpc_event_engine::experimental::GetDefaultEventEngine()->Run([this]() {
    ApplicationCallbackExecCtx application_exec_ctx;
    ExecCtx exec_ctx;
    Unref();
  });
}

OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall>
GrpcXdsTransportFactory::GrpcXdsTransport::CreateStreamingCall(
    const char* method,
    std::unique_ptr<StreamingCall::EventHandler> event_handler) {
  return MakeOrphanable<GrpcStreamingCall>(
      factory_->RefAsSubclass<GrpcXdsTransportFactory>(DEBUG_LOCATION,
                                                       "StreamingCall"),
      channel_.get(), method, std::move(event_handler));
}

void GrpcXdsTransportFactory::GrpcXdsTransport::ResetBackoff() {
  channel_->ResetConnectionBackoff();
}

//
// GrpcXdsTransportFactory
//

namespace {

ChannelArgs ModifyChannelArgs(const ChannelArgs& args) {
  return args.Set(GRPC_ARG_KEEPALIVE_TIME_MS, Duration::Minutes(5).millis());
}

}  // namespace

GrpcXdsTransportFactory::GrpcXdsTransportFactory(const ChannelArgs& args)
    : args_(ModifyChannelArgs(args)),
      interested_parties_(grpc_pollset_set_create()) {
  // Calling grpc_init to ensure gRPC does not shut down until the XdsClient is
  // destroyed.
  InitInternally();
}

GrpcXdsTransportFactory::~GrpcXdsTransportFactory() {
  grpc_pollset_set_destroy(interested_parties_);
  // Calling grpc_shutdown to ensure gRPC does not shut down until the XdsClient
  // is destroyed.
  ShutdownInternally();
}

OrphanablePtr<XdsTransportFactory::XdsTransport>
GrpcXdsTransportFactory::Create(
    const XdsBootstrap::XdsServer& server,
    std::function<void(absl::Status)> on_connectivity_failure,
    absl::Status* status) {
  return MakeOrphanable<GrpcXdsTransport>(
      this, server, std::move(on_connectivity_failure), status);
}

}  // namespace grpc_core
