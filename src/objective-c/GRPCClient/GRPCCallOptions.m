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
static const GRPCCompressionAlgorithm kDefaultCompressionAlgorithm = GRPCCompressNone;
static const BOOL kDefaultEnableRetry = YES;
static const NSTimeInterval kDefaultKeepaliveInterval = 0;
static const NSTimeInterval kDefaultKeepaliveTimeout = 0;
static const NSTimeInterval kDefaultConnectMinTimeout = 0;
static const NSTimeInterval kDefaultConnectInitialBackoff = 0;
static const NSTimeInterval kDefaultConnectMaxBackoff = 0;
static NSDictionary *const kDefaultAdditionalChannelArgs = nil;
static NSString *const kDefaultPEMRootCertificates = nil;
static NSString *const kDefaultPEMPrivateKey = nil;
static NSString *const kDefaultPEMCertChain = nil;
static NSString *const kDefaultOauth2AccessToken = nil;
static const id<GRPCAuthorizationProtocol> kDefaultAuthTokenProvider = nil;
static const GRPCTransportType kDefaultTransportType = GRPCTransportTypeChttp2BoringSSL;
static NSString *const kDefaultHostNameOverride = nil;
static const id kDefaultLogContext = nil;
static NSString *kDefaultChannelPoolDomain = nil;
static NSUInteger kDefaultChannelID = 0;

@implementation GRPCCallOptions {
 @protected
  NSString *_serverAuthority;
  NSTimeInterval _timeout;
  NSString *_oauth2AccessToken;
  id<GRPCAuthorizationProtocol> _authTokenProvider;
  NSDictionary *_initialMetadata;
  NSString *_userAgentPrefix;
  NSUInteger _responseSizeLimit;
  GRPCCompressionAlgorithm _compressionAlgorithm;
  BOOL _enableRetry;
  NSTimeInterval _keepaliveInterval;
  NSTimeInterval _keepaliveTimeout;
  NSTimeInterval _connectMinTimeout;
  NSTimeInterval _connectInitialBackoff;
  NSTimeInterval _connectMaxBackoff;
  NSDictionary *_additionalChannelArgs;
  NSString *_PEMRootCertificates;
  NSString *_PEMPrivateKey;
  NSString *_PEMCertChain;
  GRPCTransportType _transportType;
  NSString *_hostNameOverride;
  id _logContext;
  NSString *_channelPoolDomain;
  NSUInteger _channelID;
}

@synthesize serverAuthority = _serverAuthority;
@synthesize timeout = _timeout;
@synthesize oauth2AccessToken = _oauth2AccessToken;
@synthesize authTokenProvider = _authTokenProvider;
@synthesize initialMetadata = _initialMetadata;
@synthesize userAgentPrefix = _userAgentPrefix;
@synthesize responseSizeLimit = _responseSizeLimit;
@synthesize compressionAlgorithm = _compressionAlgorithm;
@synthesize enableRetry = _enableRetry;
@synthesize keepaliveInterval = _keepaliveInterval;
@synthesize keepaliveTimeout = _keepaliveTimeout;
@synthesize connectMinTimeout = _connectMinTimeout;
@synthesize connectInitialBackoff = _connectInitialBackoff;
@synthesize connectMaxBackoff = _connectMaxBackoff;
@synthesize additionalChannelArgs = _additionalChannelArgs;
@synthesize PEMRootCertificates = _PEMRootCertificates;
@synthesize PEMPrivateKey = _PEMPrivateKey;
@synthesize PEMCertChain = _PEMCertChain;
@synthesize transportType = _transportType;
@synthesize hostNameOverride = _hostNameOverride;
@synthesize logContext = _logContext;
@synthesize channelPoolDomain = _channelPoolDomain;
@synthesize channelID = _channelID;

- (instancetype)init {
  return [self initWithServerAuthority:kDefaultServerAuthority
                               timeout:kDefaultTimeout
                     oauth2AccessToken:kDefaultOauth2AccessToken
                     authTokenProvider:kDefaultAuthTokenProvider
                       initialMetadata:kDefaultInitialMetadata
                       userAgentPrefix:kDefaultUserAgentPrefix
                     responseSizeLimit:kDefaultResponseSizeLimit
                     compressionAlgorithm:kDefaultCompressionAlgorithm
                           enableRetry:kDefaultEnableRetry
                     keepaliveInterval:kDefaultKeepaliveInterval
                      keepaliveTimeout:kDefaultKeepaliveTimeout
                     connectMinTimeout:kDefaultConnectMinTimeout
                 connectInitialBackoff:kDefaultConnectInitialBackoff
                     connectMaxBackoff:kDefaultConnectMaxBackoff
                 additionalChannelArgs:kDefaultAdditionalChannelArgs
                   PEMRootCertificates:kDefaultPEMRootCertificates
                         PEMPrivateKey:kDefaultPEMPrivateKey
                          PEMCertChain:kDefaultPEMCertChain
                         transportType:kDefaultTransportType
                      hostNameOverride:kDefaultHostNameOverride
                            logContext:kDefaultLogContext
                     channelPoolDomain:kDefaultChannelPoolDomain
                             channelID:kDefaultChannelID];
}

- (instancetype)initWithServerAuthority:(NSString *)serverAuthority
                                timeout:(NSTimeInterval)timeout
                      oauth2AccessToken:(NSString *)oauth2AccessToken
                      authTokenProvider:(id<GRPCAuthorizationProtocol>)authTokenProvider
                        initialMetadata:(NSDictionary *)initialMetadata
                        userAgentPrefix:(NSString *)userAgentPrefix
                      responseSizeLimit:(NSUInteger)responseSizeLimit
                      compressionAlgorithm:(GRPCCompressionAlgorithm)compressionAlgorithm
                            enableRetry:(BOOL)enableRetry
                      keepaliveInterval:(NSTimeInterval)keepaliveInterval
                       keepaliveTimeout:(NSTimeInterval)keepaliveTimeout
                      connectMinTimeout:(NSTimeInterval)connectMinTimeout
                  connectInitialBackoff:(NSTimeInterval)connectInitialBackoff
                      connectMaxBackoff:(NSTimeInterval)connectMaxBackoff
                  additionalChannelArgs:(NSDictionary *)additionalChannelArgs
                    PEMRootCertificates:(NSString *)PEMRootCertificates
                          PEMPrivateKey:(NSString *)PEMPrivateKey
                           PEMCertChain:(NSString *)PEMCertChain
                          transportType:(GRPCTransportType)transportType
                       hostNameOverride:(NSString *)hostNameOverride
                             logContext:(id)logContext
                      channelPoolDomain:(NSString *)channelPoolDomain
                              channelID:(NSUInteger)channelID {
  if ((self = [super init])) {
    _serverAuthority = [serverAuthority copy];
    _timeout = timeout;
    _oauth2AccessToken = [oauth2AccessToken copy];
    _authTokenProvider = authTokenProvider;
    _initialMetadata = [[NSDictionary alloc] initWithDictionary:initialMetadata copyItems:YES];
    _userAgentPrefix = [userAgentPrefix copy];
    _responseSizeLimit = responseSizeLimit;
    _compressionAlgorithm = compressionAlgorithm;
    _enableRetry = enableRetry;
    _keepaliveInterval = keepaliveInterval;
    _keepaliveTimeout = keepaliveTimeout;
    _connectMinTimeout = connectMinTimeout;
    _connectInitialBackoff = connectInitialBackoff;
    _connectMaxBackoff = connectMaxBackoff;
    _additionalChannelArgs =
        [[NSDictionary alloc] initWithDictionary:additionalChannelArgs copyItems:YES];
    _PEMRootCertificates = [PEMRootCertificates copy];
    _PEMPrivateKey = [PEMPrivateKey copy];
    _PEMCertChain = [PEMCertChain copy];
    _transportType = transportType;
    _hostNameOverride = [hostNameOverride copy];
    _logContext = logContext;
    _channelPoolDomain = [channelPoolDomain copy];
    _channelID = channelID;
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
                                                  compressionAlgorithm:_compressionAlgorithm
                                                        enableRetry:_enableRetry
                                                  keepaliveInterval:_keepaliveInterval
                                                   keepaliveTimeout:_keepaliveTimeout
                                                  connectMinTimeout:_connectMinTimeout
                                              connectInitialBackoff:_connectInitialBackoff
                                                  connectMaxBackoff:_connectMaxBackoff
                                              additionalChannelArgs:_additionalChannelArgs
                                                PEMRootCertificates:_PEMRootCertificates
                                                      PEMPrivateKey:_PEMPrivateKey
                                                       PEMCertChain:_PEMCertChain
                                                      transportType:_transportType
                                                   hostNameOverride:_hostNameOverride
                                                         logContext:_logContext
                                                  channelPoolDomain:_channelPoolDomain
                                                          channelID:_channelID];
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
            compressionAlgorithm:_compressionAlgorithm
                  enableRetry:_enableRetry
            keepaliveInterval:_keepaliveInterval
             keepaliveTimeout:_keepaliveTimeout
            connectMinTimeout:_connectMinTimeout
        connectInitialBackoff:_connectInitialBackoff
            connectMaxBackoff:_connectMaxBackoff
        additionalChannelArgs:[_additionalChannelArgs copy]
          PEMRootCertificates:_PEMRootCertificates
                PEMPrivateKey:_PEMPrivateKey
                 PEMCertChain:_PEMCertChain
                transportType:_transportType
             hostNameOverride:_hostNameOverride
                   logContext:_logContext
            channelPoolDomain:_channelPoolDomain
                    channelID:_channelID];
  return newOptions;
}

- (BOOL)isChannelOptionsEqualTo:(GRPCCallOptions *)callOptions {
  if (!(callOptions.userAgentPrefix == _userAgentPrefix ||
        [callOptions.userAgentPrefix isEqualToString:_userAgentPrefix]))
    return NO;
  if (!(callOptions.responseSizeLimit == _responseSizeLimit)) return NO;
  if (!(callOptions.compressionAlgorithm == _compressionAlgorithm)) return NO;
  if (!(callOptions.enableRetry == _enableRetry)) return NO;
  if (!(callOptions.keepaliveInterval == _keepaliveInterval)) return NO;
  if (!(callOptions.keepaliveTimeout == _keepaliveTimeout)) return NO;
  if (!(callOptions.connectMinTimeout == _connectMinTimeout)) return NO;
  if (!(callOptions.connectInitialBackoff == _connectInitialBackoff)) return NO;
  if (!(callOptions.connectMaxBackoff == _connectMaxBackoff)) return NO;
  if (!(callOptions.additionalChannelArgs == _additionalChannelArgs ||
        [callOptions.additionalChannelArgs isEqualToDictionary:_additionalChannelArgs]))
    return NO;
  if (!(callOptions.PEMRootCertificates == _PEMRootCertificates ||
        [callOptions.PEMRootCertificates isEqualToString:_PEMRootCertificates]))
    return NO;
  if (!(callOptions.PEMPrivateKey == _PEMPrivateKey ||
        [callOptions.PEMPrivateKey isEqualToString:_PEMPrivateKey]))
    return NO;
  if (!(callOptions.PEMCertChain == _PEMCertChain ||
        [callOptions.PEMCertChain isEqualToString:_PEMCertChain]))
    return NO;
  if (!(callOptions.hostNameOverride == _hostNameOverride ||
        [callOptions.hostNameOverride isEqualToString:_hostNameOverride]))
    return NO;
  if (!(callOptions.transportType == _transportType)) return NO;
  if (!(callOptions.logContext == _logContext || [callOptions.logContext isEqual:_logContext]))
    return NO;
  if (!(callOptions.channelPoolDomain == _channelPoolDomain ||
        [callOptions.channelPoolDomain isEqualToString:_channelPoolDomain]))
    return NO;
  if (!(callOptions.channelID == _channelID)) return NO;

  return YES;
}

- (NSUInteger)channelOptionsHash {
  NSUInteger result = 0;
  result ^= _userAgentPrefix.hash;
  result ^= _responseSizeLimit;
  result ^= _compressionAlgorithm;
  result ^= _enableRetry;
  result ^= (unsigned int)(_keepaliveInterval * 1000);
  result ^= (unsigned int)(_keepaliveTimeout * 1000);
  result ^= (unsigned int)(_connectMinTimeout * 1000);
  result ^= (unsigned int)(_connectInitialBackoff * 1000);
  result ^= (unsigned int)(_connectMaxBackoff * 1000);
  result ^= _additionalChannelArgs.hash;
  result ^= _PEMRootCertificates.hash;
  result ^= _PEMPrivateKey.hash;
  result ^= _PEMCertChain.hash;
  result ^= _hostNameOverride.hash;
  result ^= _transportType;
  result ^= [_logContext hash];
  result ^= _channelPoolDomain.hash;
  result ^= _channelID;

  return result;
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
@dynamic compressionAlgorithm;
@dynamic enableRetry;
@dynamic keepaliveInterval;
@dynamic keepaliveTimeout;
@dynamic connectMinTimeout;
@dynamic connectInitialBackoff;
@dynamic connectMaxBackoff;
@dynamic additionalChannelArgs;
@dynamic PEMRootCertificates;
@dynamic PEMPrivateKey;
@dynamic PEMCertChain;
@dynamic transportType;
@dynamic hostNameOverride;
@dynamic logContext;
@dynamic channelPoolDomain;
@dynamic channelID;

- (instancetype)init {
  return [self initWithServerAuthority:kDefaultServerAuthority
                               timeout:kDefaultTimeout
                     oauth2AccessToken:kDefaultOauth2AccessToken
                     authTokenProvider:kDefaultAuthTokenProvider
                       initialMetadata:kDefaultInitialMetadata
                       userAgentPrefix:kDefaultUserAgentPrefix
                     responseSizeLimit:kDefaultResponseSizeLimit
                     compressionAlgorithm:kDefaultCompressionAlgorithm
                           enableRetry:kDefaultEnableRetry
                     keepaliveInterval:kDefaultKeepaliveInterval
                      keepaliveTimeout:kDefaultKeepaliveTimeout
                     connectMinTimeout:kDefaultConnectMinTimeout
                 connectInitialBackoff:kDefaultConnectInitialBackoff
                     connectMaxBackoff:kDefaultConnectMaxBackoff
                 additionalChannelArgs:kDefaultAdditionalChannelArgs
                   PEMRootCertificates:kDefaultPEMRootCertificates
                         PEMPrivateKey:kDefaultPEMPrivateKey
                          PEMCertChain:kDefaultPEMCertChain
                         transportType:kDefaultTransportType
                      hostNameOverride:kDefaultHostNameOverride
                            logContext:kDefaultLogContext
                     channelPoolDomain:kDefaultChannelPoolDomain
                             channelID:kDefaultChannelID];
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
                                                  compressionAlgorithm:_compressionAlgorithm
                                                        enableRetry:_enableRetry
                                                  keepaliveInterval:_keepaliveInterval
                                                   keepaliveTimeout:_keepaliveTimeout
                                                  connectMinTimeout:_connectMinTimeout
                                              connectInitialBackoff:_connectInitialBackoff
                                                  connectMaxBackoff:_connectMaxBackoff
                                              additionalChannelArgs:[_additionalChannelArgs copy]
                                                PEMRootCertificates:_PEMRootCertificates
                                                      PEMPrivateKey:_PEMPrivateKey
                                                       PEMCertChain:_PEMCertChain
                                                      transportType:_transportType
                                                   hostNameOverride:_hostNameOverride
                                                         logContext:_logContext
                                                  channelPoolDomain:_channelPoolDomain
                                                          channelID:_channelID];
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
            compressionAlgorithm:_compressionAlgorithm
                  enableRetry:_enableRetry
            keepaliveInterval:_keepaliveInterval
             keepaliveTimeout:_keepaliveTimeout
            connectMinTimeout:_connectMinTimeout
        connectInitialBackoff:_connectInitialBackoff
            connectMaxBackoff:_connectMaxBackoff
        additionalChannelArgs:[_additionalChannelArgs copy]
          PEMRootCertificates:_PEMRootCertificates
                PEMPrivateKey:_PEMPrivateKey
                 PEMCertChain:_PEMCertChain
                transportType:_transportType
             hostNameOverride:_hostNameOverride
                   logContext:_logContext
            channelPoolDomain:_channelPoolDomain
                    channelID:_channelID];
  return newOptions;
}

- (void)setServerAuthority:(NSString *)serverAuthority {
  _serverAuthority = [serverAuthority copy];
}

- (void)setTimeout:(NSTimeInterval)timeout {
  if (timeout < 0) {
    _timeout = 0;
  } else {
    _timeout = timeout;
  }
}

- (void)setOauth2AccessToken:(NSString *)oauth2AccessToken {
  _oauth2AccessToken = [oauth2AccessToken copy];
}

- (void)setAuthTokenProvider:(id<GRPCAuthorizationProtocol>)authTokenProvider {
  _authTokenProvider = authTokenProvider;
}

- (void)setInitialMetadata:(NSDictionary *)initialMetadata {
  _initialMetadata = [[NSDictionary alloc] initWithDictionary:initialMetadata copyItems:YES];
}

- (void)setUserAgentPrefix:(NSString *)userAgentPrefix {
  _userAgentPrefix = [userAgentPrefix copy];
}

- (void)setResponseSizeLimit:(NSUInteger)responseSizeLimit {
  _responseSizeLimit = responseSizeLimit;
}

- (void)setCompressionAlgorithm:(GRPCCompressionAlgorithm)compressionAlgorithm {
  _compressionAlgorithm = compressionAlgorithm;
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
  _additionalChannelArgs =
      [[NSDictionary alloc] initWithDictionary:additionalChannelArgs copyItems:YES];
}

- (void)setPEMRootCertificates:(NSString *)PEMRootCertificates {
  _PEMRootCertificates = [PEMRootCertificates copy];
}

- (void)setPEMPrivateKey:(NSString *)PEMPrivateKey {
  _PEMPrivateKey = [PEMPrivateKey copy];
}

- (void)setPEMCertChain:(NSString *)PEMCertChain {
  _PEMCertChain = [PEMCertChain copy];
}

- (void)setTransportType:(GRPCTransportType)transportType {
  _transportType = transportType;
}

- (void)setHostNameOverride:(NSString *)hostNameOverride {
  _hostNameOverride = [hostNameOverride copy];
}

- (void)setLogContext:(id)logContext {
  _logContext = logContext;
}

- (void)setChannelPoolDomain:(NSString *)channelPoolDomain {
  _channelPoolDomain = [channelPoolDomain copy];
}

- (void)setChannelID:(NSUInteger)channelID {
  _channelID = channelID;
}

@end
