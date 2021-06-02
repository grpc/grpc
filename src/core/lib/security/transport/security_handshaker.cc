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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/transport/security_handshaker.h"

#include <stdbool.h>
#include <string.h>
#include <limits>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/transport/secure_endpoint.h"
#include "src/core/lib/security/transport/tsi_error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/transport_security_grpc.h"

#define GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE 256

namespace grpc_core {

namespace {

class SecurityHandshaker : public Handshaker {
 public:
  SecurityHandshaker(tsi_handshaker* handshaker,
                     grpc_security_connector* connector,
                     const grpc_channel_args* args);
  ~SecurityHandshaker() override;
  void Shutdown(grpc_error_handle why) override;
  void DoHandshake(grpc_tcp_server_acceptor* acceptor,
                   grpc_closure* on_handshake_done,
                   HandshakerArgs* args) override;
  const char* name() const override { return "security"; }

 private:
  grpc_error_handle DoHandshakerNextLocked(const unsigned char* bytes_received,
                                           size_t bytes_received_size);

  grpc_error_handle OnHandshakeNextDoneLocked(
      tsi_result result, const unsigned char* bytes_to_send,
      size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result);
  void HandshakeFailedLocked(grpc_error_handle error);
  void CleanupArgsForFailureLocked();

  static void OnHandshakeDataReceivedFromPeerFn(void* arg,
                                                grpc_error_handle error);
  static void OnHandshakeDataSentToPeerFn(void* arg, grpc_error_handle error);
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
  // Endpoint and read buffer to destroy after a shutdown.
  grpc_endpoint* endpoint_to_destroy_ = nullptr;
  grpc_slice_buffer* read_buffer_to_destroy_ = nullptr;

  // State saved while performing the handshake.
  HandshakerArgs* args_ = nullptr;
  grpc_closure* on_handshake_done_ = nullptr;

  size_t handshake_buffer_size_;
  unsigned char* handshake_buffer_;
  grpc_slice_buffer outgoing_;
  grpc_closure on_handshake_data_sent_to_peer_;
  grpc_closure on_handshake_data_received_from_peer_;
  grpc_closure on_peer_checked_;
  RefCountedPtr<grpc_auth_context> auth_context_;
  tsi_handshaker_result* handshaker_result_ = nullptr;
  size_t max_frame_size_ = 0;
};

SecurityHandshaker::SecurityHandshaker(tsi_handshaker* handshaker,
                                       grpc_security_connector* connector,
                                       const grpc_channel_args* args)
    : handshaker_(handshaker),
      connector_(connector->Ref(DEBUG_LOCATION, "handshake")),
      handshake_buffer_size_(GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE),
      handshake_buffer_(
          static_cast<uint8_t*>(gpr_malloc(handshake_buffer_size_))) {
  const grpc_arg* arg =
      grpc_channel_args_find(args, GRPC_ARG_TSI_MAX_FRAME_SIZE);
  if (arg != nullptr && arg->type == GRPC_ARG_INTEGER) {
    max_frame_size_ = grpc_channel_arg_get_integer(
        arg, {0, 0, std::numeric_limits<int>::max()});
  }
  grpc_slice_buffer_init(&outgoing_);
  GRPC_CLOSURE_INIT(&on_peer_checked_, &SecurityHandshaker::OnPeerCheckedFn,
                    this, grpc_schedule_on_exec_ctx);
}

SecurityHandshaker::~SecurityHandshaker() {
  tsi_handshaker_destroy(handshaker_);
  tsi_handshaker_result_destroy(handshaker_result_);
  if (endpoint_to_destroy_ != nullptr) {
    grpc_endpoint_destroy(endpoint_to_destroy_);
  }
  if (read_buffer_to_destroy_ != nullptr) {
    grpc_slice_buffer_destroy_internal(read_buffer_to_destroy_);
    gpr_free(read_buffer_to_destroy_);
  }
  gpr_free(handshake_buffer_);
  grpc_slice_buffer_destroy_internal(&outgoing_);
  auth_context_.reset(DEBUG_LOCATION, "handshake");
  connector_.reset(DEBUG_LOCATION, "handshake");
}

size_t SecurityHandshaker::MoveReadBufferIntoHandshakeBuffer() {
  size_t bytes_in_read_buffer = args_->read_buffer->length;
  if (handshake_buffer_size_ < bytes_in_read_buffer) {
    handshake_buffer_ = static_cast<uint8_t*>(
        gpr_realloc(handshake_buffer_, bytes_in_read_buffer));
    handshake_buffer_size_ = bytes_in_read_buffer;
  }
  size_t offset = 0;
  while (args_->read_buffer->count > 0) {
    grpc_slice* next_slice = grpc_slice_buffer_peek_first(args_->read_buffer);
    memcpy(handshake_buffer_ + offset, GRPC_SLICE_START_PTR(*next_slice),
           GRPC_SLICE_LENGTH(*next_slice));
    offset += GRPC_SLICE_LENGTH(*next_slice);
    grpc_slice_buffer_remove_first(args_->read_buffer);
  }
  return bytes_in_read_buffer;
}

// Set args_ fields to NULL, saving the endpoint and read buffer for
// later destruction.
void SecurityHandshaker::CleanupArgsForFailureLocked() {
  endpoint_to_destroy_ = args_->endpoint;
  args_->endpoint = nullptr;
  read_buffer_to_destroy_ = args_->read_buffer;
  args_->read_buffer = nullptr;
  grpc_channel_args_destroy(args_->args);
  args_->args = nullptr;
}

// If the handshake failed or we're shutting down, clean up and invoke the
// callback with the error.
void SecurityHandshaker::HandshakeFailedLocked(grpc_error_handle error) {
  if (error == GRPC_ERROR_NONE) {
    // If we were shut down after the handshake succeeded but before an
    // endpoint callback was invoked, we need to generate our own error.
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshaker shutdown");
  }
  gpr_log(GPR_DEBUG, "Security handshake failed: %s",
          grpc_error_std_string(error).c_str());
  if (!is_shutdown_) {
    tsi_handshaker_shutdown(handshaker_);
    // TODO(ctiller): It is currently necessary to shutdown endpoints
    // before destroying them, even if we know that there are no
    // pending read/write callbacks.  This should be fixed, at which
    // point this can be removed.
    grpc_endpoint_shutdown(args_->endpoint, GRPC_ERROR_REF(error));
    // Not shutting down, so the write failed.  Clean up before
    // invoking the callback.
    CleanupArgsForFailureLocked();
    // Set shutdown to true so that subsequent calls to
    // security_handshaker_shutdown() do nothing.
    is_shutdown_ = true;
  }
  // Invoke callback.
  ExecCtx::Run(DEBUG_LOCATION, on_handshake_done_, error);
}

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

}  // namespace

void SecurityHandshaker::OnPeerCheckedInner(grpc_error_handle error) {
  MutexLock lock(&mu_);
  if (error != GRPC_ERROR_NONE || is_shutdown_) {
    HandshakeFailedLocked(error);
    return;
  }
  // Create zero-copy frame protector, if implemented.
  tsi_zero_copy_grpc_protector* zero_copy_protector = nullptr;
  tsi_result result = tsi_handshaker_result_create_zero_copy_grpc_protector(
      handshaker_result_, max_frame_size_ == 0 ? nullptr : &max_frame_size_,
      &zero_copy_protector);
  if (result != TSI_OK && result != TSI_UNIMPLEMENTED) {
    error = grpc_set_tsi_error_result(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Zero-copy frame protector creation failed"),
        result);
    HandshakeFailedLocked(error);
    return;
  }
  // Create frame protector if zero-copy frame protector is NULL.
  tsi_frame_protector* protector = nullptr;
  if (zero_copy_protector == nullptr) {
    result = tsi_handshaker_result_create_frame_protector(
        handshaker_result_, max_frame_size_ == 0 ? nullptr : &max_frame_size_,
        &protector);
    if (result != TSI_OK) {
      error = grpc_set_tsi_error_result(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                            "Frame protector creation failed"),
                                        result);
      HandshakeFailedLocked(error);
      return;
    }
  }
  // Get unused bytes.
  const unsigned char* unused_bytes = nullptr;
  size_t unused_bytes_size = 0;
  result = tsi_handshaker_result_get_unused_bytes(
      handshaker_result_, &unused_bytes, &unused_bytes_size);
  // Create secure endpoint.
  if (unused_bytes_size > 0) {
    grpc_slice slice = grpc_slice_from_copied_buffer(
        reinterpret_cast<const char*>(unused_bytes), unused_bytes_size);
    args_->endpoint = grpc_secure_endpoint_create(
        protector, zero_copy_protector, args_->endpoint, &slice, 1);
    grpc_slice_unref_internal(slice);
  } else {
    args_->endpoint = grpc_secure_endpoint_create(
        protector, zero_copy_protector, args_->endpoint, nullptr, 0);
  }
  tsi_handshaker_result_destroy(handshaker_result_);
  handshaker_result_ = nullptr;
  // Add auth context to channel args.
  absl::InlinedVector<grpc_arg, 2> args_to_add;
  args_to_add.push_back(grpc_auth_context_to_arg(auth_context_.get()));
  auto security = MakeChannelzSecurityFromAuthContext(auth_context_.get());
  args_to_add.push_back(security->MakeChannelArg());
  grpc_channel_args* tmp_args = args_->args;
  args_->args = grpc_channel_args_copy_and_add(tmp_args, args_to_add.data(),
                                               args_to_add.size());
  grpc_channel_args_destroy(tmp_args);
  // Invoke callback.
  ExecCtx::Run(DEBUG_LOCATION, on_handshake_done_, GRPC_ERROR_NONE);
  // Set shutdown to true so that subsequent calls to
  // security_handshaker_shutdown() do nothing.
  is_shutdown_ = true;
}

void SecurityHandshaker::OnPeerCheckedFn(void* arg, grpc_error_handle error) {
  RefCountedPtr<SecurityHandshaker>(static_cast<SecurityHandshaker*>(arg))
      ->OnPeerCheckedInner(GRPC_ERROR_REF(error));
}

grpc_error_handle SecurityHandshaker::CheckPeerLocked() {
  tsi_peer peer;
  tsi_result result =
      tsi_handshaker_result_extract_peer(handshaker_result_, &peer);
  if (result != TSI_OK) {
    return grpc_set_tsi_error_result(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Peer extraction failed"), result);
  }
  connector_->check_peer(peer, args_->endpoint, &auth_context_,
                         &on_peer_checked_);
  return GRPC_ERROR_NONE;
}

grpc_error_handle SecurityHandshaker::OnHandshakeNextDoneLocked(
    tsi_result result, const unsigned char* bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  // Handshaker was shutdown.
  if (is_shutdown_) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshaker shutdown");
  }
  // Read more if we need to.
  if (result == TSI_INCOMPLETE_DATA) {
    GPR_ASSERT(bytes_to_send_size == 0);
    grpc_endpoint_read(
        args_->endpoint, args_->read_buffer,
        GRPC_CLOSURE_INIT(
            &on_handshake_data_received_from_peer_,
            &SecurityHandshaker::OnHandshakeDataReceivedFromPeerFnScheduler,
            this, grpc_schedule_on_exec_ctx),
        /*urgent=*/true);
    return error;
  }
  if (result != TSI_OK) {
    return grpc_set_tsi_error_result(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshake failed"), result);
  }
  // Update handshaker result.
  if (handshaker_result != nullptr) {
    GPR_ASSERT(handshaker_result_ == nullptr);
    handshaker_result_ = handshaker_result;
  }
  if (bytes_to_send_size > 0) {
    // Send data to peer, if needed.
    grpc_slice to_send = grpc_slice_from_copied_buffer(
        reinterpret_cast<const char*>(bytes_to_send), bytes_to_send_size);
    grpc_slice_buffer_reset_and_unref_internal(&outgoing_);
    grpc_slice_buffer_add(&outgoing_, to_send);
    grpc_endpoint_write(
        args_->endpoint, &outgoing_,
        GRPC_CLOSURE_INIT(
            &on_handshake_data_sent_to_peer_,
            &SecurityHandshaker::OnHandshakeDataSentToPeerFnScheduler, this,
            grpc_schedule_on_exec_ctx),
        nullptr);
  } else if (handshaker_result == nullptr) {
    // There is nothing to send, but need to read from peer.
    grpc_endpoint_read(
        args_->endpoint, args_->read_buffer,
        GRPC_CLOSURE_INIT(
            &on_handshake_data_received_from_peer_,
            &SecurityHandshaker::OnHandshakeDataReceivedFromPeerFnScheduler,
            this, grpc_schedule_on_exec_ctx),
        /*urgent=*/true);
  } else {
    // Handshake has finished, check peer and so on.
    error = CheckPeerLocked();
  }
  return error;
}

void SecurityHandshaker::OnHandshakeNextDoneGrpcWrapper(
    tsi_result result, void* user_data, const unsigned char* bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result) {
  RefCountedPtr<SecurityHandshaker> h(
      static_cast<SecurityHandshaker*>(user_data));
  MutexLock lock(&h->mu_);
  grpc_error_handle error = h->OnHandshakeNextDoneLocked(
      result, bytes_to_send, bytes_to_send_size, handshaker_result);
  if (error != GRPC_ERROR_NONE) {
    h->HandshakeFailedLocked(error);
  } else {
    h.release();  // Avoid unref
  }
}

grpc_error_handle SecurityHandshaker::DoHandshakerNextLocked(
    const unsigned char* bytes_received, size_t bytes_received_size) {
  // Invoke TSI handshaker.
  const unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  tsi_handshaker_result* hs_result = nullptr;
  tsi_result result = tsi_handshaker_next(
      handshaker_, bytes_received, bytes_received_size, &bytes_to_send,
      &bytes_to_send_size, &hs_result, &OnHandshakeNextDoneGrpcWrapper, this);
  if (result == TSI_ASYNC) {
    // Handshaker operating asynchronously. Nothing else to do here;
    // callback will be invoked in a TSI thread.
    return GRPC_ERROR_NONE;
  }
  // Handshaker returned synchronously. Invoke callback directly in
  // this thread with our existing exec_ctx.
  return OnHandshakeNextDoneLocked(result, bytes_to_send, bytes_to_send_size,
                                   hs_result);
}

// This callback might be run inline while we are still holding on to the mutex,
// so schedule OnHandshakeDataReceivedFromPeerFn on ExecCtx to avoid a deadlock.
void SecurityHandshaker::OnHandshakeDataReceivedFromPeerFnScheduler(
    void* arg, grpc_error_handle error) {
  SecurityHandshaker* h = static_cast<SecurityHandshaker*>(arg);
  grpc_core::ExecCtx::Run(
      DEBUG_LOCATION,
      GRPC_CLOSURE_INIT(&h->on_handshake_data_received_from_peer_,
                        &SecurityHandshaker::OnHandshakeDataReceivedFromPeerFn,
                        h, grpc_schedule_on_exec_ctx),
      GRPC_ERROR_REF(error));
}

void SecurityHandshaker::OnHandshakeDataReceivedFromPeerFn(
    void* arg, grpc_error_handle error) {
  RefCountedPtr<SecurityHandshaker> h(static_cast<SecurityHandshaker*>(arg));
  MutexLock lock(&h->mu_);
  if (error != GRPC_ERROR_NONE || h->is_shutdown_) {
    h->HandshakeFailedLocked(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Handshake read failed", &error, 1));
    return;
  }
  // Copy all slices received.
  size_t bytes_received_size = h->MoveReadBufferIntoHandshakeBuffer();
  // Call TSI handshaker.
  error = h->DoHandshakerNextLocked(h->handshake_buffer_, bytes_received_size);

  if (error != GRPC_ERROR_NONE) {
    h->HandshakeFailedLocked(error);
  } else {
    h.release();  // Avoid unref
  }
}

// This callback might be run inline while we are still holding on to the mutex,
// so schedule OnHandshakeDataSentToPeerFn on ExecCtx to avoid a deadlock.
void SecurityHandshaker::OnHandshakeDataSentToPeerFnScheduler(
    void* arg, grpc_error_handle error) {
  SecurityHandshaker* h = static_cast<SecurityHandshaker*>(arg);
  grpc_core::ExecCtx::Run(
      DEBUG_LOCATION,
      GRPC_CLOSURE_INIT(&h->on_handshake_data_sent_to_peer_,
                        &SecurityHandshaker::OnHandshakeDataSentToPeerFn, h,
                        grpc_schedule_on_exec_ctx),
      GRPC_ERROR_REF(error));
}

void SecurityHandshaker::OnHandshakeDataSentToPeerFn(void* arg,
                                                     grpc_error_handle error) {
  RefCountedPtr<SecurityHandshaker> h(static_cast<SecurityHandshaker*>(arg));
  MutexLock lock(&h->mu_);
  if (error != GRPC_ERROR_NONE || h->is_shutdown_) {
    h->HandshakeFailedLocked(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Handshake write failed", &error, 1));
    return;
  }
  // We may be done.
  if (h->handshaker_result_ == nullptr) {
    grpc_endpoint_read(
        h->args_->endpoint, h->args_->read_buffer,
        GRPC_CLOSURE_INIT(
            &h->on_handshake_data_received_from_peer_,
            &SecurityHandshaker::OnHandshakeDataReceivedFromPeerFnScheduler,
            h.get(), grpc_schedule_on_exec_ctx),
        /*urgent=*/true);
  } else {
    error = h->CheckPeerLocked();
    if (error != GRPC_ERROR_NONE) {
      h->HandshakeFailedLocked(error);
      return;
    }
  }
  h.release();  // Avoid unref
}

//
// public handshaker API
//

void SecurityHandshaker::Shutdown(grpc_error_handle why) {
  MutexLock lock(&mu_);
  if (!is_shutdown_) {
    is_shutdown_ = true;
    connector_->cancel_check_peer(&on_peer_checked_, GRPC_ERROR_REF(why));
    tsi_handshaker_shutdown(handshaker_);
    grpc_endpoint_shutdown(args_->endpoint, GRPC_ERROR_REF(why));
    CleanupArgsForFailureLocked();
  }
  GRPC_ERROR_UNREF(why);
}

void SecurityHandshaker::DoHandshake(grpc_tcp_server_acceptor* /*acceptor*/,
                                     grpc_closure* on_handshake_done,
                                     HandshakerArgs* args) {
  auto ref = Ref();
  MutexLock lock(&mu_);
  args_ = args;
  on_handshake_done_ = on_handshake_done;
  size_t bytes_received_size = MoveReadBufferIntoHandshakeBuffer();
  grpc_error_handle error =
      DoHandshakerNextLocked(handshake_buffer_, bytes_received_size);
  if (error != GRPC_ERROR_NONE) {
    HandshakeFailedLocked(error);
  } else {
    ref.release();  // Avoid unref
  }
}

//
// FailHandshaker
//

class FailHandshaker : public Handshaker {
 public:
  const char* name() const override { return "security_fail"; }
  void Shutdown(grpc_error_handle why) override { GRPC_ERROR_UNREF(why); }
  void DoHandshake(grpc_tcp_server_acceptor* /*acceptor*/,
                   grpc_closure* on_handshake_done,
                   HandshakerArgs* /*args*/) override {
    ExecCtx::Run(DEBUG_LOCATION, on_handshake_done,
                 GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                     "Failed to create security handshaker"));
  }

 private:
  ~FailHandshaker() override = default;
};

//
// handshaker factories
//

class ClientSecurityHandshakerFactory : public HandshakerFactory {
 public:
  void AddHandshakers(const grpc_channel_args* args,
                      grpc_pollset_set* interested_parties,
                      HandshakeManager* handshake_mgr) override {
    auto* security_connector =
        reinterpret_cast<grpc_channel_security_connector*>(
            grpc_security_connector_find_in_args(args));
    if (security_connector) {
      security_connector->add_handshakers(args, interested_parties,
                                          handshake_mgr);
    }
  }
  ~ClientSecurityHandshakerFactory() override = default;
};

class ServerSecurityHandshakerFactory : public HandshakerFactory {
 public:
  void AddHandshakers(const grpc_channel_args* args,
                      grpc_pollset_set* interested_parties,
                      HandshakeManager* handshake_mgr) override {
    auto* security_connector =
        reinterpret_cast<grpc_server_security_connector*>(
            grpc_security_connector_find_in_args(args));
    if (security_connector) {
      security_connector->add_handshakers(args, interested_parties,
                                          handshake_mgr);
    }
  }
  ~ServerSecurityHandshakerFactory() override = default;
};

}  // namespace

//
// exported functions
//

RefCountedPtr<Handshaker> SecurityHandshakerCreate(
    tsi_handshaker* handshaker, grpc_security_connector* connector,
    const grpc_channel_args* args) {
  // If no TSI handshaker was created, return a handshaker that always fails.
  // Otherwise, return a real security handshaker.
  if (handshaker == nullptr) {
    return MakeRefCounted<FailHandshaker>();
  } else {
    return MakeRefCounted<SecurityHandshaker>(handshaker, connector, args);
  }
}

void SecurityRegisterHandshakerFactories() {
  HandshakerRegistry::RegisterHandshakerFactory(
      false /* at_start */, HANDSHAKER_CLIENT,
      absl::make_unique<ClientSecurityHandshakerFactory>());
  HandshakerRegistry::RegisterHandshakerFactory(
      false /* at_start */, HANDSHAKER_SERVER,
      absl::make_unique<ServerSecurityHandshakerFactory>());
}

}  // namespace grpc_core

grpc_handshaker* grpc_security_handshaker_create(
    tsi_handshaker* handshaker, grpc_security_connector* connector,
    const grpc_channel_args* args) {
  return SecurityHandshakerCreate(handshaker, connector, args).release();
}
