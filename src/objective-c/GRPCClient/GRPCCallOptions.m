/*
 *
 * Copyright 2018 gRPC authors.
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

#import "GRPCCallOptions.h"

// The default values for the call options.
static NSString *const kDefaultServerAuthority = nil;
static const NSTimeInterval kDefaultTimeout = 0;
static NSDictionary *const kDefaultInitialMetadata = nil;
static NSString *const kDefaultUserAgentPrefix = nil;
static const NSUInteger kDefaultResponseSizeLimit = 0;
static const GRPCCompressAlgorithm kDefaultCompressAlgorithm = GRPCCompressNone;
static const BOOL kDefaultEnableRetry = YES;
static const NSTimeInterval kDefaultKeepaliveInterval = 0;
static const NSTimeInterval kDefaultKeepaliveTimeout = 0;
static const NSTimeInterval kDefaultConnectMinTimeout = 0;
static const NSTimeInterval kDefaultConnectInitialBackoff = 0;
static const NSTimeInterval kDefaultConnectMaxBackoff = 0;
static NSDictionary *const kDefaultAdditionalChannelArgs = nil;
static NSString *const kDefaultPemRootCert = nil;
static NSString *const kDefaultPemPrivateKey = nil;
static NSString *const kDefaultPemCertChain = nil;
static NSString *const kDefaultOauth2AccessToken = nil;
static const id<GRPCAuthorizationProtocol> kDefaultAuthTokenProvider = nil;
static const GRPCTransportType kDefaultTransportType = GRPCTransportTypeChttp2BoringSSL;
static NSString *const kDefaultHostNameOverride = nil;
static const id kDefaultLogContext = nil;
static NSString *kDefaultChannelPoolDomain = nil;
static NSUInteger kDefaultChannelId = 0;

@implementation GRPCCallOptions {
 @protected
  NSString *_serverAuthority;
  NSTimeInterval _timeout;
  NSString *_oauth2AccessToken;
  id<GRPCAuthorizationProtocol> _authTokenProvider;
  NSDictionary *_initialMetadata;
  NSString *_userAgentPrefix;
  NSUInteger _responseSizeLimit;
  GRPCCompressAlgorithm _compressAlgorithm;
  BOOL _enableRetry;
  NSTimeInterval _keepaliveInterval;
  NSTimeInterval _keepaliveTimeout;
  NSTimeInterval _connectMinTimeout;
  NSTimeInterval _connectInitialBackoff;
  NSTimeInterval _connectMaxBackoff;
  NSDictionary *_additionalChannelArgs;
  NSString *_pemRootCert;
  NSString *_pemPrivateKey;
  NSString *_pemCertChain;
  GRPCTransportType _transportType;
  NSString *_hostNameOverride;
  id _logContext;
  NSString *_channelPoolDomain;
  NSUInteger _channelId;
}

@synthesize serverAuthority = _serverAuthority;
@synthesize timeout = _timeout;
@synthesize oauth2AccessToken = _oauth2AccessToken;
@synthesize authTokenProvider = _authTokenProvider;
@synthesize initialMetadata = _initialMetadata;
@synthesize userAgentPrefix = _userAgentPrefix;
@synthesize responseSizeLimit = _responseSizeLimit;
@synthesize compressAlgorithm = _compressAlgorithm;
@synthesize enableRetry = _enableRetry;
@synthesize keepaliveInterval = _keepaliveInterval;
@synthesize keepaliveTimeout = _keepaliveTimeout;
@synthesize connectMinTimeout = _connectMinTimeout;
@synthesize connectInitialBackoff = _connectInitialBackoff;
@synthesize connectMaxBackoff = _connectMaxBackoff;
@synthesize additionalChannelArgs = _additionalChannelArgs;
@synthesize pemRootCert = _pemRootCert;
@synthesize pemPrivateKey = _pemPrivateKey;
@synthesize pemCertChain = _pemCertChain;
@synthesize transportType = _transportType;
@synthesize hostNameOverride = _hostNameOverride;
@synthesize logContext = _logContext;
@synthesize channelPoolDomain = _channelPoolDomain;
@synthesize channelId = _channelId;

- (instancetype)init {
  return [self initWithServerAuthority:kDefaultServerAuthority
                               timeout:kDefaultTimeout
                     oauth2AccessToken:kDefaultOauth2AccessToken
                     authTokenProvider:kDefaultAuthTokenProvider
                       initialMetadata:kDefaultInitialMetadata
                       userAgentPrefix:kDefaultUserAgentPrefix
                     responseSizeLimit:kDefaultResponseSizeLimit
                     compressAlgorithm:kDefaultCompressAlgorithm
                           enableRetry:kDefaultEnableRetry
                     keepaliveInterval:kDefaultKeepaliveInterval
                      keepaliveTimeout:kDefaultKeepaliveTimeout
                     connectMinTimeout:kDefaultConnectMinTimeout
                 connectInitialBackoff:kDefaultConnectInitialBackoff
                     connectMaxBackoff:kDefaultConnectMaxBackoff
                 additionalChannelArgs:kDefaultAdditionalChannelArgs
                           pemRootCert:kDefaultPemRootCert
                         pemPrivateKey:kDefaultPemPrivateKey
                          pemCertChain:kDefaultPemCertChain
                         transportType:kDefaultTransportType
                      hostNameOverride:kDefaultHostNameOverride
                            logContext:kDefaultLogContext
                     channelPoolDomain:kDefaultChannelPoolDomain
                             channelId:kDefaultChannelId];
}

- (instancetype)initWithServerAuthority:(NSString *)serverAuthority
                                timeout:(NSTimeInterval)timeout
                      oauth2AccessToken:(NSString *)oauth2AccessToken
                      authTokenProvider:(id<GRPCAuthorizationProtocol>)authTokenProvider
                        initialMetadata:(NSDictionary *)initialMetadata
                        userAgentPrefix:(NSString *)userAgentPrefix
                      responseSizeLimit:(NSUInteger)responseSizeLimit
                      compressAlgorithm:(GRPCCompressAlgorithm)compressAlgorithm
                            enableRetry:(BOOL)enableRetry
                      keepaliveInterval:(NSTimeInterval)keepaliveInterval
                       keepaliveTimeout:(NSTimeInterval)keepaliveTimeout
                      connectMinTimeout:(NSTimeInterval)connectMinTimeout
                  connectInitialBackoff:(NSTimeInterval)connectInitialBackoff
                      connectMaxBackoff:(NSTimeInterval)connectMaxBackoff
                  additionalChannelArgs:(NSDictionary *)additionalChannelArgs
                            pemRootCert:(NSString *)pemRootCert
                          pemPrivateKey:(NSString *)pemPrivateKey
                           pemCertChain:(NSString *)pemCertChain
                          transportType:(GRPCTransportType)transportType
                       hostNameOverride:(NSString *)hostNameOverride
                             logContext:(id)logContext
                      channelPoolDomain:(NSString *)channelPoolDomain
                              channelId:(NSUInteger)channelId {
  if ((self = [super init])) {
    _serverAuthority = serverAuthority;
    _timeout = timeout;
    _oauth2AccessToken = oauth2AccessToken;
    _authTokenProvider = authTokenProvider;
    _initialMetadata = initialMetadata;
    _userAgentPrefix = userAgentPrefix;
    _responseSizeLimit = responseSizeLimit;
    _compressAlgorithm = compressAlgorithm;
    _enableRetry = enableRetry;
    _keepaliveInterval = keepaliveInterval;
    _keepaliveTimeout = keepaliveTimeout;
    _connectMinTimeout = connectMinTimeout;
    _connectInitialBackoff = connectInitialBackoff;
    _connectMaxBackoff = connectMaxBackoff;
    _additionalChannelArgs = additionalChannelArgs;
    _pemRootCert = pemRootCert;
    _pemPrivateKey = pemPrivateKey;
    _pemCertChain = pemCertChain;
    _transportType = transportType;
    _hostNameOverride = hostNameOverride;
    _logContext = logContext;
    _channelPoolDomain = channelPoolDomain;
    _channelId = channelId;
  }
  return self;
}

- (nonnull id)copyWithZone:(NSZone *)zone {
  GRPCCallOptions *newOptions =
      [[GRPCCallOptions allocWithZone:zone] initWithServerAuthority:_serverAuthority
                                                            timeout:_timeout
                                                  oauth2AccessToken:_oauth2AccessToken
                                                  authTokenProvider:_authTokenProvider
                                                    initialMetadata:_initialMetadata
                                                    userAgentPrefix:_userAgentPrefix
                                                  responseSizeLimit:_responseSizeLimit
                                                  compressAlgorithm:_compressAlgorithm
                                                        enableRetry:_enableRetry
                                                  keepaliveInterval:_keepaliveInterval
                                                   keepaliveTimeout:_keepaliveTimeout
                                                  connectMinTimeout:_connectMinTimeout
                                              connectInitialBackoff:_connectInitialBackoff
                                                  connectMaxBackoff:_connectMaxBackoff
                                              additionalChannelArgs:[_additionalChannelArgs copy]
                                                        pemRootCert:_pemRootCert
                                                      pemPrivateKey:_pemPrivateKey
                                                       pemCertChain:_pemCertChain
                                                      transportType:_transportType
                                                   hostNameOverride:_hostNameOverride
                                                         logContext:_logContext
                                                  channelPoolDomain:_channelPoolDomain
                                                          channelId:_channelId];
  return newOptions;
}

- (nonnull id)mutableCopyWithZone:(NSZone *)zone {
  GRPCMutableCallOptions *newOptions = [[GRPCMutableCallOptions allocWithZone:zone]
      initWithServerAuthority:_serverAuthority
                      timeout:_timeout
            oauth2AccessToken:_oauth2AccessToken
            authTokenProvider:_authTokenProvider
              initialMetadata:_initialMetadata
              userAgentPrefix:_userAgentPrefix
            responseSizeLimit:_responseSizeLimit
            compressAlgorithm:_compressAlgorithm
                  enableRetry:_enableRetry
            keepaliveInterval:_keepaliveInterval
             keepaliveTimeout:_keepaliveTimeout
            connectMinTimeout:_connectMinTimeout
        connectInitialBackoff:_connectInitialBackoff
            connectMaxBackoff:_connectMaxBackoff
        additionalChannelArgs:[_additionalChannelArgs copy]
                  pemRootCert:_pemRootCert
                pemPrivateKey:_pemPrivateKey
                 pemCertChain:_pemCertChain
                transportType:_transportType
             hostNameOverride:_hostNameOverride
                   logContext:_logContext
            channelPoolDomain:_channelPoolDomain
                    channelId:_channelId];
  return newOptions;
}

@end

@implementation GRPCMutableCallOptions

@dynamic serverAuthority;
@dynamic timeout;
@dynamic oauth2AccessToken;
@dynamic authTokenProvider;
@dynamic initialMetadata;
@dynamic userAgentPrefix;
@dynamic responseSizeLimit;
@dynamic compressAlgorithm;
@dynamic enableRetry;
@dynamic keepaliveInterval;
@dynamic keepaliveTimeout;
@dynamic connectMinTimeout;
@dynamic connectInitialBackoff;
@dynamic connectMaxBackoff;
@dynamic additionalChannelArgs;
@dynamic pemRootCert;
@dynamic pemPrivateKey;
@dynamic pemCertChain;
@dynamic transportType;
@dynamic hostNameOverride;
@dynamic logContext;
@dynamic channelPoolDomain;
@dynamic channelId;

- (instancetype)init {
  return [self initWithServerAuthority:kDefaultServerAuthority
                               timeout:kDefaultTimeout
                     oauth2AccessToken:kDefaultOauth2AccessToken
                     authTokenProvider:kDefaultAuthTokenProvider
                       initialMetadata:kDefaultInitialMetadata
                       userAgentPrefix:kDefaultUserAgentPrefix
                     responseSizeLimit:kDefaultResponseSizeLimit
                     compressAlgorithm:kDefaultCompressAlgorithm
                           enableRetry:kDefaultEnableRetry
                     keepaliveInterval:kDefaultKeepaliveInterval
                      keepaliveTimeout:kDefaultKeepaliveTimeout
                     connectMinTimeout:kDefaultConnectMinTimeout
                 connectInitialBackoff:kDefaultConnectInitialBackoff
                     connectMaxBackoff:kDefaultConnectMaxBackoff
                 additionalChannelArgs:kDefaultAdditionalChannelArgs
                           pemRootCert:kDefaultPemRootCert
                         pemPrivateKey:kDefaultPemPrivateKey
                          pemCertChain:kDefaultPemCertChain
                         transportType:kDefaultTransportType
                      hostNameOverride:kDefaultHostNameOverride
                            logContext:kDefaultLogContext
                     channelPoolDomain:kDefaultChannelPoolDomain
                             channelId:kDefaultChannelId];
}

- (nonnull id)copyWithZone:(NSZone *)zone {
  GRPCCallOptions *newOptions =
      [[GRPCCallOptions allocWithZone:zone] initWithServerAuthority:_serverAuthority
                                                            timeout:_timeout
                                                  oauth2AccessToken:_oauth2AccessToken
                                                  authTokenProvider:_authTokenProvider
                                                    initialMetadata:_initialMetadata
                                                    userAgentPrefix:_userAgentPrefix
                                                  responseSizeLimit:_responseSizeLimit
                                                  compressAlgorithm:_compressAlgorithm
                                                        enableRetry:_enableRetry
                                                  keepaliveInterval:_keepaliveInterval
                                                   keepaliveTimeout:_keepaliveTimeout
                                                  connectMinTimeout:_connectMinTimeout
                                              connectInitialBackoff:_connectInitialBackoff
                                                  connectMaxBackoff:_connectMaxBackoff
                                              additionalChannelArgs:[_additionalChannelArgs copy]
                                                        pemRootCert:_pemRootCert
                                                      pemPrivateKey:_pemPrivateKey
                                                       pemCertChain:_pemCertChain
                                                      transportType:_transportType
                                                   hostNameOverride:_hostNameOverride
                                                         logContext:_logContext
                                                  channelPoolDomain:_channelPoolDomain
                                                          channelId:_channelId];
  return newOptions;
}

- (nonnull id)mutableCopyWithZone:(NSZone *)zone {
  GRPCMutableCallOptions *newOptions = [[GRPCMutableCallOptions allocWithZone:zone]
      initWithServerAuthority:_serverAuthority
                      timeout:_timeout
            oauth2AccessToken:_oauth2AccessToken
            authTokenProvider:_authTokenProvider
              initialMetadata:_initialMetadata
              userAgentPrefix:_userAgentPrefix
            responseSizeLimit:_responseSizeLimit
            compressAlgorithm:_compressAlgorithm
                  enableRetry:_enableRetry
            keepaliveInterval:_keepaliveInterval
             keepaliveTimeout:_keepaliveTimeout
            connectMinTimeout:_connectMinTimeout
        connectInitialBackoff:_connectInitialBackoff
            connectMaxBackoff:_connectMaxBackoff
        additionalChannelArgs:[_additionalChannelArgs copy]
                  pemRootCert:_pemRootCert
                pemPrivateKey:_pemPrivateKey
                 pemCertChain:_pemCertChain
                transportType:_transportType
             hostNameOverride:_hostNameOverride
                   logContext:_logContext
            channelPoolDomain:_channelPoolDomain
                    channelId:_channelId];
  return newOptions;
}

- (void)setServerAuthority:(NSString *)serverAuthority {
  _serverAuthority = serverAuthority;
}

- (void)setTimeout:(NSTimeInterval)timeout {
  if (timeout < 0) {
    _timeout = 0;
  } else {
    _timeout = timeout;
  }
}

- (void)setOauth2AccessToken:(NSString *)oauth2AccessToken {
  _oauth2AccessToken = oauth2AccessToken;
}

- (void)setAuthTokenProvider:(id<GRPCAuthorizationProtocol>)authTokenProvider {
  _authTokenProvider = authTokenProvider;
}

- (void)setInitialMetadata:(NSDictionary *)initialMetadata {
  _initialMetadata = initialMetadata;
}

- (void)setUserAgentPrefix:(NSString *)userAgentPrefix {
  _userAgentPrefix = userAgentPrefix;
}

- (void)setResponseSizeLimit:(NSUInteger)responseSizeLimit {
  _responseSizeLimit = responseSizeLimit;
}

- (void)setCompressAlgorithm:(GRPCCompressAlgorithm)compressAlgorithm {
  _compressAlgorithm = compressAlgorithm;
}

- (void)setEnableRetry:(BOOL)enableRetry {
  _enableRetry = enableRetry;
}

- (void)setKeepaliveInterval:(NSTimeInterval)keepaliveInterval {
  if (keepaliveInterval < 0) {
    _keepaliveInterval = 0;
  } else {
    _keepaliveInterval = keepaliveInterval;
  }
}

- (void)setKeepaliveTimeout:(NSTimeInterval)keepaliveTimeout {
  if (keepaliveTimeout < 0) {
    _keepaliveTimeout = 0;
  } else {
    _keepaliveTimeout = keepaliveTimeout;
  }
}

- (void)setConnectMinTimeout:(NSTimeInterval)connectMinTimeout {
  if (connectMinTimeout < 0) {
    connectMinTimeout = 0;
  } else {
    _connectMinTimeout = connectMinTimeout;
  }
}

- (void)setConnectInitialBackoff:(NSTimeInterval)connectInitialBackoff {
  if (connectInitialBackoff < 0) {
    _connectInitialBackoff = 0;
  } else {
    _connectInitialBackoff = connectInitialBackoff;
  }
}

- (void)setConnectMaxBackoff:(NSTimeInterval)connectMaxBackoff {
  if (connectMaxBackoff < 0) {
    _connectMaxBackoff = 0;
  } else {
    _connectMaxBackoff = connectMaxBackoff;
  }
}

- (void)setAdditionalChannelArgs:(NSDictionary *)additionalChannelArgs {
  _additionalChannelArgs = additionalChannelArgs;
}

- (void)setPemRootCert:(NSString *)pemRootCert {
  _pemRootCert = pemRootCert;
}

- (void)setPemPrivateKey:(NSString *)pemPrivateKey {
  _pemPrivateKey = pemPrivateKey;
}

- (void)setPemCertChain:(NSString *)pemCertChain {
  _pemCertChain = pemCertChain;
}

- (void)setTransportType:(GRPCTransportType)transportType {
  _transportType = transportType;
}

- (void)setHostNameOverride:(NSString *)hostNameOverride {
  _hostNameOverride = hostNameOverride;
}

- (void)setLogContext:(id)logContext {
  _logContext = logContext;
}

- (void)setChannelPoolDomain:(NSString *)channelPoolDomain {
  _channelPoolDomain = channelPoolDomain;
}

- (void)setChannelId:(NSUInteger)channelId {
  _channelId = channelId;
}

@end
