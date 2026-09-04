//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/handshaker/http_connect/https_proxy_tls_handshaker.h"

#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <limits.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/tls/ssl_utils.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/handshaker_factory.h"
#include "src/core/handshaker/handshaker_registry.h"
#include "src/core/handshaker/security/secure_endpoint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#define GRPC_HTTPS_PROXY_HANDSHAKE_BUFFER_SIZE 256

namespace grpc_core {

namespace {

class HttpsProxyTlsHandshaker : public Handshaker {
 public:
  HttpsProxyTlsHandshaker(tsi_ssl_client_handshaker_factory* factory,
                          std::string proxy_server_name);
  ~HttpsProxyTlsHandshaker() override;

  absl::string_view name() const override { return "https_proxy_tls"; }
  void DoHandshake(
      HandshakerArgs* args,
      absl::AnyInvocable<void(absl::Status)> on_handshake_done) override;
  void Shutdown(absl::Status error) override;

 private:
  grpc_error_handle DoHandshakerNextLocked(const unsigned char* bytes_received,
                                           size_t bytes_received_size)
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

  size_t MoveReadBufferIntoHandshakeBuffer();
  grpc_error_handle ProcessHandshakeResult()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // State set at creation time.
  tsi_ssl_client_handshaker_factory* handshaker_factory_;
  std::string proxy_server_name_;
  tsi_handshaker* tsi_handshaker_ = nullptr;

  Mutex mu_;

  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;

  // State saved while performing the handshake.
  HandshakerArgs* args_ = nullptr;
  absl::AnyInvocable<void(absl::Status)> on_handshake_done_;

  size_t handshake_buffer_size_;
  unsigned char* handshake_buffer_;
  SliceBuffer outgoing_;
  tsi_handshaker_result* handshaker_result_ ABSL_GUARDED_BY(mu_) = nullptr;
  std::string tsi_handshake_error_;
};

HttpsProxyTlsHandshaker::HttpsProxyTlsHandshaker(
    tsi_ssl_client_handshaker_factory* factory, std::string proxy_server_name)
    : handshaker_factory_(factory),
      proxy_server_name_(std::move(proxy_server_name)),
      handshake_buffer_size_(GRPC_HTTPS_PROXY_HANDSHAKE_BUFFER_SIZE),
      handshake_buffer_(
          static_cast<uint8_t*>(gpr_malloc(handshake_buffer_size_))) {
  // Keep a ref to the factory
  if (handshaker_factory_ != nullptr) {
    tsi_ssl_client_handshaker_factory_ref(handshaker_factory_);
  }
}

HttpsProxyTlsHandshaker::~HttpsProxyTlsHandshaker() {
  if (tsi_handshaker_ != nullptr) {
    tsi_handshaker_destroy(tsi_handshaker_);
  }
  if (handshaker_result_ != nullptr) {
    tsi_handshaker_result_destroy(handshaker_result_);
  }
  if (handshaker_factory_ != nullptr) {
    tsi_ssl_client_handshaker_factory_unref(handshaker_factory_);
  }
  gpr_free(handshake_buffer_);
}

size_t HttpsProxyTlsHandshaker::MoveReadBufferIntoHandshakeBuffer() {
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

void HttpsProxyTlsHandshaker::HandshakeFailedLocked(absl::Status error) {
  if (error.ok()) {
    error = GRPC_ERROR_CREATE("HTTPS proxy TLS handshaker shutdown");
  }
  if (!is_shutdown_) {
    if (tsi_handshaker_ != nullptr) {
      tsi_handshaker_shutdown(tsi_handshaker_);
    }
    is_shutdown_ = true;
  }
  Finish(std::move(error));
}

void HttpsProxyTlsHandshaker::Finish(absl::Status status) {
  InvokeOnHandshakeDone(args_, std::move(on_handshake_done_),
                        std::move(status));
}

grpc_error_handle HttpsProxyTlsHandshaker::ProcessHandshakeResult() {
  // Get unused bytes from the handshake.
  const unsigned char* unused_bytes = nullptr;
  size_t unused_bytes_size = 0;
  tsi_result result = tsi_handshaker_result_get_unused_bytes(
      handshaker_result_, &unused_bytes, &unused_bytes_size);
  if (result != TSI_OK) {
    return GRPC_ERROR_CREATE(
        absl::StrCat("HTTPS proxy TLS handshaker result does not provide "
                     "unused bytes (",
                     tsi_result_to_string(result), ")"));
  }

  // Check whether we need to wrap the endpoint with a secure endpoint.
  tsi_frame_protector_type frame_protector_type;
  result = tsi_handshaker_result_get_frame_protector_type(
      handshaker_result_, &frame_protector_type);
  if (result != TSI_OK) {
    return GRPC_ERROR_CREATE(
        absl::StrCat("HTTPS proxy TLS handshaker result does not implement "
                     "get_frame_protector_type (",
                     tsi_result_to_string(result), ")"));
  }

  tsi_zero_copy_grpc_protector* zero_copy_protector = nullptr;
  tsi_frame_protector* protector = nullptr;
  size_t max_frame_size = 0;

  switch (frame_protector_type) {
    case TSI_FRAME_PROTECTOR_ZERO_COPY:
      [[fallthrough]];
    case TSI_FRAME_PROTECTOR_NORMAL_OR_ZERO_COPY:
      result = tsi_handshaker_result_create_zero_copy_grpc_protector(
          handshaker_result_, max_frame_size == 0 ? nullptr : &max_frame_size,
          &zero_copy_protector);
      if (result != TSI_OK) {
        return GRPC_ERROR_CREATE(
            absl::StrCat("HTTPS proxy zero-copy frame protector creation "
                         "failed (",
                         tsi_result_to_string(result), ")"));
      }
      break;
    case TSI_FRAME_PROTECTOR_NORMAL:
      result = tsi_handshaker_result_create_frame_protector(
          handshaker_result_, max_frame_size == 0 ? nullptr : &max_frame_size,
          &protector);
      if (result != TSI_OK) {
        return GRPC_ERROR_CREATE(absl::StrCat(
            "HTTPS proxy frame protector creation failed (",
            tsi_result_to_string(result), ")"));
      }
      break;
    case TSI_FRAME_PROTECTOR_NONE:
      break;
  }

  bool has_frame_protector =
      zero_copy_protector != nullptr || protector != nullptr;

  // Wrap the endpoint with a secure endpoint if we have a frame protector.
  if (has_frame_protector) {
    if (unused_bytes_size > 0) {
      grpc_slice slice = grpc_slice_from_copied_buffer(
          reinterpret_cast<const char*>(unused_bytes), unused_bytes_size);
      args_->endpoint = grpc_secure_endpoint_create(
          protector, zero_copy_protector, std::move(args_->endpoint), &slice, 1,
          args_->args);
      CSliceUnref(slice);
    } else {
      args_->endpoint = grpc_secure_endpoint_create(
          protector, zero_copy_protector, std::move(args_->endpoint), nullptr,
          0, args_->args);
    }
  } else if (unused_bytes_size > 0) {
    // Not wrapping the endpoint, so just pass along unused bytes.
    args_->read_buffer.Append(Slice::FromCopiedBuffer(
        reinterpret_cast<const char*>(unused_bytes), unused_bytes_size));
  }

  // Done with handshaker result.
  tsi_handshaker_result_destroy(handshaker_result_);
  handshaker_result_ = nullptr;

  return absl::OkStatus();
}

grpc_error_handle HttpsProxyTlsHandshaker::OnHandshakeNextDoneLocked(
    tsi_result result, const unsigned char* bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result) {
  grpc_error_handle error;

  if (is_shutdown_) {
    return GRPC_ERROR_CREATE("HTTPS proxy TLS handshaker shutdown");
  }

  if (result == TSI_INCOMPLETE_DATA) {
    // Need more data from peer.
    grpc_endpoint_read(
        args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
        NewClosure([self = RefAsSubclass<HttpsProxyTlsHandshaker>()](
                       absl::Status status) {
          self->OnHandshakeDataReceivedFromPeerFnScheduler(std::move(status));
        }),
        /*urgent=*/true, /*min_progress_size=*/1);
    return absl::OkStatus();
  }

  if (result != TSI_OK) {
    return GRPC_ERROR_CREATE(
        absl::StrCat("HTTPS proxy TLS handshake failed (",
                     tsi_result_to_string(result), ")"));
  }

  // If we have data to send, send it.
  if (bytes_to_send_size > 0) {
    grpc_slice to_send = grpc_slice_from_copied_buffer(
        reinterpret_cast<const char*>(bytes_to_send), bytes_to_send_size);
    outgoing_.Append(Slice(to_send));
    grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs
        write_args;
    write_args.set_max_frame_size(INT_MAX);
    grpc_endpoint_write(
        args_->endpoint.get(), outgoing_.c_slice_buffer(),
        NewClosure([self = RefAsSubclass<HttpsProxyTlsHandshaker>()](
                       absl::Status status) {
          self->OnHandshakeDataSentToPeerFnScheduler(std::move(status));
        }),
        std::move(write_args));
  }

  // If handshake is complete, process the result.
  if (handshaker_result != nullptr) {
    GRPC_CHECK_EQ(handshaker_result_, nullptr);
    handshaker_result_ = handshaker_result;
    if (bytes_to_send_size == 0) {
      // No data to send, so we can process the result now.
      error = ProcessHandshakeResult();
      if (!error.ok()) {
        return error;
      }
      Finish(absl::OkStatus());
    }
    // Otherwise, we'll process the result after the write completes.
  }

  return absl::OkStatus();
}

void HttpsProxyTlsHandshaker::OnHandshakeNextDoneGrpcWrapper(
    tsi_result result, void* user_data, const unsigned char* bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result) {
  RefCountedPtr<HttpsProxyTlsHandshaker> self(
      static_cast<HttpsProxyTlsHandshaker*>(user_data));
  MutexLock lock(&self->mu_);
  grpc_error_handle error = self->OnHandshakeNextDoneLocked(
      result, bytes_to_send, bytes_to_send_size, handshaker_result);
  if (!error.ok()) {
    self->HandshakeFailedLocked(std::move(error));
  }
}

grpc_error_handle HttpsProxyTlsHandshaker::DoHandshakerNextLocked(
    const unsigned char* bytes_received, size_t bytes_received_size) {
  const unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  tsi_handshaker_result* hs_result = nullptr;
  auto self = RefAsSubclass<HttpsProxyTlsHandshaker>();
  tsi_result result = tsi_handshaker_next(
      tsi_handshaker_, bytes_received, bytes_received_size, &bytes_to_send,
      &bytes_to_send_size, &hs_result, &OnHandshakeNextDoneGrpcWrapper,
      self.get(), &tsi_handshake_error_);
  if (result == TSI_ASYNC) {
    // Handshaker operating asynchronously.
    self.release();
    return absl::OkStatus();
  }
  // Handshaker returned synchronously.
  return OnHandshakeNextDoneLocked(result, bytes_to_send, bytes_to_send_size,
                                   hs_result);
}

void HttpsProxyTlsHandshaker::OnHandshakeDataReceivedFromPeerFnScheduler(
    grpc_error_handle error) {
  args_->event_engine->Run(
      [self = RefAsSubclass<HttpsProxyTlsHandshaker>(),
       error = std::move(error)]() mutable {
        ExecCtx exec_ctx;
        self->OnHandshakeDataReceivedFromPeerFn(std::move(error));
        self.reset();
      });
}

void HttpsProxyTlsHandshaker::OnHandshakeDataReceivedFromPeerFn(
    absl::Status error) {
  MutexLock lock(&mu_);
  if (!error.ok() || is_shutdown_) {
    HandshakeFailedLocked(GRPC_ERROR_CREATE_REFERENCING(
        "HTTPS proxy TLS handshake read failed", &error, 1));
    return;
  }
  size_t bytes_received_size = MoveReadBufferIntoHandshakeBuffer();
  error = DoHandshakerNextLocked(handshake_buffer_, bytes_received_size);
  if (!error.ok()) {
    HandshakeFailedLocked(std::move(error));
  }
}

void HttpsProxyTlsHandshaker::OnHandshakeDataSentToPeerFnScheduler(
    grpc_error_handle error) {
  args_->event_engine->Run(
      [self = RefAsSubclass<HttpsProxyTlsHandshaker>(),
       error = std::move(error)]() mutable {
        ExecCtx exec_ctx;
        self->OnHandshakeDataSentToPeerFn(std::move(error));
        self.reset();
      });
}

void HttpsProxyTlsHandshaker::OnHandshakeDataSentToPeerFn(absl::Status error) {
  MutexLock lock(&mu_);
  if (!error.ok() || is_shutdown_) {
    HandshakeFailedLocked(GRPC_ERROR_CREATE_REFERENCING(
        "HTTPS proxy TLS handshake write failed", &error, 1));
    return;
  }
  // If we have a handshaker result, process it now.
  if (handshaker_result_ != nullptr) {
    error = ProcessHandshakeResult();
    if (!error.ok()) {
      HandshakeFailedLocked(std::move(error));
      return;
    }
    Finish(absl::OkStatus());
  } else {
    // Otherwise, read more data.
    grpc_endpoint_read(
        args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
        NewClosure([self = RefAsSubclass<HttpsProxyTlsHandshaker>()](
                       absl::Status status) {
          self->OnHandshakeDataReceivedFromPeerFnScheduler(std::move(status));
        }),
        /*urgent=*/true, /*min_progress_size=*/1);
  }
}

void HttpsProxyTlsHandshaker::Shutdown(absl::Status /*error*/) {
  MutexLock lock(&mu_);
  if (!is_shutdown_) {
    is_shutdown_ = true;
    if (tsi_handshaker_ != nullptr) {
      tsi_handshaker_shutdown(tsi_handshaker_);
    }
    args_->endpoint.reset();
  }
}

void HttpsProxyTlsHandshaker::DoHandshake(
    HandshakerArgs* args,
    absl::AnyInvocable<void(absl::Status)> on_handshake_done) {
  // If HTTPS proxy TLS is not enabled, skip this handshaker.
  if (!args->args.GetBool(GRPC_ARG_HTTP_PROXY_TLS_ENABLED).value_or(false)) {
    InvokeOnHandshakeDone(args, std::move(on_handshake_done), absl::OkStatus());
    return;
  }

  // Create the TSI handshaker.
  if (handshaker_factory_ == nullptr) {
    InvokeOnHandshakeDone(
        args, std::move(on_handshake_done),
        GRPC_ERROR_CREATE(
            "HTTPS proxy TLS handshaker factory not initialized"));
    return;
  }

  tsi_result result = tsi_ssl_client_handshaker_factory_create_handshaker(
      handshaker_factory_, proxy_server_name_.c_str(),
      /*network_bio_buf_size=*/0,
      /*ssl_bio_buf_size=*/0, /*alpn_protocols=*/std::nullopt, &tsi_handshaker_);
  if (result != TSI_OK || tsi_handshaker_ == nullptr) {
    InvokeOnHandshakeDone(
        args, std::move(on_handshake_done),
        GRPC_ERROR_CREATE(absl::StrCat(
            "Failed to create HTTPS proxy TLS handshaker (",
            tsi_result_to_string(result), ")")));
    return;
  }

  MutexLock lock(&mu_);
  args_ = args;
  on_handshake_done_ = std::move(on_handshake_done);

  VLOG(2) << "Starting TLS handshake with HTTPS proxy: " << proxy_server_name_;

  // Start the TLS handshake.
  size_t bytes_received_size = MoveReadBufferIntoHandshakeBuffer();
  grpc_error_handle error =
      DoHandshakerNextLocked(handshake_buffer_, bytes_received_size);
  if (!error.ok()) {
    HandshakeFailedLocked(std::move(error));
  }
}

//
// HttpsProxyTlsHandshakerFactory
//

class HttpsProxyTlsHandshakerFactory : public HandshakerFactory {
 public:
  HttpsProxyTlsHandshakerFactory();
  ~HttpsProxyTlsHandshakerFactory() override;

  void AddHandshakers(const ChannelArgs& args,
                      grpc_pollset_set* interested_parties,
                      HandshakeManager* handshake_mgr) override;

  HandshakerPriority Priority() override {
    return HandshakerPriority::kHTTPProxyTLSHandshakers;
  }

 private:
  tsi_result InitHandshakerFactory(const ChannelArgs& args);

  Mutex mu_;
  tsi_ssl_client_handshaker_factory* handshaker_factory_
      ABSL_GUARDED_BY(mu_) = nullptr;
  bool factory_initialized_ ABSL_GUARDED_BY(mu_) = false;
};

HttpsProxyTlsHandshakerFactory::HttpsProxyTlsHandshakerFactory() = default;

HttpsProxyTlsHandshakerFactory::~HttpsProxyTlsHandshakerFactory() {
  if (handshaker_factory_ != nullptr) {
    tsi_ssl_client_handshaker_factory_unref(handshaker_factory_);
  }
}

tsi_result HttpsProxyTlsHandshakerFactory::InitHandshakerFactory(
    const ChannelArgs& args) {
  tsi_ssl_client_handshaker_options options;

  // Check if server certificate verification should be skipped.
  bool verify_server_cert =
      args.GetBool(GRPC_ARG_HTTP_PROXY_TLS_VERIFY_SERVER_CERT).value_or(true);
  if (!verify_server_cert) {
    options.skip_server_certificate_verification = true;
  }

  // Get root certificates for proxy verification.
  std::optional<std::string> root_certs =
      args.GetOwnedString(GRPC_ARG_HTTP_PROXY_TLS_ROOT_CERTS);
  if (root_certs.has_value()) {
    options.root_cert_info =
        std::make_shared<RootCertInfo>(root_certs->c_str());
  } else if (verify_server_cert) {
    // Only load default root certs if we need to verify.
    // Use system default root certificates for proxy TLS verification.
    const char* default_root_certs = DefaultSslRootStore::GetPemRootCerts();
    if (default_root_certs != nullptr) {
      options.root_cert_info =
          std::make_shared<RootCertInfo>(default_root_certs);
    }
    // Also set root_store if available (for OpenSSL 1.1+).
    options.root_store = DefaultSslRootStore::GetRootStore();
  }

  // Get client certificate and key for mTLS with proxy.
  std::optional<std::string> cert_chain =
      args.GetOwnedString(GRPC_ARG_HTTP_PROXY_TLS_CERT_CHAIN);
  std::optional<std::string> private_key =
      args.GetOwnedString(GRPC_ARG_HTTP_PROXY_TLS_PRIVATE_KEY);

  tsi_ssl_pem_key_cert_pair pem_key_cert_pair;
  if (cert_chain.has_value() && private_key.has_value()) {
    pem_key_cert_pair.private_key = private_key->c_str();
    pem_key_cert_pair.cert_chain = cert_chain->c_str();
    options.pem_key_cert_pair = &pem_key_cert_pair;
  }

  return tsi_create_ssl_client_handshaker_factory_with_options(
      &options, &handshaker_factory_);
}

void HttpsProxyTlsHandshakerFactory::AddHandshakers(
    const ChannelArgs& args, grpc_pollset_set* /*interested_parties*/,
    HandshakeManager* handshake_mgr) {
  // Only add handshaker if HTTPS proxy TLS is enabled.
  if (!args.GetBool(GRPC_ARG_HTTP_PROXY_TLS_ENABLED).value_or(false)) {
    return;
  }

  MutexLock lock(&mu_);

  // Initialize the handshaker factory if not already done.
  if (!factory_initialized_) {
    tsi_result result = InitHandshakerFactory(args);
    factory_initialized_ = true;
    if (result != TSI_OK) {
      LOG(ERROR) << "Failed to initialize HTTPS proxy TLS handshaker factory: "
                 << tsi_result_to_string(result);
      return;
    }
  }

  if (handshaker_factory_ == nullptr) {
    LOG(ERROR) << "HTTPS proxy TLS handshaker factory is null";
    return;
  }

  // Get the proxy server name for TLS verification.
  std::string proxy_server_name;
  std::optional<std::string> server_name =
      args.GetOwnedString(GRPC_ARG_HTTP_PROXY_TLS_SERVER_NAME);
  if (server_name.has_value()) {
    proxy_server_name = *server_name;
  } else {
    // If no server name specified, this will be set by the proxy mapper
    // based on the proxy URI hostname.
    LOG(WARNING) << "HTTPS proxy TLS server name not specified";
  }

  handshake_mgr->Add(MakeRefCounted<HttpsProxyTlsHandshaker>(
      handshaker_factory_, std::move(proxy_server_name)));
}

}  // namespace

void RegisterHttpsProxyTlsHandshaker(CoreConfiguration::Builder* builder) {
  builder->handshaker_registry()->RegisterHandshakerFactory(
      HANDSHAKER_CLIENT, std::make_unique<HttpsProxyTlsHandshakerFactory>());
}

}  // namespace grpc_core
