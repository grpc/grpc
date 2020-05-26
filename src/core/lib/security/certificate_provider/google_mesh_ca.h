#ifndef GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_GOOGLE_MESH_CA_H
#define GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_GOOGLE_MESH_CA_H

#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/security/certificate_provider/config.h"
#include "src/core/lib/security/certificate_provider/provider.h"

namespace grpc_core {

class GoogleMeshCaConfig : public CertificateProviderConfig {
 public:
  struct StsConfig {
    std::string token_exchange_service_uri;
    std::string subject_token_path;
    std::string subject_token_type;
  };

  // Builder parses the configuration json.
  class Builder {
   public:
    Builder(const Json& config_json);
    ~Builder();

    RefCountedPtr<GoogleMeshCaConfig> Build(grpc_error** error);

   private:
    grpc_error* Validate();

    grpc_error* error_ = GRPC_ERROR_NONE;
    RefCountedPtr<GoogleMeshCaConfig> config_ = nullptr;
  };

  GoogleMeshCaConfig(const Json& config_json);

  const char* name() const override;

  const std::string& endpoint() const { return endpoint_; }
  const StsConfig& sts_config() const { return sts_config_; }
  grpc_millis rpc_timeout() const { return rpc_timeout_; }
  grpc_millis certificate_lifetime() const { return certificate_lifetime_; }
  grpc_millis renewal_grace_period() const { return renewal_grace_period_; }
  const std::string& key_type() const { return key_type_; }
  uint32_t key_size() const { return key_size_; }
  const std::string& gce_compute_zone() const { return gce_compute_zone_; }

 private:
  std::string endpoint_;
  StsConfig sts_config_;
  grpc_millis rpc_timeout_ = 10 * GPR_MS_PER_SEC;
  grpc_millis certificate_lifetime_ =
      1 * 86400 /* SEC_PER_DAY */ * GPR_MS_PER_SEC;
  grpc_millis renewal_grace_period_ =
      12 * 3600 /* SEC_PER_HOUR*/ * GPR_MS_PER_SEC;
  std::string key_type_ = "KEY_TYPE_RSA";
  uint32_t key_size_ = 2048;
  std::string gce_compute_zone_;
};

class GoogleMeshCaProvider : public CertificateProvider {
 public:
  GoogleMeshCaProvider(
      RefCountedPtr<GoogleMeshCaConfig> config,
      RefCountedPtr<grpc_tls_certificate_distributor> distributor);

  // Allows overriding credentials for testing
  GoogleMeshCaProvider(
      RefCountedPtr<GoogleMeshCaConfig> config,
      RefCountedPtr<grpc_tls_certificate_distributor> distributor,
      grpc_channel_credentials* channel_creds);

  ~GoogleMeshCaProvider();

  void Orphan() override;

 private:
  static void InitClient(void* arg, grpc_error* /*error*/);

  void InitClient();

  // Starts a call to the Mesh CA. There must not be a pending call. mu_ must be
  // held when calling this method.
  void StartCallLocked();

  static void OnCallComplete(void* arg, grpc_error* error);

  // Certificate sign/renewal is considered failed in either of the following
  // cases: 1) error != GRPC_ERROR_NONE. 2) Call status (status_) is not
  // GRPC_STATUS_OK. 3) Error encountered when parsing the signed certificate.
  // In cases 1 and 2, the same request should be retried. In case 3, a retry
  // with a new request should be issued.
  void OnCallComplete(grpc_error* error);

  static void OnNextRenewal(void* arg, grpc_error* error);

  void OnNextRenewal();

  // Spawn the private key and CSR, store the private key and make the request
  // byte buffer.
  void MakeKeyAndRequestLocked();

  std::vector<std::string> ParseCertChainLocked(grpc_error** error);

  // Update the distributor with the key-cert pair and the root certificates.
  // The root certificates is the last element in the cert chain.
  void PushResponseLocked(const std::string& private_key,
                          std::vector<std::string> cert_chain);

  bool is_shutdown_ = false;
  Mutex mu_;
  grpc_closure init_client_cb_;
  grpc_closure call_complete_cb_;
  // Closure for certificate renewal
  grpc_closure renewal_cb_;
  // Timer to trigger certificate renewal
  grpc_timer renewal_timer_;

  // Private key used for the current pending CSR.
  std::string private_key_ = "";

  // States for the CSR call
  grpc_channel* channel_ = nullptr;
  grpc_channel_credentials* channel_creds_ = nullptr;
  grpc_call* call_ = nullptr;
  BackOff backoff_state_;
  grpc_metadata_array initial_metadata_recv_;
  grpc_metadata_array trailing_metadata_recv_;
  grpc_byte_buffer* message_store_ = nullptr;
  grpc_byte_buffer* message_send_ = nullptr;
  grpc_byte_buffer* message_recv_ = nullptr;
  grpc_status_code status_ = GRPC_STATUS_OK;
  grpc_slice status_details_;
};

void RegisterGoogleMeshCaProvider();

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_GOOGLE_MESH_CA_H
