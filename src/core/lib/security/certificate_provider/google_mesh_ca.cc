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

#include "src/core/lib/security/certificate_provider/google_mesh_ca.h"

#include "absl/strings/str_cat.h"
#include "src/core/ext/upb-generated/google/protobuf/duration.upb.h"
#include "src/core/ext/upb-generated/third_party/istio/security/proto/providers/google/meshca.upb.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/certificate_provider/config.h"
#include "src/core/lib/security/certificate_provider/factory.h"
#include "src/core/lib/security/certificate_provider/provider.h"
#include "src/core/lib/security/certificate_provider/registry.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "upb/upb.hpp"

#include <fstream>
#include <random>
#include <sstream>
#include <type_traits>

#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/slice.h>

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

namespace grpc_core {

static const char* kGoogleMeshCa = "google_mesh_ca";
static const grpc_millis kInitialBackoff = 1000;
static const double kMultiplier = 1.6;
static const double kJitter = 0.2;
static const grpc_millis kMaxBackoff = 120000;
static const char* kMeshCaRequestPath =
    "/google.security.meshca.v1.MeshCertificateService/CreateCertificate";

template <typename T>
static grpc_error* ExtractJsonType(const Json& json,
                                   const std::string& field_name, T* output) {
  if (json.type() != Json::Type::NUMBER) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field ", field_name,
                     " has unexpected type (expected type: NUMBER).")
            .c_str());
  } else {
    std::istringstream ss(json.string_value());
    ss >> *output;
    return GRPC_ERROR_NONE;
  }
}

template <>
grpc_error* ExtractJsonType<bool>(const Json& json,
                                  const std::string& field_name, bool* output) {
  switch (json.type()) {
    case Json::Type::JSON_TRUE:
      *output = true;
      return GRPC_ERROR_NONE;
    case Json::Type::JSON_FALSE:
      *output = false;
      return GRPC_ERROR_NONE;
    default:
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("field ", field_name,
                       " has unexpected type (expected type: BOOLEAN).")
              .c_str());
  };
}

template <typename T>
static grpc_error* ExtractJsonType(const Json& json,
                                   const std::string& field_name,
                                   const T** output) {
  // Specialized versions should always be used.
  // TODO: static_assert(false, "Specialized version of ExtractJsonType should
  // be used");
  GPR_UNREACHABLE_CODE(return GRPC_ERROR_CANCELLED);
}

template <>
grpc_error* ExtractJsonType<Json::Object>(const Json& json,
                                          const std::string& field_name,
                                          const Json::Object** output) {
  if (json.type() != Json::Type::OBJECT) {
    *output = nullptr;
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field ", field_name,
                     " has unexpected type (expected type: OBJECT).")
            .c_str());
  } else {
    *output = &json.object_value();
    return GRPC_ERROR_NONE;
  }
}

template <>
grpc_error* ExtractJsonType<Json::Array>(const Json& json,
                                         const std::string& field_name,
                                         const Json::Array** output) {
  if (json.type() != Json::Type::ARRAY) {
    *output = nullptr;
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field ", field_name,
                     " has unexpected type (expected type: ARRAY).")
            .c_str());
  } else {
    *output = &json.array_value();
    return GRPC_ERROR_NONE;
  }
}

template <>
grpc_error* ExtractJsonType<std::string>(const Json& json,
                                         const std::string& field_name,
                                         const std::string** output) {
  if (json.type() != Json::Type::STRING) {
    *output = nullptr;
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field ", field_name,
                     " has unexpected type (expected type: STRING).")
            .c_str());
  } else {
    *output = &json.string_value();
    return GRPC_ERROR_NONE;
  }
}

template <typename T>
static grpc_error* ParseJsonObjectField(const Json::Object& object,
                                        const std::string& field_name,
                                        T* output, bool optional = false) {
  auto it = object.find(field_name);
  if (it == object.end()) {
    if (optional) {
      *output = (T)0;
      return GRPC_ERROR_NONE;
    } else {
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("field ", field_name, " does not exist.").c_str());
    }
  } else {
    auto& child_object_json = it->second;
    return ExtractJsonType(child_object_json, field_name, output);
  }
}

template <typename T, typename U>
static absl::InlinedVector<grpc_error*, 1> IterateJsonArrayInternal(
    const Json::Array& array, const std::string& field_name,
    std::function<void(const T&)> pred) {
  absl::InlinedVector<grpc_error*, 1> result;
  for (int i = 0; i < array.size(); i++) {
    U item;
    grpc_error* error =
        ExtractJsonType(array[i], absl::StrCat(field_name, "[", i, "]"), &item);
    if (error != GRPC_ERROR_NONE) {
      result.push_back(error);
    } else {
      pred(*item);
    }
  }
  return result;
}

template <typename T>
static absl::InlinedVector<grpc_error*, 1> IterateJsonArray(
    const Json::Array& array, const std::string& field_name,
    std::function<void(const T&)> pred) {
  return IterateJsonArrayInternal<T, T>(array, field_name, pred);
}

template <>
absl::InlinedVector<grpc_error*, 1> IterateJsonArray<Json::Object>(
    const Json::Array& array, const std::string& field_name,
    std::function<void(const Json::Object&)> pred) {
  return IterateJsonArrayInternal<Json::Object, const Json::Object*>(
      array, field_name, pred);
}

static grpc_error* ParseDurationField(const Json::Object& object,
                                      grpc_millis* duration) {
  grpc_error* result = GRPC_ERROR_NONE;
  int64_t seconds = 0;
  result = ParseJsonObjectField(object, "seconds", &seconds);
  if (result == GRPC_ERROR_NONE) {
    int32_t nanoseconds = 0;
    result = ParseJsonObjectField(object, "nanos", &nanoseconds, true);
    if (result == GRPC_ERROR_NONE) {
      *duration = seconds * GPR_MS_PER_SEC + nanoseconds / GPR_NS_PER_MS;
    }
  }
  return result;
}

// Make a key pair.
static EVP_PKEY* MakeKeys(uint32_t key_size) {
  RSA* rsa = RSA_new();
  GPR_ASSERT(rsa != nullptr);
  BIGNUM* e = BN_new();
  GPR_ASSERT(e != nullptr);
  BN_set_word(e, 65537);
  GPR_ASSERT(RSA_generate_key_ex(rsa, key_size, e, nullptr));
  EVP_PKEY* keys = EVP_PKEY_new();
  GPR_ASSERT(keys != nullptr);
  GPR_ASSERT(EVP_PKEY_assign_RSA(keys, rsa));
  BN_free(e);
  return keys;
}

// Make a CSR based on keys.
static X509_REQ* MakeCsr(EVP_PKEY* keys) {
  X509_REQ* csr = X509_REQ_new();
  GPR_ASSERT(csr != nullptr);
  // Mesh CA only cares about the public key; ignore everything else in the CSR.
  GPR_ASSERT(X509_REQ_set_pubkey(csr, keys));
  GPR_ASSERT(X509_REQ_sign(csr, keys, EVP_sha256()));
  return csr;
}

// Convert private key to PEM format.
static char* KeysToPem(EVP_PKEY* keys) {
  BIO* bio = BIO_new(BIO_s_mem());
  PEM_write_bio_PrivateKey(bio, keys, nullptr, nullptr, 0, nullptr, nullptr);
  int len = BIO_pending(bio);
  char* priv_key_str = static_cast<char*>(gpr_malloc(len + 1));
  BIO_read(bio, priv_key_str, len);
  BIO_free_all(bio);
  priv_key_str[len] = 0;
  return priv_key_str;
}

// Convert CSR to PEM format.
static char* CsrToPem(X509_REQ* csr) {
  BIO* bio = BIO_new(BIO_s_mem());
  PEM_write_bio_X509_REQ(bio, csr);
  int len = BIO_pending(bio);
  char* csr_str = static_cast<char*>(gpr_malloc(len + 1));
  BIO_read(bio, csr_str, len);
  BIO_free_all(bio);
  csr_str[len] = 0;
  return csr_str;
}

// Generate a random UUID.
static std::string RandomUuid() {
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

GoogleMeshCaConfig::Builder::Builder(const Json& config_json)
    : config_(MakeRefCounted<GoogleMeshCaConfig>(config_json)) {
  grpc_error* internal_error;
  absl::InlinedVector<grpc_error*, 1> error_list;
  error_ = GRPC_ERROR_NONE;
  if (config_json.type() != Json::Type::OBJECT) {
    error_ =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("config is not of type Object");
    return;
  }
  // Extract server field from GoogleMeshCaConfig message.
  const Json::Object* server;
  internal_error =
      ParseJsonObjectField(config_json.object_value(), "server", &server);
  if (internal_error != GRPC_ERROR_NONE) {
    error_list.push_back(internal_error);
  } else {
    // Extract grpc_services field from ApiConfigSource message.
    const Json::Array* grpc_services;
    internal_error =
        ParseJsonObjectField(*server, "grpcServices", &grpc_services);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else {
      // Support only one service at this moment.
      if (grpc_services->size() > 1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "multiple entries in grpcServices not supported"));
      } else {
        auto internal_error_list = IterateJsonArray<Json::Object>(
            *grpc_services, "grpcServices",
            [&error_list, this](const Json::Object& grpc_service) {
              // Extract timeout field from GrpcService message.
              const Json::Object* timeout;
              grpc_error* internal_error = ParseJsonObjectField(
                  grpc_service, "timeout", &timeout, false);
              if (internal_error != GRPC_ERROR_NONE) {
                error_list.push_back(internal_error);
              } else if (timeout != nullptr) {
                grpc_millis timeout_ms;
                internal_error = ParseDurationField(*timeout, &timeout_ms);
                if (internal_error != GRPC_ERROR_NONE) {
                  error_list.push_back(internal_error);
                } else {
                  config_->rpc_timeout_ = timeout_ms;
                }
              }
              // Extract google_grpc field from GrpcService message.
              const Json::Object* google_grpc;
              internal_error = ParseJsonObjectField(grpc_service, "googleGrpc",
                                                    &google_grpc);
              if (internal_error != GRPC_ERROR_NONE) {
                error_list.push_back(internal_error);
              } else {
                // Extract target_uri field from GoogleGrpc message.
                const std::string* target_uri;
                internal_error = ParseJsonObjectField(*google_grpc, "targetUri",
                                                      &target_uri);
                if (internal_error != GRPC_ERROR_NONE) {
                  error_list.push_back(internal_error);
                } else {
                  config_->endpoint_ = *target_uri;
                }
                // Extract call_credentials field from GoogleGrpc message.
                const Json::Array* call_credentials;
                internal_error = ParseJsonObjectField(
                    *google_grpc, "callCredentials", &call_credentials, true);
                if (internal_error != GRPC_ERROR_NONE) {
                  error_list.push_back(internal_error);
                } else if (call_credentials != nullptr) {
                  if (call_credentials->size() > 1) {
                    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                        "multiple entries in callCredentials not supported"));
                  } else {
                    // Iterate over the call_credentials array.
                    auto internal_error_list = IterateJsonArray<Json::Object>(
                        *call_credentials, "callCredentials",
                        [&error_list,
                         this](const Json::Object& call_credential) {
                          // Extract sts_service field from CallCredentials
                          // message.
                          const Json::Object* sts_service;
                          grpc_error* internal_error = ParseJsonObjectField(
                              call_credential, "stsService", &sts_service);
                          if (internal_error != GRPC_ERROR_NONE) {
                            error_list.push_back(internal_error);
                          } else {
                            GoogleMeshCaConfig::StsConfig sts_config;
                            // Extract fields in StsService message.
                            const std::string* string_value;
                            internal_error = ParseJsonObjectField(
                                *sts_service, "tokenExchangeServiceUri",
                                &string_value);
                            if (internal_error != GRPC_ERROR_NONE) {
                              error_list.push_back(internal_error);
                            } else {
                              sts_config.token_exchange_service_uri =
                                  *string_value;
                            }
                            internal_error = ParseJsonObjectField(
                                *sts_service, "subject_token_path",
                                &string_value);
                            if (internal_error != GRPC_ERROR_NONE) {
                              error_list.push_back(internal_error);
                            } else {
                              sts_config.subject_token_path = *string_value;
                            }
                            internal_error = ParseJsonObjectField(
                                *sts_service, "subject_token_type",
                                &string_value);
                            if (internal_error != GRPC_ERROR_NONE) {
                              error_list.push_back(internal_error);
                            } else {
                              sts_config.subject_token_type = *string_value;
                            }
                            config_->sts_config_ = sts_config;
                          }
                        });
                    if (!internal_error_list.empty()) {
                      error_list.insert(error_list.end(),
                                        internal_error_list.begin(),
                                        internal_error_list.end());
                    }
                  }
                }
              }
            });
        if (!internal_error_list.empty()) {
          error_list.insert(error_list.end(), internal_error_list.begin(),
                            internal_error_list.end());
        }
      }
    }
  }
  // Parse certificate_lifetime field from GoogleMeshCaConfig message.
  const Json::Object* certificate_lifetime;
  internal_error =
      ParseJsonObjectField(config_json.object_value(), "certificateLifetime",
                           &certificate_lifetime, true);
  if (internal_error != GRPC_ERROR_NONE) {
    error_list.push_back(internal_error);
  } else if (certificate_lifetime != nullptr) {
    grpc_millis lifetime;
    internal_error = ParseDurationField(*certificate_lifetime, &lifetime);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else {
      config_->certificate_lifetime_ = lifetime;
    }
  }
  // Parse renewal_grace_period field from GoogleMeshCaConfig message.
  const Json::Object* renewal_grace_period;
  internal_error =
      ParseJsonObjectField(config_json.object_value(), "renewalGracePeriod",
                           &renewal_grace_period, true);
  if (internal_error != GRPC_ERROR_NONE) {
    error_list.push_back(internal_error);
  } else if (renewal_grace_period != nullptr) {
    grpc_millis period;
    internal_error = ParseDurationField(*renewal_grace_period, &period);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else {
      config_->renewal_grace_period_ = period;
    }
  }
  // Parse key_type field from GoogleMeshCaConfig message.
  const std::string* key_type;
  internal_error = ParseJsonObjectField(config_json.object_value(), "keyType",
                                        &key_type, true);
  if (internal_error != GRPC_ERROR_NONE) {
    error_list.push_back(internal_error);
  } else if (key_type != nullptr) {
    config_->key_type_ = *key_type;
  }
  // Parse key_size field from GoogleMeshCaConfig message.
  uint32_t key_size;
  internal_error = ParseJsonObjectField(config_json.object_value(), "keySize",
                                        &key_size, true);
  if (internal_error != GRPC_ERROR_NONE) {
    error_list.push_back(internal_error);
  } else if (key_size != 0) {
    config_->key_size_ = key_size;
  }
  // Parse location field from GoogleMeshCaConfig message.
  const std::string* gce_compute_zone;
  internal_error = ParseJsonObjectField(config_json.object_value(), "location",
                                        &gce_compute_zone, true);
  if (internal_error != GRPC_ERROR_NONE) {
    error_list.push_back(internal_error);
  } else if (gce_compute_zone != nullptr) {
    config_->gce_compute_zone_ = *gce_compute_zone;
  }
  // Build the config if no error, or generate the parent error.
  if (error_list.empty()) {
    error_ = Validate();
  } else {
    error_ = GRPC_ERROR_CREATE_FROM_VECTOR("Error parsing mesh CA config",
                                           &error_list);
  }
}

GoogleMeshCaConfig::Builder::~Builder() {
  if (error_ != GRPC_ERROR_NONE) {
    GRPC_ERROR_UNREF(error_);
  }
}

RefCountedPtr<GoogleMeshCaConfig> GoogleMeshCaConfig::Builder::Build(
    grpc_error** error) {
  *error = error_;
  return error_ == GRPC_ERROR_NONE ? config_ : nullptr;
}

grpc_error* GoogleMeshCaConfig::Builder::Validate() {
  absl::InlinedVector<grpc_error*, 1> error_list;
  // Validate values
  // endpoint cannot be empty
  if (config_->endpoint_.empty()) {
    error_list.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("CA endpoint is empty."));
  }
  if (!config_->sts_config_.subject_token_path.empty()) {
    // Check if the file exists
    // TODO (mxyan): use std::filesystem::exists when C++14/17 is supported
    std::ifstream fs(config_->sts_config_.subject_token_path);
    if (!fs.good()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("Cannot access token file at STS subject token path (",
                       config_->sts_config_.subject_token_path, ").")
              .c_str()));
    }
  }
  if (config_->rpc_timeout_ <= 0) {
    error_list.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("RPC timeout is negative."));
  }
  if (config_->certificate_lifetime_ <= 0) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Certificate lifetime is negative."));
  }
  if (config_->renewal_grace_period_ <= 0) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Renewal grace period is negative."));
  } else if (config_->renewal_grace_period_ >= config_->certificate_lifetime_) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Renewal grace period must be smaller than certificate lifetime."));
  }
  if (config_->key_type_ != "KEY_TYPE_RSA") {
    // Supports RSA key type only at this moment.
    error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Unsupported key type (", config_->key_type_, ").")
            .c_str()));
  }
  if (config_->key_size_ != 2048) {
    // Supports 2048 bits key at this moment.
    error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Unsupported key size (", config_->key_size_, ").")
            .c_str()));
  }

  return GRPC_ERROR_CREATE_FROM_VECTOR("Error validating mesh CA config",
                                       &error_list);
}

GoogleMeshCaConfig::GoogleMeshCaConfig(const Json& config_json)
    : CertificateProviderConfig(kGoogleMeshCa, config_json) {}

const char* GoogleMeshCaConfig::name() const { return kGoogleMeshCa; }

GoogleMeshCaProvider::GoogleMeshCaProvider(
    RefCountedPtr<GoogleMeshCaConfig> config,
    RefCountedPtr<grpc_tls_certificate_distributor> distributor)
    : GoogleMeshCaProvider(std::move(config), std::move(distributor), nullptr) {
}

GoogleMeshCaProvider::GoogleMeshCaProvider(
    RefCountedPtr<GoogleMeshCaConfig> config,
    RefCountedPtr<grpc_tls_certificate_distributor> distributor,
    grpc_channel_credentials* channel_creds)
    : CertificateProvider(std::move(config), std::move(distributor)),
      channel_creds_(channel_creds),
      backoff_state_(BackOff::Options()
                         .set_initial_backoff(kInitialBackoff)
                         .set_multiplier(kMultiplier)
                         .set_jitter(kJitter)
                         .set_max_backoff(kMaxBackoff)) {
  GRPC_CLOSURE_INIT(&call_complete_cb_, OnCallComplete, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&renewal_cb_, OnNextRenewal, this,
                    grpc_schedule_on_exec_ctx);
  // Start the call outside the current context to avoid contention.
  ExecCtx::Run(DEBUG_LOCATION,
               GRPC_CLOSURE_INIT(&init_client_cb_, InitClient, Ref().release(),
                                 grpc_schedule_on_exec_ctx),
               GRPC_ERROR_NONE);
}

GoogleMeshCaProvider::~GoogleMeshCaProvider() {
  if (channel_creds_ != nullptr) {
    grpc_channel_credentials_release(channel_creds_);
  }
}

void GoogleMeshCaProvider::Orphan() {
  MutexLock lock(&mu_);
  is_shutdown_ = true;
  if (call_ != nullptr) {
    grpc_call_cancel_internal(call_);
  }
  if (message_store_ != nullptr) {
    grpc_byte_buffer_destroy(message_store_);
    message_store_ = nullptr;
  }
}

void GoogleMeshCaProvider::InitClient(void* arg, grpc_error* /*error*/) {
  RefCountedPtr<GoogleMeshCaProvider> self(
      static_cast<GoogleMeshCaProvider*>(arg));
  self->InitClient();
}

void GoogleMeshCaProvider::InitClient() {
  MutexLock lock(&mu_);
  RefCountedPtr<GoogleMeshCaConfig> meshca_config = config();
  // Create the client channel to the Mesh CA server.
  // Use SSL with well known root certs as channel credentials.
  grpc_channel_credentials* channel_creds =
      channel_creds_ != nullptr
          ? channel_creds_
          : grpc_ssl_credentials_create_ex(nullptr, nullptr, nullptr, nullptr);
  if (!meshca_config->sts_config().token_exchange_service_uri.empty()) {
    // Use STS as call credentials.
    grpc_sts_credentials_options sts_options = {
        meshca_config->sts_config().token_exchange_service_uri.c_str(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        meshca_config->sts_config().subject_token_path.c_str(),
        meshca_config->sts_config().subject_token_type.c_str(),
        nullptr,
        nullptr};
    grpc_call_credentials* sts_creds =
        grpc_sts_credentials_create(&sts_options, nullptr);
    channel_creds = grpc_composite_channel_credentials_create(
        channel_creds, sts_creds, nullptr);
  }
  channel_ = grpc_secure_channel_create(
      channel_creds, meshca_config->endpoint().c_str(), nullptr, nullptr);
  GPR_ASSERT(channel_ != nullptr);
  // Starts the first call to the Mesh CA.
  StartCallLocked();
}

void GoogleMeshCaProvider::StartCallLocked() {
  grpc_metadata_array_init(&initial_metadata_recv_);
  grpc_metadata_array_init(&trailing_metadata_recv_);
  RefCountedPtr<GoogleMeshCaConfig> meshca_config = config();
  GPR_ASSERT(call_ == nullptr);
  grpc_millis now = ExecCtx::Get()->Now();
  call_ = grpc_channel_create_pollset_set_call(
      channel_, nullptr, GRPC_PROPAGATE_DEFAULTS, interested_parties(),
      grpc_slice_from_static_string(kMeshCaRequestPath), nullptr,
      now + meshca_config->rpc_timeout(), nullptr);
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
  // If the previous request failed, we will have a request stored and do not
  // make a new key or request.
  if (message_store_ == nullptr) {
    MakeKeyAndRequestLocked();
  }
  message_send_ = grpc_byte_buffer_copy(message_store_);
  op->data.send_message.send_message = message_send_;
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
  op->data.recv_message.recv_message = &message_recv_;
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
  Ref().release();
  auto call_error = grpc_call_start_batch_and_execute(
      call_, ops, static_cast<size_t>(op - ops), &call_complete_cb_);
  GPR_ASSERT(call_error == GRPC_CALL_OK);
}

void GoogleMeshCaProvider::OnCallComplete(void* arg, grpc_error* error) {
  RefCountedPtr<GoogleMeshCaProvider> self(
      static_cast<GoogleMeshCaProvider*>(arg));
  self->OnCallComplete(error);
}

// Certificate sign/renewal is considered failed in either of the following
// cases: 1) error != GRPC_ERROR_NONE. 2) Call status (status_) is not
// GRPC_STATUS_OK. 3) Error encountered when parsing the signed certificate. In
// cases 1 and 2, the same request should be retried. In case 3, a retry with a
// new request should be issued.
void GoogleMeshCaProvider::OnCallComplete(grpc_error* error) {
  GRPC_ERROR_REF(error);
  MutexLock lock(&mu_);
  RefCountedPtr<GoogleMeshCaConfig> meshca_config = config();
  if (!is_shutdown_) {
    grpc_millis next_renewal_time;
    if (error == GRPC_ERROR_NONE) {
      if (status_ != GRPC_STATUS_OK) {
        error =
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Mesh CA error from server");
        error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS, status_);
        error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE,
                                   status_details_);
      } else {
        // Next call will use a new request.
        grpc_byte_buffer_destroy(message_store_);
        message_store_ = nullptr;
        std::vector<std::string> parsed_cert_chain =
            ParseCertChainLocked(&error);
        if (error == GRPC_ERROR_NONE) {
          PushResponseLocked(private_key_, std::move(parsed_cert_chain));
          backoff_state_.Reset();
          // Next renewal will be at the beginning of the grace period.
          // TODO(mxyan): check the expiration date of the received certificate
          // to get a more accurate time.
          next_renewal_time = ExecCtx::Get()->Now() +
                              meshca_config->certificate_lifetime() -
                              meshca_config->renewal_grace_period();
        }
      }
    }
    if (error != GRPC_ERROR_NONE) {
      // In case of error, leave the current certificate as is, and set next
      // renewal at the end of the backoff.
      next_renewal_time = backoff_state_.NextAttemptTime();
    }
    // Ref self for the timer.
    Ref().release();
    // Set the timer for next CSR with Mesh CA.
    grpc_timer_init(&renewal_timer_, next_renewal_time, &renewal_cb_);
  }
  // Release the call resources.
  grpc_call_unref(call_);
  call_ = nullptr;
  grpc_metadata_array_destroy(&initial_metadata_recv_);
  grpc_metadata_array_destroy(&trailing_metadata_recv_);
  grpc_byte_buffer_destroy(message_send_);
  grpc_byte_buffer_destroy(message_recv_);
  GRPC_ERROR_UNREF(error);
}

void GoogleMeshCaProvider::OnNextRenewal(void* arg, grpc_error* error) {
  RefCountedPtr<GoogleMeshCaProvider> self(
      static_cast<GoogleMeshCaProvider*>(arg));
  if (error == GRPC_ERROR_NONE) {
    self->OnNextRenewal();
  }
  // In case of cancel, do nothing except unref self for the timer, which is
  // handled automatically by the RefCountedPtr.
}

void GoogleMeshCaProvider::OnNextRenewal() {
  MutexLock lock(&mu_);
  if (is_shutdown_) return;
  StartCallLocked();
}

// Spawn the private key and CSR, store the private key and make the request
// byte buffer.
void GoogleMeshCaProvider::MakeKeyAndRequestLocked() {
  RefCountedPtr<GoogleMeshCaConfig> meshca_config = config();
  EVP_PKEY* keys = MakeKeys(meshca_config->key_size());
  X509_REQ* csr = MakeCsr(keys);
  char* priv_key_str = KeysToPem(keys);
  char* csr_str = CsrToPem(csr);
  std::string uuid = RandomUuid();
  // Make the MeshCertificateRequest object based on the CSR.
  upb::Arena arena;
  google_security_meshca_v1_MeshCertificateRequest* req =
      google_security_meshca_v1_MeshCertificateRequest_new(arena.ptr());
  google_security_meshca_v1_MeshCertificateRequest_set_request_id(
      req, upb_strview_makez(uuid.c_str()));
  google_security_meshca_v1_MeshCertificateRequest_set_csr(
      req, upb_strview_makez(csr_str));
  gpr_timespec validity_ts = grpc_millis_to_timespec(
      meshca_config->certificate_lifetime(), GPR_TIMESPAN);
  google_protobuf_Duration* validity =
      google_security_meshca_v1_MeshCertificateRequest_mutable_validity(
          req, arena.ptr());
  google_protobuf_Duration_set_seconds(validity, validity_ts.tv_sec);
  google_protobuf_Duration_set_nanos(validity, validity_ts.tv_nsec);
  size_t len;
  char* send_buf = google_security_meshca_v1_MeshCertificateRequest_serialize(
      req, arena.ptr(), &len);
  grpc_slice send_slice = grpc_slice_from_copied_buffer(send_buf, len);
  message_store_ = grpc_raw_byte_buffer_create(&send_slice, 1);
  grpc_slice_unref_internal(send_slice);
  // Store the private key for pushing to distributor later.
  private_key_ = priv_key_str;
  gpr_free(csr_str);
  gpr_free(priv_key_str);
  X509_REQ_free(csr);
  EVP_PKEY_free(keys);
}

std::vector<std::string> GoogleMeshCaProvider::ParseCertChainLocked(
    grpc_error** error) {
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, message_recv_);
  grpc_slice res_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_byte_buffer_reader_destroy(&bbr);
  upb::Arena arena;
  google_security_meshca_v1_MeshCertificateResponse* res =
      google_security_meshca_v1_MeshCertificateResponse_parse(
          (const char*)GRPC_SLICE_START_PTR(res_slice),
          GRPC_SLICE_LENGTH(res_slice), arena.ptr());
  if (res == nullptr) {
    grpc_slice_unref_internal(res_slice);
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Failed to parse Mesh CA response.");
    return {};
  }
  size_t size;
  const upb_strview* cert_chain =
      google_security_meshca_v1_MeshCertificateResponse_cert_chain(res, &size);
  if (size == 0) {
    grpc_slice_unref_internal(res_slice);
    *error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("No certificate in response.");
    return {};
  }
  std::vector<std::string> result;
  result.reserve(size);
  for (int i = 0; i < size; i++) {
    result.emplace_back(cert_chain[i].data, cert_chain[i].size);
  }
  grpc_slice_unref_internal(res_slice);
  return result;
}

// Update the distributor with the key-cert pair and the root certificates.
// The root certificates is the last element in the cert chain.
void GoogleMeshCaProvider::PushResponseLocked(
    const std::string& private_key, std::vector<std::string> cert_chain) {
  GPR_ASSERT(cert_chain.size() > 0);
  auto key_cert_pair = static_cast<grpc_ssl_pem_key_cert_pair*>(
      gpr_zalloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  std::stringstream ss;
  for (int i = 0; i < cert_chain.size(); i++) {
    ss << cert_chain[i];
  }
  key_cert_pair->private_key = strdup(private_key.c_str());
  key_cert_pair->cert_chain = strdup(ss.str().c_str());
  distributor()->SetKeyMaterials(cert_chain.back(),
                                 {PemKeyCertPair(key_cert_pair)});
}

class GoogleMeshCaFactory : public CertificateProviderFactory {
 private:
  const char* name() const override { return kGoogleMeshCa; }

  RefCountedPtr<CertificateProviderConfig> CreateProviderConfig(
      const Json& config_json, grpc_error** error) override {
    GoogleMeshCaConfig::Builder config_builder(config_json);
    return config_builder.Build(error);
  }

  OrphanablePtr<CertificateProvider> CreateProvider(
      RefCountedPtr<CertificateProviderConfig> config,
      RefCountedPtr<grpc_tls_certificate_distributor> distributor) override {
    return MakeOrphanable<GoogleMeshCaProvider>(std::move(config),
                                                std::move(distributor));
  }
};

void RegisterGoogleMeshCaProvider() {
  CertificateProviderRegistry::RegisterProvider(
      std::unique_ptr<CertificateProviderFactory>(new GoogleMeshCaFactory()));
}

}  // namespace grpc_core
