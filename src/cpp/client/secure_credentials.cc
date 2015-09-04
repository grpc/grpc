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

#include <grpc/support/log.h>
#include <grpc++/channel.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc++/support/channel_arguments.h>
#include "src/cpp/client/create_channel_internal.h"
#include "src/cpp/client/secure_credentials.h"

namespace grpc {

std::shared_ptr<grpc::Channel> SecureCredentials::CreateChannel(
    const string& target, const grpc::ChannelArguments& args) {
  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);
  return CreateChannelInternal(
      args.GetSslTargetNameOverride(),
      grpc_secure_channel_create(c_creds_, target.c_str(), &channel_args,
                                 nullptr));
}

bool SecureCredentials::ApplyToCall(grpc_call* call) {
  return grpc_call_set_credentials(call, c_creds_) == GRPC_CALL_OK;
}

namespace {
std::shared_ptr<Credentials> WrapCredentials(grpc_credentials* creds) {
  return creds == nullptr
             ? nullptr
             : std::shared_ptr<Credentials>(new SecureCredentials(creds));
}
}  // namespace

std::shared_ptr<Credentials> GoogleDefaultCredentials() {
  GrpcLibrary init;  // To call grpc_init().
  return WrapCredentials(grpc_google_default_credentials_create());
}

// Builds SSL Credentials given SSL specific options
std::shared_ptr<Credentials> SslCredentials(
    const SslCredentialsOptions& options) {
  GrpcLibrary init;  // To call grpc_init().
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {
      options.pem_private_key.c_str(), options.pem_cert_chain.c_str()};

  grpc_credentials* c_creds = grpc_ssl_credentials_create(
      options.pem_root_certs.empty() ? nullptr : options.pem_root_certs.c_str(),
      options.pem_private_key.empty() ? nullptr : &pem_key_cert_pair, nullptr);
  return WrapCredentials(c_creds);
}

// Builds credentials for use when running in GCE
std::shared_ptr<Credentials> GoogleComputeEngineCredentials() {
  GrpcLibrary init;  // To call grpc_init().
  return WrapCredentials(
      grpc_google_compute_engine_credentials_create(nullptr));
}

// Builds JWT credentials.
std::shared_ptr<Credentials> ServiceAccountJWTAccessCredentials(
    const grpc::string& json_key, long token_lifetime_seconds) {
  GrpcLibrary init;  // To call grpc_init().
  if (token_lifetime_seconds <= 0) {
    gpr_log(GPR_ERROR,
            "Trying to create JWTCredentials with non-positive lifetime");
    return WrapCredentials(nullptr);
  }
  gpr_timespec lifetime =
      gpr_time_from_seconds(token_lifetime_seconds, GPR_TIMESPAN);
  return WrapCredentials(grpc_service_account_jwt_access_credentials_create(
      json_key.c_str(), lifetime, nullptr));
}

// Builds refresh token credentials.
std::shared_ptr<Credentials> GoogleRefreshTokenCredentials(
    const grpc::string& json_refresh_token) {
  GrpcLibrary init;  // To call grpc_init().
  return WrapCredentials(grpc_google_refresh_token_credentials_create(
      json_refresh_token.c_str(), nullptr));
}

// Builds access token credentials.
std::shared_ptr<Credentials> AccessTokenCredentials(
    const grpc::string& access_token) {
  GrpcLibrary init;  // To call grpc_init().
  return WrapCredentials(
      grpc_access_token_credentials_create(access_token.c_str(), nullptr));
}

// Builds IAM credentials.
std::shared_ptr<Credentials> GoogleIAMCredentials(
    const grpc::string& authorization_token,
    const grpc::string& authority_selector) {
  GrpcLibrary init;  // To call grpc_init().
  return WrapCredentials(grpc_google_iam_credentials_create(
      authorization_token.c_str(), authority_selector.c_str(), nullptr));
}

// Combines two credentials objects into a composite credentials.
std::shared_ptr<Credentials> CompositeCredentials(
    const std::shared_ptr<Credentials>& creds1,
    const std::shared_ptr<Credentials>& creds2) {
  // Note that we are not saving shared_ptrs to the two credentials
  // passed in here. This is OK because the underlying C objects (i.e.,
  // creds1 and creds2) into grpc_composite_credentials_create will see their
  // refcounts incremented.
  SecureCredentials* s1 = creds1->AsSecureCredentials();
  SecureCredentials* s2 = creds2->AsSecureCredentials();
  if (s1 && s2) {
    return WrapCredentials(grpc_composite_credentials_create(
        s1->GetRawCreds(), s2->GetRawCreds(), nullptr));
  }
  return nullptr;
}

}  // namespace grpc
