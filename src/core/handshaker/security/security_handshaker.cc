//
//
// Copyright 2015 gRPC authors.
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
//

#include "src/core/handshaker/security/security_handshaker.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/channelz/channelz.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/handshaker_factory.h"
#include "src/core/handshaker/handshaker_registry.h"
#include "src/core/handshaker/security/secure_endpoint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/tsi/transport_security_grpc.h"

#define GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE 256

namespace grpc_core {

namespace {

RefCountedPtr<channelz::SocketNode::Security>
MakeChannelzSecurityFromAuthContext(grpc_auth_context* auth_context) {
  RefCountedPtr<channelz::SocketNode::Security> security =
      MakeRefCounted<channelz::SocketNode::Security>();
  // TODO(yashykt): Currently, we are assuming TLS by default and are only able
  // to fill in the remote certificate but we should ideally be able to fill in
  // other fields in
  // https://github.com/grpc/grpc/blob/fcd43e90304862a823316b224ee733d17a8cfd90/src/proto/grpc/channelz/channelz.proto#L326
  // from grpc_auth_context.
  security->type = channelz::SocketNode::Security::ModelType::kTls;
  security->tls = absl::make_optional<channelz::SocketNode::Security::Tls>();
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context, GRPC_X509_PEM_CERT_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (prop != nullptr) {
    security->tls->remote_certificate =
        std::string(prop->value, prop->value_length);
  }
  return security;
}

class LegacySecurityHandshaker : public Handshaker {
 public:
  LegacySecurityHandshaker(tsi_handshaker* handshaker,
                           grpc_security_connector* connector,
                           const ChannelArgs& args);
  ~LegacySecurityHandshaker() override;
  absl::string_view name() const override { return "security"; }
  void DoHandshake(
      HandshakerArgs* args,
      absl::AnyInvocable<void(absl::Status)> on_handshake_done) override;
  void Shutdown(absl::Status error) override;

 private:
  grpc_error_handle DoHandshakerNextLocked(const unsigned char* bytes_received,
                                           size_t bytes_received_size);

  grpc_error_handle OnHandshakeNextDoneLocked(
      tsi_result result, const unsigned char* bytes_to_send,
      size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result);
  void HandshakeFailedLocked(absl::Status error);
  void Finish(absl::Status status);

  void OnHandshakeDataReceivedFromPeerFn(absl::Status error);
  void OnHandshakeDataSentToPeerFn(absl::Status error);
  static void OnHandshakeDataReceivedFromPeerFnScheduler(
      void* arg, grpc_error_handle error);
  static void OnHandshakeDataSentToPeerFnScheduler(void* arg,
                                                   grpc_error_handle error);
  static void OnHandshakeNextDoneGrpcWrapper(
      tsi_result result, void* user_data, const unsigned char* bytes_to_send,
      size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result);
  static void OnPeerCheckedFn(void* arg, grpc_error_handle error);
  void OnPeerCheckedInner(grpc_error_handle error);
  size_t MoveReadBufferIntoHandshakeBuffer();
  grpc_error_handle CheckPeerLocked();

  // State set at creation time.
  tsi_handshaker* handshaker_;
  RefCountedPtr<grpc_security_connector> connector_;

  Mutex mu_;

  bool is_shutdown_ = false;

  // State saved while performing the handshake.
  HandshakerArgs* args_ = nullptr;
  absl::AnyInvocable<void(absl::Status)> on_handshake_done_;

  size_t handshake_buffer_size_;
  unsigned char* handshake_buffer_;
  SliceBuffer outgoing_;
  grpc_closure on_handshake_data_sent_to_peer_;
  grpc_closure on_handshake_data_received_from_peer_;
  grpc_closure on_peer_checked_;
  RefCountedPtr<grpc_auth_context> auth_context_;
  tsi_handshaker_result* handshaker_result_ = nullptr;
  size_t max_frame_size_ = 0;
  std::string tsi_handshake_error_;
};

LegacySecurityHandshaker::LegacySecurityHandshaker(
    tsi_handshaker* handshaker, grpc_security_connector* connector,
    const ChannelArgs& args)
    : handshaker_(handshaker),
      connector_(connector->Ref(DEBUG_LOCATION, "handshake")),
      handshake_buffer_size_(GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE),
      handshake_buffer_(
          static_cast<uint8_t*>(gpr_malloc(handshake_buffer_size_))),
      max_frame_size_(
          std::max(0, args.GetInt(GRPC_ARG_TSI_MAX_FRAME_SIZE).value_or(0))) {
  GRPC_CLOSURE_INIT(&on_peer_checked_,
                    &LegacySecurityHandshaker::OnPeerCheckedFn, this,
                    grpc_schedule_on_exec_ctx);
}

LegacySecurityHandshaker::~LegacySecurityHandshaker() {
  tsi_handshaker_destroy(handshaker_);
  tsi_handshaker_result_destroy(handshaker_result_);
  gpr_free(handshake_buffer_);
  auth_context_.reset(DEBUG_LOCATION, "handshake");
  connector_.reset(DEBUG_LOCATION, "handshake");
}

size_t LegacySecurityHandshaker::MoveReadBufferIntoHandshakeBuffer() {
  size_t bytes_in_read_buffer = args_->read_buffer.Length();
  if (handshake_buffer_size_ < bytes_in_read_buffer) {
    handshake_buffer_ = static_cast<uint8_t*>(
        gpr_realloc(handshake_buffer_, bytes_in_read_buffer));
    handshake_buffer_size_ = bytes_in_read_buffer;
  }
  size_t offset = 0;
  while (args_->read_buffer.Count() > 0) {
    Slice slice = args_->read_buffer.TakeFirst();
    memcpy(handshake_buffer_ + offset, slice.data(), slice.size());
    offset += slice.size();
  }
  return bytes_in_read_buffer;
}

// If the handshake failed or we're shutting down, clean up and invoke the
// callback with the error.
void LegacySecurityHandshaker::HandshakeFailedLocked(absl::Status error) {
  if (error.ok()) {
    // If we were shut down after the handshake succeeded but before an
    // endpoint callback was invoked, we need to generate our own error.
    error = GRPC_ERROR_CREATE("Handshaker shutdown");
  }
  if (!is_shutdown_) {
    tsi_handshaker_shutdown(handshaker_);
    // Set shutdown to true so that subsequent calls to
    // security_handshaker_shutdown() do nothing.
    is_shutdown_ = true;
  }
  // Invoke callback.
  Finish(std::move(error));
}

void LegacySecurityHandshaker::Finish(absl::Status status) {
  InvokeOnHandshakeDone(args_, std::move(on_handshake_done_),
                        std::move(status));
}

void LegacySecurityHandshaker::OnPeerCheckedInner(grpc_error_handle error) {
  MutexLock lock(&mu_);
  if (!error.ok() || is_shutdown_) {
    HandshakeFailedLocked(error);
    return;
  }
  // Get unused bytes.
  const unsigned char* unused_bytes = nullptr;
  size_t unused_bytes_size = 0;
  tsi_result result = tsi_handshaker_result_get_unused_bytes(
      handshaker_result_, &unused_bytes, &unused_bytes_size);
  if (result != TSI_OK) {
    HandshakeFailedLocked(GRPC_ERROR_CREATE(
        absl::StrCat("TSI handshaker result does not provide unused bytes (",
                     tsi_result_to_string(result), ")")));
    return;
  }
  // Check whether we need to wrap the endpoint.
  tsi_frame_protector_type frame_protector_type;
  result = tsi_handshaker_result_get_frame_protector_type(
      handshaker_result_, &frame_protector_type);
  if (result != TSI_OK) {
    HandshakeFailedLocked(GRPC_ERROR_CREATE(
        absl::StrCat("TSI handshaker result does not implement "
                     "get_frame_protector_type (",
                     tsi_result_to_string(result), ")")));
    return;
  }
  tsi_zero_copy_grpc_protector* zero_copy_protector = nullptr;
  tsi_frame_protector* protector = nullptr;
  switch (frame_protector_type) {
    case TSI_FRAME_PROTECTOR_ZERO_COPY:
      ABSL_FALLTHROUGH_INTENDED;
    case TSI_FRAME_PROTECTOR_NORMAL_OR_ZERO_COPY:
      // Create zero-copy frame protector.
      result = tsi_handshaker_result_create_zero_copy_grpc_protector(
          handshaker_result_, max_frame_size_ == 0 ? nullptr : &max_frame_size_,
          &zero_copy_protector);
      if (result != TSI_OK) {
        HandshakeFailedLocked(GRPC_ERROR_CREATE(
            absl::StrCat("Zero-copy frame protector creation failed (",
                         tsi_result_to_string(result), ")")));
        return;
      }
      break;
    case TSI_FRAME_PROTECTOR_NORMAL:
      // Create normal frame protector.
      result = tsi_handshaker_result_create_frame_protector(
          handshaker_result_, max_frame_size_ == 0 ? nullptr : &max_frame_size_,
          &protector);
      if (result != TSI_OK) {
        HandshakeFailedLocked(
            GRPC_ERROR_CREATE(absl::StrCat("Frame protector creation failed (",
                                           tsi_result_to_string(result), ")")));
        return;
      }
      break;
    case TSI_FRAME_PROTECTOR_NONE:
      break;
  }
  bool has_frame_protector =
      zero_copy_protector != nullptr || protector != nullptr;
  // If we have a frame protector, create a secure endpoint.
  if (has_frame_protector) {
    if (unused_bytes_size > 0) {
      grpc_slice slice = grpc_slice_from_copied_buffer(
          reinterpret_cast<const char*>(unused_bytes), unused_bytes_size);
      args_->endpoint = grpc_secure_endpoint_create(
          protector, zero_copy_protector, std::move(args_->endpoint), &slice,
          args_->args.ToC().get(), 1);
      CSliceUnref(slice);
    } else {
      args_->endpoint = grpc_secure_endpoint_create(
          protector, zero_copy_protector, std::move(args_->endpoint), nullptr,
          args_->args.ToC().get(), 0);
    }
  } else if (unused_bytes_size > 0) {
    // Not wrapping the endpoint, so just pass along unused bytes.
    args_->read_buffer.Append(Slice::FromCopiedBuffer(
        reinterpret_cast<const char*>(unused_bytes), unused_bytes_size));
  }
  // Done with handshaker result.
  tsi_handshaker_result_destroy(handshaker_result_);
  handshaker_result_ = nullptr;
  args_->args = args_->args.SetObject(auth_context_);
  // Add channelz channel args only if frame protector is created.
  if (has_frame_protector) {
    args_->args = args_->args.SetObject(
        MakeChannelzSecurityFromAuthContext(auth_context_.get()));
  }
  // Set shutdown to true so that subsequent calls to
  // security_handshaker_shutdown() do nothing.
  is_shutdown_ = true;
  // Invoke callback.
  Finish(absl::OkStatus());
}

void LegacySecurityHandshaker::OnPeerCheckedFn(void* arg,
                                               grpc_error_handle error) {
  RefCountedPtr<LegacySecurityHandshaker>(
      static_cast<LegacySecurityHandshaker*>(arg))
      ->OnPeerCheckedInner(error);
}

grpc_error_handle LegacySecurityHandshaker::CheckPeerLocked() {
  tsi_peer peer;
  tsi_result result =
      tsi_handshaker_result_extract_peer(handshaker_result_, &peer);
  if (result != TSI_OK) {
    return GRPC_ERROR_CREATE(absl::StrCat("Peer extraction failed (",
                                          tsi_result_to_string(result), ")"));
  }
  connector_->check_peer(peer, args_->endpoint.get(), args_->args,
                         &auth_context_, &on_peer_checked_);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context_.get(), GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (!prop ||
      !strcmp(tsi_security_level_to_string(TSI_SECURITY_NONE), prop->value)) {
    global_stats().IncrementInsecureConnectionsCreated();
  }
  return absl::OkStatus();
}

grpc_error_handle LegacySecurityHandshaker::OnHandshakeNextDoneLocked(
    tsi_result result, const unsigned char* bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result) {
  grpc_error_handle error;
  // Handshaker was shutdown.
  if (is_shutdown_) {
    tsi_handshaker_result_destroy(handshaker_result);
    return GRPC_ERROR_CREATE("Handshaker shutdown");
  }
  // Read more if we need to.
  if (result == TSI_INCOMPLETE_DATA) {
    CHECK_EQ(bytes_to_send_size, 0u);
    grpc_endpoint_read(
        args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
        GRPC_CLOSURE_INIT(&on_handshake_data_received_from_peer_,
                          &LegacySecurityHandshaker::
                              OnHandshakeDataReceivedFromPeerFnScheduler,
                          this, grpc_schedule_on_exec_ctx),
        /*urgent=*/true, /*min_progress_size=*/1);
    return error;
  }
  if (result != TSI_OK) {
    auto* security_connector = args_->args.GetObject<grpc_security_connector>();
    absl::string_view connector_type = "<unknown>";
    if (security_connector != nullptr) {
      connector_type = security_connector->type().name();
    }
    // TODO(roth): Get a better signal from the TSI layer as to what
    // status code we should use here.
    return GRPC_ERROR_CREATE(absl::StrCat(
        connector_type, " handshake failed (", tsi_result_to_string(result),
        ")", (tsi_handshake_error_.empty() ? "" : ": "), tsi_handshake_error_));
  }
  // Update handshaker result.
  if (handshaker_result != nullptr) {
    CHECK_EQ(handshaker_result_, nullptr);
    handshaker_result_ = handshaker_result;
  }
  if (bytes_to_send_size > 0) {
    // Send data to peer, if needed.
    outgoing_.Clear();
    outgoing_.Append(Slice::FromCopiedBuffer(
        reinterpret_cast<const char*>(bytes_to_send), bytes_to_send_size));
    grpc_endpoint_write(
        args_->endpoint.get(), outgoing_.c_slice_buffer(),
        GRPC_CLOSURE_INIT(
            &on_handshake_data_sent_to_peer_,
            &LegacySecurityHandshaker::OnHandshakeDataSentToPeerFnScheduler,
            this, grpc_schedule_on_exec_ctx),
        nullptr, /*max_frame_size=*/INT_MAX);
  } else if (handshaker_result == nullptr) {
    // There is nothing to send, but need to read from peer.
    grpc_endpoint_read(
        args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
        GRPC_CLOSURE_INIT(&on_handshake_data_received_from_peer_,
                          &LegacySecurityHandshaker::
                              OnHandshakeDataReceivedFromPeerFnScheduler,
                          this, grpc_schedule_on_exec_ctx),
        /*urgent=*/true, /*min_progress_size=*/1);
  } else {
    // Handshake has finished, check peer and so on.
    error = CheckPeerLocked();
  }
  return error;
}

void LegacySecurityHandshaker::OnHandshakeNextDoneGrpcWrapper(
    tsi_result result, void* user_data, const unsigned char* bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result) {
  RefCountedPtr<LegacySecurityHandshaker> h(
      static_cast<LegacySecurityHandshaker*>(user_data));
  MutexLock lock(&h->mu_);
  grpc_error_handle error = h->OnHandshakeNextDoneLocked(
      result, bytes_to_send, bytes_to_send_size, handshaker_result);
  if (!error.ok()) {
    h->HandshakeFailedLocked(std::move(error));
  } else {
    h.release();  // Avoid unref
  }
}

grpc_error_handle LegacySecurityHandshaker::DoHandshakerNextLocked(
    const unsigned char* bytes_received, size_t bytes_received_size) {
  // Invoke TSI handshaker.
  const unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  tsi_handshaker_result* hs_result = nullptr;
  tsi_result result = tsi_handshaker_next(
      handshaker_, bytes_received, bytes_received_size, &bytes_to_send,
      &bytes_to_send_size, &hs_result, &OnHandshakeNextDoneGrpcWrapper, this,
      &tsi_handshake_error_);
  if (result == TSI_ASYNC) {
    // Handshaker operating asynchronously. Nothing else to do here;
    // callback will be invoked in a TSI thread.
    return absl::OkStatus();
  }
  // Handshaker returned synchronously. Invoke callback directly in
  // this thread with our existing exec_ctx.
  return OnHandshakeNextDoneLocked(result, bytes_to_send, bytes_to_send_size,
                                   hs_result);
}

// This callback might be run inline while we are still holding on to the mutex,
// so run OnHandshakeDataReceivedFromPeerFn asynchronously to avoid a deadlock.
// TODO(roth): This will no longer be necessary once we migrate to the
// EventEngine endpoint API.
void LegacySecurityHandshaker::OnHandshakeDataReceivedFromPeerFnScheduler(
    void* arg, grpc_error_handle error) {
  LegacySecurityHandshaker* handshaker =
      static_cast<LegacySecurityHandshaker*>(arg);
  handshaker->args_->event_engine->Run(
      [handshaker, error = std::move(error)]() mutable {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        handshaker->OnHandshakeDataReceivedFromPeerFn(std::move(error));
      });
}

void LegacySecurityHandshaker::OnHandshakeDataReceivedFromPeerFn(
    absl::Status error) {
  RefCountedPtr<LegacySecurityHandshaker> handshaker(this);
  MutexLock lock(&mu_);
  if (!error.ok() || is_shutdown_) {
    HandshakeFailedLocked(
        GRPC_ERROR_CREATE_REFERENCING("Handshake read failed", &error, 1));
    return;
  }
  // Copy all slices received.
  size_t bytes_received_size = MoveReadBufferIntoHandshakeBuffer();
  // Call TSI handshaker.
  error = DoHandshakerNextLocked(handshake_buffer_, bytes_received_size);
  if (!error.ok()) {
    HandshakeFailedLocked(std::move(error));
  } else {
    handshaker.release();  // Avoid unref
  }
}

// This callback might be run inline while we are still holding on to the mutex,
// so run OnHandshakeDataSentToPeerFn asynchronously to avoid a deadlock.
// TODO(roth): This will no longer be necessary once we migrate to the
// EventEngine endpoint API.
void LegacySecurityHandshaker::OnHandshakeDataSentToPeerFnScheduler(
    void* arg, grpc_error_handle error) {
  LegacySecurityHandshaker* handshaker =
      static_cast<LegacySecurityHandshaker*>(arg);
  handshaker->args_->event_engine->Run(
      [handshaker, error = std::move(error)]() mutable {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        handshaker->OnHandshakeDataSentToPeerFn(std::move(error));
      });
}

void LegacySecurityHandshaker::OnHandshakeDataSentToPeerFn(absl::Status error) {
  RefCountedPtr<LegacySecurityHandshaker> handshaker(this);
  MutexLock lock(&mu_);
  if (!error.ok() || is_shutdown_) {
    HandshakeFailedLocked(
        GRPC_ERROR_CREATE_REFERENCING("Handshake write failed", &error, 1));
    return;
  }
  // We may be done.
  if (handshaker_result_ == nullptr) {
    grpc_endpoint_read(
        args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
        GRPC_CLOSURE_INIT(&on_handshake_data_received_from_peer_,
                          &LegacySecurityHandshaker::
                              OnHandshakeDataReceivedFromPeerFnScheduler,
                          this, grpc_schedule_on_exec_ctx),
        /*urgent=*/true, /*min_progress_size=*/1);
  } else {
    error = CheckPeerLocked();
    if (!error.ok()) {
      HandshakeFailedLocked(error);
      return;
    }
  }
  handshaker.release();  // Avoid unref
}

//
// public handshaker API
//

void LegacySecurityHandshaker::Shutdown(grpc_error_handle error) {
  MutexLock lock(&mu_);
  if (!is_shutdown_) {
    is_shutdown_ = true;
    connector_->cancel_check_peer(&on_peer_checked_, std::move(error));
    tsi_handshaker_shutdown(handshaker_);
    args_->endpoint.reset();
  }
}

void LegacySecurityHandshaker::DoHandshake(
    HandshakerArgs* args,
    absl::AnyInvocable<void(absl::Status)> on_handshake_done) {
  auto ref = Ref();
  MutexLock lock(&mu_);
  args_ = args;
  on_handshake_done_ = std::move(on_handshake_done);
  size_t bytes_received_size = MoveReadBufferIntoHandshakeBuffer();
  grpc_error_handle error =
      DoHandshakerNextLocked(handshake_buffer_, bytes_received_size);
  if (!error.ok()) {
    HandshakeFailedLocked(error);
  } else {
    ref.release();  // Avoid unref
  }
}

class SecurityHandshaker : public Handshaker {
 public:
  SecurityHandshaker(tsi_handshaker* handshaker,
                     grpc_security_connector* connector,
                     const ChannelArgs& args);
  ~SecurityHandshaker() override;
  absl::string_view name() const override { return "security"; }
  void DoHandshake(
      HandshakerArgs* args,
      absl::AnyInvocable<void(absl::Status)> on_handshake_done) override;
  void Shutdown(absl::Status error) override;

 private:
  struct InitializationArgs {
    InitializationArgs(tsi_handshaker* handshaker,
                       grpc_security_connector* connector,
                       const ChannelArgs& args)
        : handshaker(handshaker, tsi_handshaker_destroy),
          connector(connector->Ref(DEBUG_LOCATION, "handshake")),
          initial_max_frame_size(
              args.GetInt(GRPC_ARG_TSI_MAX_FRAME_SIZE).value_or(0)) {}
    std::unique_ptr<tsi_handshaker, void (*)(tsi_handshaker*)> handshaker;
    RefCountedPtr<grpc_security_connector> connector;
    size_t initial_max_frame_size;
  };

  class Handshake : public RefCounted<Handshake> {
   public:
    Handshake(RefCountedPtr<SecurityHandshaker> security_handshaker,
              std::unique_ptr<InitializationArgs> initialization_args,
              HandshakerArgs* args,
              absl::AnyInvocable<void(absl::Status)> on_handshake_done)
        : security_handshaker_(std::move(security_handshaker)),
          handshaker_(std::move(initialization_args->handshaker)),
          connector_(std::move(initialization_args->connector)),
          args_(args),
          on_handshake_done_(std::move(on_handshake_done)),
          max_frame_size_(initialization_args->initial_max_frame_size) {}

    void Start();
    void Shutdown(absl::Status error);

   private:
    grpc_error_handle DoHandshakerNextLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

    grpc_error_handle OnHandshakeNextDoneLocked(
        tsi_result result, const unsigned char* bytes_to_send,
        size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
    void HandshakeFailedLocked(absl::Status error)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
    void Finish(absl::Status status);

    void OnHandshakeDataReceivedFromPeerFn(absl::Status error);
    void OnHandshakeDataSentToPeerFn(absl::Status error);
    void OnHandshakeDataReceivedFromPeerFnScheduler(grpc_error_handle error);
    void OnHandshakeDataSentToPeerFnScheduler(grpc_error_handle error);
    static void OnHandshakeNextDoneGrpcWrapper(
        tsi_result result, void* user_data, const unsigned char* bytes_to_send,
        size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result);
    void OnPeerCheckedFn(grpc_error_handle error);
    void MoveReadBufferIntoHandshakeBuffer();
    grpc_error_handle CheckPeerLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

    const RefCountedPtr<SecurityHandshaker> security_handshaker_;

    Mutex mu_;

    const std::unique_ptr<tsi_handshaker, void (*)(tsi_handshaker*)>
        handshaker_;
    const RefCountedPtr<grpc_security_connector> connector_;

    // State saved while performing the handshake.
    HandshakerArgs* const args_;
    absl::AnyInvocable<void(absl::Status)> on_handshake_done_;
    bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;

    std::vector<unsigned char> handshake_buffer_;
    SliceBuffer outgoing_;
    RefCountedPtr<grpc_auth_context> auth_context_;
    std::unique_ptr<tsi_handshaker_result, void (*)(tsi_handshaker_result*)>
        handshaker_result_{nullptr, tsi_handshaker_result_destroy};
    std::string tsi_handshake_error_;
    grpc_closure* on_peer_checked_ ABSL_GUARDED_BY(mu_) = nullptr;
    size_t max_frame_size_;
  };

  Mutex state_mu_;
  std::unique_ptr<InitializationArgs> initialization_args_
      ABSL_GUARDED_BY(state_mu_);
  RefCountedPtr<Handshake> handshake_ ABSL_GUARDED_BY(state_mu_);
};

SecurityHandshaker::SecurityHandshaker(tsi_handshaker* handshaker,
                                       grpc_security_connector* connector,
                                       const ChannelArgs& args)
    : initialization_args_(
          std::make_unique<InitializationArgs>(handshaker, connector, args)) {}

SecurityHandshaker::~SecurityHandshaker() {}

void SecurityHandshaker::Handshake::Start() {
  MutexLock lock(&mu_);
  MoveReadBufferIntoHandshakeBuffer();
  grpc_error_handle error = DoHandshakerNextLocked();
  if (!error.ok()) {
    HandshakeFailedLocked(error);
  }
}

void SecurityHandshaker::Handshake::MoveReadBufferIntoHandshakeBuffer() {
  size_t bytes_in_read_buffer = args_->read_buffer.Length();
  handshake_buffer_.resize(bytes_in_read_buffer);
  size_t offset = 0;
  while (args_->read_buffer.Count() > 0) {
    Slice slice = args_->read_buffer.TakeFirst();
    memcpy(handshake_buffer_.data() + offset, slice.data(), slice.size());
    offset += slice.size();
  }
}

// If the handshake failed or we're shutting down, clean up and invoke the
// callback with the error.
void SecurityHandshaker::Handshake::HandshakeFailedLocked(absl::Status error) {
  if (error.ok()) {
    // If we were shut down after the handshake succeeded but before an
    // endpoint callback was invoked, we need to generate our own error.
    error = GRPC_ERROR_CREATE("Handshaker shutdown");
  }
  if (!is_shutdown_) {
    tsi_handshaker_shutdown(handshaker_.get());
    // Set shutdown to true so that subsequent calls to
    // security_handshaker_shutdown() do nothing.
    is_shutdown_ = true;
  }
  // Invoke callback.
  Finish(std::move(error));
}

void SecurityHandshaker::Handshake::Finish(absl::Status status) {
  InvokeOnHandshakeDone(args_, std::move(on_handshake_done_),
                        std::move(status));
  RefCountedPtr<Handshake> self;
  MutexLock lock(&security_handshaker_->state_mu_);
  self = std::move(security_handshaker_->handshake_);
}

void SecurityHandshaker::Handshake::OnPeerCheckedFn(grpc_error_handle error) {
  MutexLock lock(&mu_);
  on_peer_checked_ = nullptr;
  if (!error.ok() || is_shutdown_) {
    HandshakeFailedLocked(error);
    return;
  }
  // Get unused bytes.
  const unsigned char* unused_bytes = nullptr;
  size_t unused_bytes_size = 0;
  tsi_result result = tsi_handshaker_result_get_unused_bytes(
      handshaker_result_.get(), &unused_bytes, &unused_bytes_size);
  if (result != TSI_OK) {
    HandshakeFailedLocked(GRPC_ERROR_CREATE(
        absl::StrCat("TSI handshaker result does not provide unused bytes (",
                     tsi_result_to_string(result), ")")));
    return;
  }
  // Check whether we need to wrap the endpoint.
  tsi_frame_protector_type frame_protector_type;
  result = tsi_handshaker_result_get_frame_protector_type(
      handshaker_result_.get(), &frame_protector_type);
  if (result != TSI_OK) {
    HandshakeFailedLocked(GRPC_ERROR_CREATE(
        absl::StrCat("TSI handshaker result does not implement "
                     "get_frame_protector_type (",
                     tsi_result_to_string(result), ")")));
    return;
  }
  tsi_zero_copy_grpc_protector* zero_copy_protector = nullptr;
  tsi_frame_protector* protector = nullptr;
  switch (frame_protector_type) {
    case TSI_FRAME_PROTECTOR_ZERO_COPY:
      ABSL_FALLTHROUGH_INTENDED;
    case TSI_FRAME_PROTECTOR_NORMAL_OR_ZERO_COPY:
      // Create zero-copy frame protector.
      result = tsi_handshaker_result_create_zero_copy_grpc_protector(
          handshaker_result_.get(),
          max_frame_size_ == 0 ? nullptr : &max_frame_size_,
          &zero_copy_protector);
      if (result != TSI_OK) {
        HandshakeFailedLocked(GRPC_ERROR_CREATE(
            absl::StrCat("Zero-copy frame protector creation failed (",
                         tsi_result_to_string(result), ")")));
        return;
      }
      break;
    case TSI_FRAME_PROTECTOR_NORMAL:
      // Create normal frame protector.
      result = tsi_handshaker_result_create_frame_protector(
          handshaker_result_.get(),
          max_frame_size_ == 0 ? nullptr : &max_frame_size_, &protector);
      if (result != TSI_OK) {
        HandshakeFailedLocked(
            GRPC_ERROR_CREATE(absl::StrCat("Frame protector creation failed (",
                                           tsi_result_to_string(result), ")")));
        return;
      }
      break;
    case TSI_FRAME_PROTECTOR_NONE:
      break;
  }
  bool has_frame_protector =
      zero_copy_protector != nullptr || protector != nullptr;
  // If we have a frame protector, create a secure endpoint.
  if (has_frame_protector) {
    if (unused_bytes_size > 0) {
      grpc_slice slice = grpc_slice_from_copied_buffer(
          reinterpret_cast<const char*>(unused_bytes), unused_bytes_size);
      args_->endpoint = grpc_secure_endpoint_create(
          protector, zero_copy_protector, std::move(args_->endpoint), &slice,
          args_->args.ToC().get(), 1);
      CSliceUnref(slice);
    } else {
      args_->endpoint = grpc_secure_endpoint_create(
          protector, zero_copy_protector, std::move(args_->endpoint), nullptr,
          args_->args.ToC().get(), 0);
    }
  } else if (unused_bytes_size > 0) {
    // Not wrapping the endpoint, so just pass along unused bytes.
    args_->read_buffer.Append(Slice::FromCopiedBuffer(
        reinterpret_cast<const char*>(unused_bytes), unused_bytes_size));
  }
  // Done with handshaker result.
  handshaker_result_.reset();
  args_->args = args_->args.SetObject(auth_context_);
  // Add channelz channel args only if frame protector is created.
  if (has_frame_protector) {
    args_->args = args_->args.SetObject(
        MakeChannelzSecurityFromAuthContext(auth_context_.get()));
  }
  // Set shutdown to true so that subsequent calls to
  // security_handshaker_shutdown() do nothing.
  is_shutdown_ = true;
  // Invoke callback.
  Finish(absl::OkStatus());
}

grpc_error_handle SecurityHandshaker::Handshake::CheckPeerLocked() {
  tsi_peer peer;
  tsi_result result =
      tsi_handshaker_result_extract_peer(handshaker_result_.get(), &peer);
  if (result != TSI_OK) {
    return GRPC_ERROR_CREATE(absl::StrCat("Peer extraction failed (",
                                          tsi_result_to_string(result), ")"));
  }
  on_peer_checked_ = NewClosure([self = Ref()](absl::Status status) {
    self->OnPeerCheckedFn(std::move(status));
  });
  connector_->check_peer(peer, args_->endpoint.get(), args_->args,
                         &auth_context_, on_peer_checked_);
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context_.get(), GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (!prop ||
      !strcmp(tsi_security_level_to_string(TSI_SECURITY_NONE), prop->value)) {
    global_stats().IncrementInsecureConnectionsCreated();
  }
  return absl::OkStatus();
}

grpc_error_handle SecurityHandshaker::Handshake::OnHandshakeNextDoneLocked(
    tsi_result result, const unsigned char* bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result) {
  grpc_error_handle error;
  // Handshaker was shutdown.
  if (is_shutdown_) {
    tsi_handshaker_result_destroy(handshaker_result);
    return GRPC_ERROR_CREATE("Handshaker shutdown");
  }
  // Read more if we need to.
  if (result == TSI_INCOMPLETE_DATA) {
    CHECK_EQ(bytes_to_send_size, 0u);
    grpc_endpoint_read(
        args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
        NewClosure([self = Ref()](absl::Status status) {
          self->OnHandshakeDataReceivedFromPeerFnScheduler(std::move(status));
        }),
        /*urgent=*/true, /*min_progress_size=*/1);
    return error;
  }
  if (result != TSI_OK) {
    auto* security_connector = args_->args.GetObject<grpc_security_connector>();
    absl::string_view connector_type = "<unknown>";
    if (security_connector != nullptr) {
      connector_type = security_connector->type().name();
    }
    // TODO(roth): Get a better signal from the TSI layer as to what
    // status code we should use here.
    return GRPC_ERROR_CREATE(absl::StrCat(
        connector_type, " handshake failed (", tsi_result_to_string(result),
        ")", (tsi_handshake_error_.empty() ? "" : ": "), tsi_handshake_error_));
  }
  // Update handshaker result.
  if (handshaker_result != nullptr) {
    CHECK_EQ(handshaker_result_.get(), nullptr);
    handshaker_result_.reset(handshaker_result);
  }
  if (bytes_to_send_size > 0) {
    // Send data to peer, if needed.
    outgoing_.Clear();
    outgoing_.Append(Slice::FromCopiedBuffer(
        reinterpret_cast<const char*>(bytes_to_send), bytes_to_send_size));
    grpc_endpoint_write(
        args_->endpoint.get(), outgoing_.c_slice_buffer(),
        NewClosure([self = Ref()](absl::Status status) {
          self->OnHandshakeDataSentToPeerFnScheduler(std::move(status));
        }),
        nullptr, /*max_frame_size=*/INT_MAX);
  } else if (handshaker_result == nullptr) {
    // There is nothing to send, but need to read from peer.
    grpc_endpoint_read(
        args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
        NewClosure([self = Ref()](absl::Status status) {
          self->OnHandshakeDataReceivedFromPeerFnScheduler(std::move(status));
        }),
        /*urgent=*/true, /*min_progress_size=*/1);
  } else {
    // Handshake has finished, check peer and so on.
    error = CheckPeerLocked();
  }
  return error;
}

void SecurityHandshaker::Handshake::OnHandshakeNextDoneGrpcWrapper(
    tsi_result result, void* user_data, const unsigned char* bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result) {
  RefCountedPtr<Handshake> h(static_cast<Handshake*>(user_data));
  MutexLock lock(&h->mu_);
  grpc_error_handle error = h->OnHandshakeNextDoneLocked(
      result, bytes_to_send, bytes_to_send_size, handshaker_result);
  if (!error.ok()) {
    h->HandshakeFailedLocked(std::move(error));
  }
}

grpc_error_handle SecurityHandshaker::Handshake::DoHandshakerNextLocked() {
  // Invoke TSI handshaker.
  const unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  tsi_handshaker_result* hs_result = nullptr;
  auto self = Ref();
  tsi_result result = tsi_handshaker_next(
      handshaker_.get(), handshake_buffer_.data(), handshake_buffer_.size(),
      &bytes_to_send, &bytes_to_send_size, &hs_result,
      &OnHandshakeNextDoneGrpcWrapper, self.get(), &tsi_handshake_error_);
  if (result == TSI_ASYNC) {
    // Handshaker operating asynchronously. Callback will be invoked in a TSI
    // thread. We no longer own the ref held in self.
    self.release();
    return absl::OkStatus();
  }
  // Handshaker returned synchronously. Invoke callback directly in
  // this thread with our existing exec_ctx.
  return OnHandshakeNextDoneLocked(result, bytes_to_send, bytes_to_send_size,
                                   hs_result);
}

// This callback might be run inline while we are still holding on to the mutex,
// so run OnHandshakeDataReceivedFromPeerFn asynchronously to avoid a deadlock.
// TODO(roth): This will no longer be necessary once we migrate to the
// EventEngine endpoint API.
void SecurityHandshaker::Handshake::OnHandshakeDataReceivedFromPeerFnScheduler(
    grpc_error_handle error) {
  args_->event_engine->Run([self = Ref(), error = std::move(error)]() mutable {
    ApplicationCallbackExecCtx callback_exec_ctx;
    ExecCtx exec_ctx;
    self->OnHandshakeDataReceivedFromPeerFn(std::move(error));
    // Avoid destruction outside of an ExecCtx (since this is non-cancelable).
    self.reset();
  });
}

void SecurityHandshaker::Handshake::OnHandshakeDataReceivedFromPeerFn(
    absl::Status error) {
  MutexLock lock(&mu_);
  if (!error.ok() || is_shutdown_) {
    HandshakeFailedLocked(
        GRPC_ERROR_CREATE_REFERENCING("Handshake read failed", &error, 1));
    return;
  }
  // Copy all slices received.
  MoveReadBufferIntoHandshakeBuffer();
  // Call TSI handshaker.
  error = DoHandshakerNextLocked();
  if (!error.ok()) {
    HandshakeFailedLocked(std::move(error));
  }
}

// This callback might be run inline while we are still holding on to the mutex,
// so run OnHandshakeDataSentToPeerFn asynchronously to avoid a deadlock.
// TODO(roth): This will no longer be necessary once we migrate to the
// EventEngine endpoint API.
void SecurityHandshaker::Handshake::OnHandshakeDataSentToPeerFnScheduler(
    grpc_error_handle error) {
  args_->event_engine->Run([self = Ref(), error = std::move(error)]() mutable {
    ApplicationCallbackExecCtx callback_exec_ctx;
    ExecCtx exec_ctx;
    self->OnHandshakeDataSentToPeerFn(std::move(error));
    // Avoid destruction outside of an ExecCtx (since this is non-cancelable).
    self.reset();
  });
}

void SecurityHandshaker::Handshake::OnHandshakeDataSentToPeerFn(
    absl::Status error) {
  MutexLock lock(&mu_);
  if (!error.ok() || is_shutdown_) {
    HandshakeFailedLocked(
        GRPC_ERROR_CREATE_REFERENCING("Handshake write failed", &error, 1));
    return;
  }
  // We may be done.
  if (handshaker_result_ == nullptr) {
    grpc_endpoint_read(
        args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
        NewClosure([self = Ref()](absl::Status status) {
          self->OnHandshakeDataReceivedFromPeerFnScheduler(std::move(status));
        }),
        /*urgent=*/true, /*min_progress_size=*/1);
  } else {
    error = CheckPeerLocked();
    if (!error.ok()) {
      HandshakeFailedLocked(error);
      return;
    }
  }
}

void SecurityHandshaker::Handshake::Shutdown(grpc_error_handle error) {
  MutexLock lock(&mu_);
  if (!is_shutdown_) {
    is_shutdown_ = true;
    connector_->cancel_check_peer(on_peer_checked_, std::move(error));
    tsi_handshaker_shutdown(handshaker_.get());
    args_->endpoint.reset();
  }
}

//
// public handshaker API
//

void SecurityHandshaker::Shutdown(grpc_error_handle error) {
  ReleasableMutexLock lock(&state_mu_);
  RefCountedPtr<Handshake> handshake = std::move(handshake_);
  if (handshake != nullptr) {
    lock.Release();
    handshake->Shutdown(std::move(error));
  }
}

void SecurityHandshaker::DoHandshake(
    HandshakerArgs* args,
    absl::AnyInvocable<void(absl::Status)> on_handshake_done) {
  RefCountedPtr<Handshake> handshake;
  {
    MutexLock lock(&state_mu_);
    handshake = MakeRefCounted<Handshake>(RefAsSubclass<SecurityHandshaker>(),
                                          std::move(initialization_args_), args,
                                          std::move(on_handshake_done));
    handshake_ = handshake;
  }
  handshake->Start();
}

//
// FailHandshaker
//

class FailHandshaker : public Handshaker {
 public:
  explicit FailHandshaker(absl::Status status) : status_(std::move(status)) {}
  absl::string_view name() const override { return "security_fail"; }
  void DoHandshake(
      HandshakerArgs* args,
      absl::AnyInvocable<void(absl::Status)> on_handshake_done) override {
    InvokeOnHandshakeDone(args, std::move(on_handshake_done), status_);
  }
  void Shutdown(absl::Status /*error*/) override {}

 private:
  ~FailHandshaker() override = default;
  absl::Status status_;
};

//
// handshaker factories
//

class ClientSecurityHandshakerFactory : public HandshakerFactory {
 public:
  void AddHandshakers(const ChannelArgs& args,
                      grpc_pollset_set* interested_parties,
                      HandshakeManager* handshake_mgr) override {
    auto* security_connector =
        args.GetObject<grpc_channel_security_connector>();
    if (security_connector) {
      security_connector->add_handshakers(args, interested_parties,
                                          handshake_mgr);
    }
  }
  HandshakerPriority Priority() override {
    return HandshakerPriority::kSecurityHandshakers;
  }
  ~ClientSecurityHandshakerFactory() override = default;
};

class ServerSecurityHandshakerFactory : public HandshakerFactory {
 public:
  void AddHandshakers(const ChannelArgs& args,
                      grpc_pollset_set* interested_parties,
                      HandshakeManager* handshake_mgr) override {
    auto* security_connector = args.GetObject<grpc_server_security_connector>();
    if (security_connector) {
      security_connector->add_handshakers(args, interested_parties,
                                          handshake_mgr);
    }
  }
  HandshakerPriority Priority() override {
    return HandshakerPriority::kSecurityHandshakers;
  }
  ~ServerSecurityHandshakerFactory() override = default;
};

}  // namespace

//
// exported functions
//

RefCountedPtr<Handshaker> SecurityHandshakerCreate(
    absl::StatusOr<tsi_handshaker*> handshaker,
    grpc_security_connector* connector, const ChannelArgs& args) {
  // If no TSI handshaker was created, return a handshaker that always fails.
  // Otherwise, return a real security handshaker.
  if (!handshaker.ok()) {
    return MakeRefCounted<FailHandshaker>(
        absl::Status(handshaker.status().code(),
                     absl::StrCat("Failed to create security handshaker: ",
                                  handshaker.status().message())));
  } else if (*handshaker == nullptr) {
    // TODO(gtcooke94) Once all TSI impls are updated to pass StatusOr<> instead
    // of null, we should change this to use absl::InternalError().
    return MakeRefCounted<FailHandshaker>(
        absl::UnknownError("Failed to create security handshaker."));
  } else if (!IsSecurityHandshakerEarlyReleaseEnabled()) {
    return MakeRefCounted<LegacySecurityHandshaker>(*handshaker, connector,
                                                    args);
  } else {
    return MakeRefCounted<SecurityHandshaker>(*handshaker, connector, args);
  }
}

void SecurityRegisterHandshakerFactories(CoreConfiguration::Builder* builder) {
  builder->handshaker_registry()->RegisterHandshakerFactory(
      HANDSHAKER_CLIENT, std::make_unique<ClientSecurityHandshakerFactory>());
  builder->handshaker_registry()->RegisterHandshakerFactory(
      HANDSHAKER_SERVER, std::make_unique<ServerSecurityHandshakerFactory>());
}

}  // namespace grpc_core
