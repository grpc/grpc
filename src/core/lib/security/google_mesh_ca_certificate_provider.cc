//
//
// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/google_mesh_ca_certificate_provider.h"

#include <random>

#include "absl/functional/bind_front.h"
#include "absl/strings/str_cat.h"
#include "upb/upb.hpp"

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/slice.h>

#include "src/core/ext/upb-generated/google/protobuf/duration.upb.h"
#include "src/core/ext/upb-generated/third_party/istio/security/proto/providers/google/meshca.upb.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"

namespace grpc_core {

TraceFlag grpc_mesh_ca_certificate_provider(false,
                                            "mesh_ca_certificate_provider");

namespace {
// TODO(yashykt): Maybe use static metadata
const char* kMeshCaFullMethodName =
    "/google.security.meshca.v1.MeshCertificateService/CreateCertificate";

// Backoff constants
const grpc_millis kInitialBackoff = 1000;
const double kMultiplier = 1.6;
const double kJitter = 0.2;
const grpc_millis kMaxBackoff = 120000;

std::string RandomUuid() {
  static const char alphabet[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  // Generate 16 random bytes.
  std::default_random_engine engine(ExecCtx::Get()->Now());
  std::uniform_int_distribution<int> uniform_nibble(0, 15);
  std::uniform_int_distribution<int> uniform_variant(8, 11);
  std::vector<char> uuid(36, 0);
  // Set the dashes.
  uuid[8] = uuid[13] = uuid[18] = uuid[23] = '-';
  // Set version.
  uuid[14] = '4';
  // Set variant.
  uuid[19] = alphabet[uniform_variant(engine)];
  // Set all other characters randomly.
  for (int i = 0; i < uuid.size(); i++) {
    if (uuid[i] == 0) {
      uuid[i] = alphabet[uniform_nibble(engine)];
    }
  }
  return std::string(&uuid[0], 36);
}

}  // namespace

// Generate CSR
absl::optional<std::string> GoogleMeshCaCertificateProvider::GenerateCSR() {
  bssl::UniquePtr<RSA> rsa(RSA_new());
  if (rsa == nullptr) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to allocate RSA", this);
    return absl::nullopt;
  }
  bssl::UniquePtr<BIGNUM> e(BN_new());
  if (e == nullptr) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to allocated BIGNUM", this);
    return absl::nullopt;
  }
  BN_set_word(e.get(), RSA_F4);
  // Generate RSA key
  if (!RSA_generate_key_ex(rsa.get(), key_size_, e.get(), nullptr)) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to generate RSA key", this);
    return absl::nullopt;
  }
  bssl::UniquePtr<EVP_PKEY> pkey(EVP_PKEY_new());
  if (pkey == nullptr) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to allocate EVP_PKEY", this);
    return absl::nullopt;
  }
  if (!EVP_PKEY_set1_RSA(pkey.get(), rsa.get())) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to set key in EVP_KEY", this);
    return absl::nullopt;
  }
  bssl::UniquePtr<BIO> bio(BIO_new(BIO_s_mem()));
  if (bio == nullptr) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to allocate BIO", this);
    return absl::nullopt;
  }
  // Store private key in pem format
  if (!PEM_write_bio_PrivateKey(bio.get(), pkey.get(), nullptr, nullptr, 0,
                                nullptr, nullptr)) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to write key to bio", this);
    return absl::nullopt;
  }
  size_t len = 0;
  const uint8_t* data = nullptr;
  if (!BIO_mem_contents(bio.get(), &data, &len)) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to get contents of bio", this);
    return absl::nullopt;
  }
  GPR_ASSERT(data != nullptr);
  GPR_ASSERT(len > 0);
  private_key_.assign(reinterpret_cast<const char*>(data), len);
  // Create CSR
  bssl::UniquePtr<X509_REQ> req(X509_REQ_new());
  if (req == nullptr) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to allocate X509_REQ", this);
    return absl::nullopt;
  }
  if (!X509_REQ_set_version(req.get(), 0L)) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to set X509_REQ version", this);
    return absl::nullopt;
  }
  // Mesh CA only cares about the public key. Ignore the other fields.
  if (!X509_REQ_set_pubkey(req.get(), pkey.get())) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to set X509_REQ public key", this);
    return absl::nullopt;
  }
  // Sign the CSR
  if (!X509_REQ_sign(req.get(), pkey.get(), EVP_sha1())) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to sign X509_REQ key", this);
    return absl::nullopt;
  }
  bio.reset(BIO_new(BIO_s_mem()));
  if (!PEM_write_bio_X509_REQ(bio.get(), req.get())) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to write key to bio", this);
    return absl::nullopt;
  }
  if (!BIO_mem_contents(bio.get(), &data, &len)) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to get contents of bio", this);
    return absl::nullopt;
  }
  GPR_ASSERT(data != nullptr);
  GPR_ASSERT(len > 0);
  return std::string(reinterpret_cast<const char*>(data), len);
}

void GoogleMeshCaCertificateProvider::ParseCertChain() {
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, response_payload_);
  grpc_slice res_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_byte_buffer_reader_destroy(&bbr);
  upb::Arena arena;
  google_security_meshca_v1_MeshCertificateResponse* res =
      google_security_meshca_v1_MeshCertificateResponse_parse(
          (const char*)GRPC_SLICE_START_PTR(res_slice),
          GRPC_SLICE_LENGTH(res_slice), arena.ptr());
  if (res == nullptr) {
    grpc_slice_unref_internal(res_slice);
    gpr_log(GPR_ERROR, "Failed to parse Mesh CA response.");
    return;
  }
  size_t size;
  const upb_strview* cert_chain =
      google_security_meshca_v1_MeshCertificateResponse_cert_chain(res, &size);
  if (size == 0) {
    grpc_slice_unref_internal(res_slice);
    gpr_log(GPR_ERROR, "No certificate in response");
    return;
  }
  std::string joined_cert_chain;
  for (int i = 0; i < size; i++) {
    absl::StrAppend(&joined_cert_chain,
                    std::string(cert_chain[i].data, cert_chain[i].size));
  }
  std::string root_certs(cert_chain[size - 1].data, cert_chain[size - 1].size);
  grpc_slice_unref_internal(res_slice);
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(private_key_.c_str());
  ssl_pair->cert_chain = gpr_strdup(joined_cert_chain.c_str());
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(ssl_pair);
  parsed_result_ = {root_certs, pem_key_cert_pairs};
}

GoogleMeshCaCertificateProvider::GoogleMeshCaCertificateProvider(
    std::string endpoint, grpc_channel_credentials* channel_creds,
    grpc_millis timeout, grpc_millis certificate_lifetime,
    grpc_millis renewal_grace_period, uint32_t key_size)
    : endpoint_(endpoint),
      timeout_(timeout),
      certificate_lifetime_(certificate_lifetime),
      renewal_grace_period_(renewal_grace_period),
      key_size_(key_size),
      distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()),
      backoff_(BackOff::Options()
                   .set_initial_backoff(kInitialBackoff)
                   .set_multiplier(kMultiplier)
                   .set_jitter(kJitter)
                   .set_max_backoff(kMaxBackoff)) {
  distributor_->SetWatchStatusCallback(absl::bind_front(
      &GoogleMeshCaCertificateProvider::WatchStatusCallback, this));
  channel_ = grpc_secure_channel_create(channel_creds, endpoint_.c_str(),
                                        nullptr, nullptr);
  GPR_ASSERT(channel_ != nullptr);
  GRPC_CLOSURE_INIT(&on_call_complete_, OnCallComplete, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&on_renewal_timer_, OnRenewalTimer, this,
                    grpc_schedule_on_exec_ctx);
}

GoogleMeshCaCertificateProvider::~GoogleMeshCaCertificateProvider() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_mesh_ca_certificate_provider)) {
    gpr_log(GPR_INFO, "[mesh_ca:%p] Destroying", this);
  }
  if (channel_ != nullptr) {
    grpc_channel_destroy_internal(channel_);
  }
}

// Spawn the private key and CSR, store the private key and make the request
// byte buffer. Return true on success, false otherwise.
bool GoogleMeshCaCertificateProvider::GenerateRequestLocked() {
  absl::optional<std::string> csr = GenerateCSR();
  if (!csr.has_value()) {
    return false;
  }
  std::string uuid = RandomUuid();
  // Make the MeshCertificateRequest object based on the CSR.
  upb::Arena arena;
  google_security_meshca_v1_MeshCertificateRequest* req =
      google_security_meshca_v1_MeshCertificateRequest_new(arena.ptr());
  google_security_meshca_v1_MeshCertificateRequest_set_request_id(
      req, upb_strview_makez(uuid.c_str()));
  google_security_meshca_v1_MeshCertificateRequest_set_csr(
      req, upb_strview_makez(csr->c_str()));
  gpr_timespec validity_ts =
      grpc_millis_to_timespec(certificate_lifetime_, GPR_TIMESPAN);
  google_protobuf_Duration* validity =
      google_security_meshca_v1_MeshCertificateRequest_mutable_validity(
          req, arena.ptr());
  google_protobuf_Duration_set_seconds(validity, validity_ts.tv_sec);
  google_protobuf_Duration_set_nanos(validity, validity_ts.tv_nsec);
  size_t len;
  char* send_buf = google_security_meshca_v1_MeshCertificateRequest_serialize(
      req, arena.ptr(), &len);
  grpc_slice send_slice = grpc_slice_from_copied_buffer(send_buf, len);
  request_payload_ = grpc_raw_byte_buffer_create(&send_slice, 1);
  grpc_slice_unref_internal(send_slice);
  return true;
}

void GoogleMeshCaCertificateProvider::StartCallLocked() {
  grpc_metadata_array_init(&initial_metadata_recv_);
  grpc_metadata_array_init(&trailing_metadata_recv_);
  GPR_ASSERT(call_ == nullptr);
  call_ = grpc_channel_create_pollset_set_call(
      channel_, nullptr, GRPC_PROPAGATE_DEFAULTS, interested_parties(),
      grpc_slice_from_static_string(kMeshCaFullMethodName), nullptr,
      ExecCtx::Get()->Now() + timeout_, nullptr);
  grpc_op ops[6];
  memset(ops, 0, sizeof(ops));
  grpc_op* op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY |
              GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  if (!GenerateRequestLocked()) {
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to generate request", this);
    status_ = GRPC_STATUS_INTERNAL;
    status_details_ =
        grpc_slice_from_static_string("Failed to generate request");
    return OnCallComplete();
  }
  op->data.send_message.send_message = request_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv_;
  op->data.recv_status_on_client.status = &status_;
  op->data.recv_status_on_client.status_details = &status_details_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // Take a ref while the call is in progress
  Ref().release();
  auto call_error = grpc_call_start_batch_and_execute(
      call_, ops, static_cast<size_t>(op - ops), &on_call_complete_);
  GPR_ASSERT(call_error == GRPC_CALL_OK);
}

void GoogleMeshCaCertificateProvider::OnCallComplete(void* arg,
                                                     grpc_error* error) {
  RefCountedPtr<GoogleMeshCaCertificateProvider> self(
      static_cast<GoogleMeshCaCertificateProvider*>(arg));
  self->OnCallComplete();
}

void GoogleMeshCaCertificateProvider::OnCallComplete() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_mesh_ca_certificate_provider)) {
    gpr_log(GPR_INFO, "[mesh_ca:%p] Call Complete", this);
  }
  MutexLock lock(&mu_);
  grpc_millis next_renewal_time;
  if (status_ != GRPC_STATUS_OK) {
    next_renewal_time = backoff_.NextAttemptTime();
    char* status_message = grpc_slice_to_c_string(status_details_);
    gpr_log(GPR_ERROR, "[mesh_ca:%p] Call failed. status=%d status message:%s",
            this, status_, status_message);
    gpr_free(status_message);
    parsed_result_.reset();
    distributor_->SetError(grpc_error_set_str(
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Call failed"),
                           GRPC_ERROR_INT_GRPC_STATUS, status_),
        GRPC_ERROR_STR_GRPC_MESSAGE, grpc_slice_ref(status_details_)));
  } else {
    ParseCertChain();
    if (parsed_result_.has_value()) {
      time_of_certificate_ = ExecCtx::Get()->Now();
      distributor_->SetKeyMaterials("", parsed_result_->pem_root_certs,
                                    parsed_result_->pem_key_cert_pairs);
      backoff_.Reset();
      // Next renewal will be at the beginning of the grace period.
      // TODO(yashkt): check the expiration date of the received certificate
      // to get a more accurate time.
      next_renewal_time =
          ExecCtx::Get()->Now() + certificate_lifetime_ - renewal_grace_period_;
    } else {
      gpr_log(GPR_ERROR, "[mesh_ca:%p] Failed to parse response.", this);
      next_renewal_time = backoff_.NextAttemptTime();
      distributor_->SetError(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to parse response"));
    }
  }
  // Ref self for the timer.
  Ref().release();
  // Set the timer for next CSR with Mesh CA.
  grpc_timer_init(&renewal_timer_, next_renewal_time, &on_renewal_timer_);
  // Release the call resources.
  grpc_call_unref(call_);
  call_ = nullptr;
  grpc_metadata_array_destroy(&initial_metadata_recv_);
  grpc_metadata_array_destroy(&trailing_metadata_recv_);
  grpc_byte_buffer_destroy(request_payload_);
  grpc_byte_buffer_destroy(response_payload_);
  grpc_slice_unref_internal(status_details_);
}

void GoogleMeshCaCertificateProvider::WatchStatusCallback(
    std::string cert_name, bool root_being_watched,
    bool identity_being_watched) {
  MutexLock lock(&mu_);
  if (root_being_watched || identity_being_watched) {
    // If we have a valid certificate from previous calls. Use it if it is not
    // due for renewal
    if (parsed_result_.has_value() &&
        (time_of_certificate_ + certificate_lifetime_ - renewal_grace_period_ >
         (ExecCtx::Get()->Now()))) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_mesh_ca_certificate_provider)) {
        gpr_log(GPR_INFO,
                "[mesh_ca:%p] Watch started again. Send previously received "
                "certificates %ld vs %ld",
                this,
                time_of_certificate_ + certificate_lifetime_ -
                    renewal_grace_period_,
                ExecCtx::Get()->Now());
      }
      distributor_->SetKeyMaterials("", parsed_result_->pem_root_certs,
                                    parsed_result_->pem_key_cert_pairs);
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_mesh_ca_certificate_provider)) {
        gpr_log(GPR_INFO, "[mesh_ca:%p] Watch started. Starting call %ld %ld",
                this,
                time_of_certificate_ + certificate_lifetime_ -
                    renewal_grace_period_,
                ExecCtx::Get()->Now());
      }
      StartCallLocked();
    }
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_mesh_ca_certificate_provider)) {
      gpr_log(GPR_INFO, "[mesh_ca:%p] Watch cancelled", this);
    }
    // Cancel the timer so that we can let go of internal refs.
    grpc_timer_cancel(&renewal_timer_);
  }
}

void GoogleMeshCaCertificateProvider::OnRenewalTimer(void* arg,
                                                     grpc_error* error) {
  RefCountedPtr<GoogleMeshCaCertificateProvider> self(
      static_cast<GoogleMeshCaCertificateProvider*>(arg));
  if (error == GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_mesh_ca_certificate_provider)) {
      gpr_log(GPR_INFO, "[mesh_ca:%p] Renewal timer fired. Starting call", arg);
    }
    MutexLock lock(&self->mu_);
    self->StartCallLocked();
  } else {
    // The timer was cancelled. Do nothing.
  }
}

}  // namespace grpc_core
