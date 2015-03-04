/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <string>

#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include <grpc++/credentials.h>

namespace grpc {

Credentials::Credentials(grpc_credentials *c_creds) : creds_(c_creds) {}

Credentials::~Credentials() { grpc_credentials_release(creds_); }
grpc_credentials *Credentials::GetRawCreds() { return creds_; }

std::unique_ptr<Credentials> CredentialsFactory::GoogleDefaultCredentials() {
  grpc_credentials *c_creds = grpc_google_default_credentials_create();
  std::unique_ptr<Credentials> cpp_creds(
      c_creds == nullptr ? nullptr : new Credentials(c_creds));
  return cpp_creds;
}

// Builds SSL Credentials given SSL specific options
std::unique_ptr<Credentials> CredentialsFactory::SslCredentials(
    const SslCredentialsOptions &options) {
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {
      options.pem_private_key.c_str(), options.pem_cert_chain.c_str()};

  grpc_credentials *c_creds = grpc_ssl_credentials_create(
      options.pem_root_certs.empty() ? nullptr : options.pem_root_certs.c_str(),
      options.pem_private_key.empty() ? nullptr : &pem_key_cert_pair);
  std::unique_ptr<Credentials> cpp_creds(
      c_creds == nullptr ? nullptr : new Credentials(c_creds));
  return cpp_creds;
}

// Builds credentials for use when running in GCE
std::unique_ptr<Credentials> CredentialsFactory::ComputeEngineCredentials() {
  grpc_credentials *c_creds = grpc_compute_engine_credentials_create();
  std::unique_ptr<Credentials> cpp_creds(
      c_creds == nullptr ? nullptr : new Credentials(c_creds));
  return cpp_creds;
}

// Builds service account credentials.
std::unique_ptr<Credentials> CredentialsFactory::ServiceAccountCredentials(
    const grpc::string &json_key, const grpc::string &scope,
    std::chrono::seconds token_lifetime) {
  gpr_timespec lifetime = gpr_time_from_seconds(
      token_lifetime.count() > 0 ? token_lifetime.count() : 0);
  grpc_credentials *c_creds = grpc_service_account_credentials_create(
      json_key.c_str(), scope.c_str(), lifetime);
  std::unique_ptr<Credentials> cpp_creds(
      c_creds == nullptr ? nullptr : new Credentials(c_creds));
  return cpp_creds;
}

// Builds JWT credentials.
std::unique_ptr<Credentials> CredentialsFactory::JWTCredentials(
    const grpc::string &json_key, std::chrono::seconds token_lifetime) {
  gpr_timespec lifetime = gpr_time_from_seconds(
      token_lifetime.count() > 0 ? token_lifetime.count() : 0);
  grpc_credentials *c_creds =
      grpc_jwt_credentials_create(json_key.c_str(), lifetime);
  std::unique_ptr<Credentials> cpp_creds(
      c_creds == nullptr ? nullptr : new Credentials(c_creds));
  return cpp_creds;
}

// Builds IAM credentials.
std::unique_ptr<Credentials> CredentialsFactory::IAMCredentials(
    const grpc::string &authorization_token,
    const grpc::string &authority_selector) {
  grpc_credentials *c_creds = grpc_iam_credentials_create(
      authorization_token.c_str(), authority_selector.c_str());
  std::unique_ptr<Credentials> cpp_creds(
      c_creds == nullptr ? nullptr : new Credentials(c_creds));
  return cpp_creds;
}

// Combines two credentials objects into a composite credentials.
std::unique_ptr<Credentials> CredentialsFactory::CompositeCredentials(
    const std::unique_ptr<Credentials> &creds1,
    const std::unique_ptr<Credentials> &creds2) {
  // Note that we are not saving unique_ptrs to the two credentials
  // passed in here. This is OK because the underlying C objects (i.e.,
  // creds1 and creds2) into grpc_composite_credentials_create will see their
  // refcounts incremented.
  grpc_credentials *c_creds = grpc_composite_credentials_create(
      creds1->GetRawCreds(), creds2->GetRawCreds());
  std::unique_ptr<Credentials> cpp_creds(
      c_creds == nullptr ? nullptr : new Credentials(c_creds));
  return cpp_creds;
}

}  // namespace grpc
