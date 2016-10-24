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

#ifndef GRPCXX_SECURITY_CREDENTIALS_H
#define GRPCXX_SECURITY_CREDENTIALS_H

#include <map>
#include <memory>

#include <grpc++/impl/codegen/grpc_library.h>
#include <grpc++/security/auth_context.h>
#include <grpc++/support/status.h>
#include <grpc++/support/string_ref.h>

struct grpc_call;

namespace grpc {
class ChannelArguments;
class Channel;
class SecureChannelCredentials;
class CallCredentials;
class SecureCallCredentials;

/// A channel credentials object encapsulates all the state needed by a client
/// to authenticate with a server for a given channel.
/// It can make various assertions, e.g., about the client’s identity, role
/// for all the calls on that channel.
///
/// \see http://www.grpc.io/docs/guides/auth.html
class ChannelCredentials : private GrpcLibraryCodegen {
 public:
  ChannelCredentials();
  ~ChannelCredentials();

 protected:
  friend std::shared_ptr<ChannelCredentials> CompositeChannelCredentials(
      const std::shared_ptr<ChannelCredentials>& channel_creds,
      const std::shared_ptr<CallCredentials>& call_creds);

  virtual SecureChannelCredentials* AsSecureCredentials() = 0;

 private:
  friend std::shared_ptr<Channel> CreateCustomChannel(
      const grpc::string& target,
      const std::shared_ptr<ChannelCredentials>& creds,
      const ChannelArguments& args);

  virtual std::shared_ptr<Channel> CreateChannel(
      const grpc::string& target, const ChannelArguments& args) = 0;
};

/// A call credentials object encapsulates the state needed by a client to
/// authenticate with a server for a given call on a channel.
///
/// \see http://www.grpc.io/docs/guides/auth.html
class CallCredentials : private GrpcLibraryCodegen {
 public:
  CallCredentials();
  ~CallCredentials();

  /// Apply this instance's credentials to \a call.
  virtual bool ApplyToCall(grpc_call* call) = 0;

 protected:
  friend std::shared_ptr<ChannelCredentials> CompositeChannelCredentials(
      const std::shared_ptr<ChannelCredentials>& channel_creds,
      const std::shared_ptr<CallCredentials>& call_creds);

  friend std::shared_ptr<CallCredentials> CompositeCallCredentials(
      const std::shared_ptr<CallCredentials>& creds1,
      const std::shared_ptr<CallCredentials>& creds2);

  virtual SecureCallCredentials* AsSecureCredentials() = 0;
};

/// Options used to build SslCredentials.
struct SslCredentialsOptions {
  /// The buffer containing the PEM encoding of the server root certificates. If
  /// this parameter is empty, the default roots will be used.  The default
  /// roots can be overridden using the \a GRPC_DEFAULT_SSL_ROOTS_FILE_PATH
  /// environment variable pointing to a file on the file system containing the
  /// roots.
  grpc::string pem_root_certs;

  /// The buffer containing the PEM encoding of the client's private key. This
  /// parameter can be empty if the client does not have a private key.
  grpc::string pem_private_key;

  /// The buffer containing the PEM encoding of the client's certificate chain.
  /// This parameter can be empty if the client does not have a certificate
  /// chain.
  grpc::string pem_cert_chain;
};

// Factories for building different types of Credentials The functions may
// return empty shared_ptr when credentials cannot be created. If a
// Credentials pointer is returned, it can still be invalid when used to create
// a channel. A lame channel will be created then and all rpcs will fail on it.

/// Builds credentials with reasonable defaults.
///
/// \warning Only use these credentials when connecting to a Google endpoint.
/// Using these credentials to connect to any other service may result in this
/// service being able to impersonate your client for requests to Google
/// services.
std::shared_ptr<ChannelCredentials> GoogleDefaultCredentials();

/// Builds SSL Credentials given SSL specific options
std::shared_ptr<ChannelCredentials> SslCredentials(
    const SslCredentialsOptions& options);

/// Builds credentials for use when running in GCE
///
/// \warning Only use these credentials when connecting to a Google endpoint.
/// Using these credentials to connect to any other service may result in this
/// service being able to impersonate your client for requests to Google
/// services.
std::shared_ptr<CallCredentials> GoogleComputeEngineCredentials();

/// Builds Service Account JWT Access credentials.
/// json_key is the JSON key string containing the client's private key.
/// token_lifetime_seconds is the lifetime in seconds of each Json Web Token
/// (JWT) created with this credentials. It should not exceed
/// grpc_max_auth_token_lifetime or will be cropped to this value.
std::shared_ptr<CallCredentials> ServiceAccountJWTAccessCredentials(
    const grpc::string& json_key, long token_lifetime_seconds);

/// Builds refresh token credentials.
/// json_refresh_token is the JSON string containing the refresh token along
/// with a client_id and client_secret.
///
/// \warning Only use these credentials when connecting to a Google endpoint.
/// Using these credentials to connect to any other service may result in this
/// service being able to impersonate your client for requests to Google
/// services.
std::shared_ptr<CallCredentials> GoogleRefreshTokenCredentials(
    const grpc::string& json_refresh_token);

/// Builds access token credentials.
/// access_token is an oauth2 access token that was fetched using an out of band
/// mechanism.
///
/// \warning Only use these credentials when connecting to a Google endpoint.
/// Using these credentials to connect to any other service may result in this
/// service being able to impersonate your client for requests to Google
/// services.
std::shared_ptr<CallCredentials> AccessTokenCredentials(
    const grpc::string& access_token);

/// Builds IAM credentials.
///
/// \warning Only use these credentials when connecting to a Google endpoint.
/// Using these credentials to connect to any other service may result in this
/// service being able to impersonate your client for requests to Google
/// services.
std::shared_ptr<CallCredentials> GoogleIAMCredentials(
    const grpc::string& authorization_token,
    const grpc::string& authority_selector);

/// Combines a channel credentials and a call credentials into a composite
/// channel credentials.
std::shared_ptr<ChannelCredentials> CompositeChannelCredentials(
    const std::shared_ptr<ChannelCredentials>& channel_creds,
    const std::shared_ptr<CallCredentials>& call_creds);

/// Combines two call credentials objects into a composite call credentials.
std::shared_ptr<CallCredentials> CompositeCallCredentials(
    const std::shared_ptr<CallCredentials>& creds1,
    const std::shared_ptr<CallCredentials>& creds2);

/// Credentials for an unencrypted, unauthenticated channel
std::shared_ptr<ChannelCredentials> InsecureChannelCredentials();

/// Credentials for a channel using Cronet.
std::shared_ptr<ChannelCredentials> CronetChannelCredentials(void* engine);

// User defined metadata credentials.
class MetadataCredentialsPlugin {
 public:
  virtual ~MetadataCredentialsPlugin() {}

  // If this method returns true, the Process function will be scheduled in
  // a different thread from the one processing the call.
  virtual bool IsBlocking() const { return true; }

  // Type of credentials this plugin is implementing.
  virtual const char* GetType() const { return ""; }

  // Gets the auth metatada produced by this plugin.
  // The fully qualified method name is:
  // service_url + "/" + method_name.
  // The channel_auth_context contains (among other things), the identity of
  // the server.
  virtual Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const AuthContext& channel_auth_context,
      std::multimap<grpc::string, grpc::string>* metadata) = 0;
};

std::shared_ptr<CallCredentials> MetadataCredentialsFromPlugin(
    std::unique_ptr<MetadataCredentialsPlugin> plugin);

}  // namespace grpc

#endif  // GRPCXX_SECURITY_CREDENTIALS_H
