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

#ifndef GRPCPP_SECURITY_CREDENTIALS_H
#define GRPCPP_SECURITY_CREDENTIALS_H

#include <map>
#include <memory>

#include <grpcpp/impl/codegen/client_context.h>
#include <grpcpp/impl/codegen/grpc_library.h>
#include <grpcpp/security/auth_context.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/string_ref.h>

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
/// This is a base class that will only get constructed from its derived
/// classes in the implementation.
///
/// \see https://grpc.io/docs/guides/auth.html
class ChannelCredentials : private internal::GrpcLibraryCodegen {
 protected:
  ChannelCredentials();
  ~ChannelCredentials();

 private:
  friend std::shared_ptr<Channel> CreateCustomChannel(
      const grpc::string& target,
      const std::shared_ptr<ChannelCredentials>& creds,
      const ChannelArguments& args);

  friend std::shared_ptr<ChannelCredentials> CompositeChannelCredentials(
      const std::shared_ptr<ChannelCredentials>& channel_creds,
      const std::shared_ptr<CallCredentials>& call_creds);

  /// Either give this call credentials if secure or nullptr if not
  /// (like a dynamic_cast). Only called by CompositeChannelCredentials
  virtual SecureChannelCredentials* AsSecureCredentials() = 0;

  virtual std::shared_ptr<Channel> CreateChannel(
      const grpc::string& target, const ChannelArguments& args) = 0;
};

/// A call credentials object encapsulates the state needed by a client to
/// authenticate with a server for a given call on a channel.
/// This is a base class that will only get constructed from its derived
/// classes in the implementation.
///
/// \see https://grpc.io/docs/guides/auth.html
class CallCredentials : private internal::GrpcLibraryCodegen {
 protected:
  CallCredentials();
  ~CallCredentials();

 private:
  friend class ClientContext;

  friend std::shared_ptr<ChannelCredentials> CompositeChannelCredentials(
      const std::shared_ptr<ChannelCredentials>& channel_creds,
      const std::shared_ptr<CallCredentials>& call_creds);

  friend std::shared_ptr<CallCredentials> CompositeCallCredentials(
      const std::shared_ptr<CallCredentials>& creds1,
      const std::shared_ptr<CallCredentials>& creds2);

  /// Apply this instance's credentials to call in \a ctx.
  /// Applications should not need to call this; this will be called by
  /// the ClientContext when it gets bound to a call.
  virtual bool ApplyToCall(ClientContext* ctx) = 0;

  /// Either give this call credentials if secure or nullptr if not
  /// (like a dynamic_cast). Only called by the CompositeCredentials functions.
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

/// Constant for maximum auth token lifetime.
constexpr long kMaxAuthTokenLifetimeSecs = 3600;

/// Builds Service Account JWT Access credentials.
/// json_key is the JSON key string containing the client's private key.
/// token_lifetime_seconds is the lifetime in seconds of each Json Web Token
/// (JWT) created with this credentials. It should not exceed
/// \a kMaxAuthTokenLifetimeSecs or will be cropped to this value.
std::shared_ptr<CallCredentials> ServiceAccountJWTAccessCredentials(
    const grpc::string& json_key,
    long token_lifetime_seconds = kMaxAuthTokenLifetimeSecs);

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

/// User defined metadata credentials.
class MetadataCredentialsPlugin {
 public:
  virtual ~MetadataCredentialsPlugin() {}

  /// If this method returns true, the Process function will be scheduled in
  /// a different thread from the one processing the call.
  virtual bool IsBlocking() const { return true; }

  /// Type of credentials this plugin is implementing.
  virtual const char* GetType() const { return ""; }

  /// Gets the auth metatada produced by this plugin.
  /// The fully qualified method name is:
  /// service_url + "/" + method_name.
  /// The channel_auth_context contains (among other things), the identity of
  /// the server.
  virtual Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const AuthContext& channel_auth_context,
      std::multimap<grpc::string, grpc::string>* metadata) = 0;
};

std::shared_ptr<CallCredentials> MetadataCredentialsFromPlugin(
    std::unique_ptr<MetadataCredentialsPlugin> plugin);

}  // namespace grpc

#endif  // GRPCPP_SECURITY_CREDENTIALS_H
