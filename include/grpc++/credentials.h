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

#ifndef GRPCXX_CREDENTIALS_H
#define GRPCXX_CREDENTIALS_H

#include <chrono>
#include <memory>

#include <grpc++/config.h>

struct grpc_credentials;

namespace grpc {

// grpc_credentials wrapper class. Typical use in C++ applications is limited
// to creating an instance using CredentialsFactory, and passing it down
// during channel construction.

class Credentials GRPC_FINAL {
 public:
  ~Credentials();

  // TODO(abhikumar): Specify a plugin API here to be implemented by
  // credentials that do not have a corresponding implementation in C.

 private:
  explicit Credentials(grpc_credentials*);
  grpc_credentials* GetRawCreds();

  friend class Channel;
  friend class CredentialsFactory;

  grpc_credentials* creds_;
};

// Options used to build SslCredentials
// pem_roots_cert is the buffer containing the PEM encoding of the server root
// certificates. If this parameter is empty, the default roots will be used.
// pem_private_key is the buffer containing the PEM encoding of the client's
// private key. This parameter can be empty if the client does not have a
// private key.
// pem_cert_chain is the buffer containing the PEM encoding of the client's
// certificate chain. This parameter can be empty if the client does not have
// a certificate chain.
struct SslCredentialsOptions {
  grpc::string pem_root_certs;
  grpc::string pem_private_key;
  grpc::string pem_cert_chain;
};

// Factory for building different types of Credentials
// The methods may return empty unique_ptr when credentials cannot be created.
// If a Credentials pointer is returned, it can still be invalid when used to
// create a channel. A lame channel will be created then and all rpcs will
// fail on it.
class CredentialsFactory {
 public:
  // Builds google credentials with reasonable defaults.
  // WARNING: Do NOT use this credentials to connect to a non-google service as
  // this could result in an oauth2 token leak.
  static std::unique_ptr<Credentials> GoogleDefaultCredentials();

  // Builds SSL Credentials given SSL specific options
  static std::unique_ptr<Credentials> SslCredentials(
      const SslCredentialsOptions& options);

  // Builds credentials for use when running in GCE
  // WARNING: Do NOT use this credentials to connect to a non-google service as
  // this could result in an oauth2 token leak.
  static std::unique_ptr<Credentials> ComputeEngineCredentials();

  // Builds service account credentials.
  // WARNING: Do NOT use this credentials to connect to a non-google service as
  // this could result in an oauth2 token leak.
  // json_key is the JSON key string containing the client's private key.
  // scope is a space-delimited list of the requested permissions.
  // token_lifetime is the lifetime of each token acquired through this service
  // account credentials. It should be positive and should not exceed
  // grpc_max_auth_token_lifetime or will be cropped to this value.
  static std::unique_ptr<Credentials> ServiceAccountCredentials(
      const grpc::string& json_key, const grpc::string& scope,
      std::chrono::seconds token_lifetime);

  // Builds JWT credentials.
  // json_key is the JSON key string containing the client's private key.
  // token_lifetime is the lifetime of each Json Web Token (JWT) created with
  // this credentials.  It should not exceed grpc_max_auth_token_lifetime or
  // will be cropped to this value.
  static std::unique_ptr<Credentials> JWTCredentials(
      const grpc::string& json_key, std::chrono::seconds token_lifetime);

  // Builds IAM credentials.
  static std::unique_ptr<Credentials> IAMCredentials(
      const grpc::string& authorization_token,
      const grpc::string& authority_selector);

  // Combines two credentials objects into a composite credentials
  static std::unique_ptr<Credentials> CompositeCredentials(
      const std::unique_ptr<Credentials>& creds1,
      const std::unique_ptr<Credentials>& creds2);
};

}  // namespace grpc

#endif  // GRPCXX_CREDENTIALS_H
