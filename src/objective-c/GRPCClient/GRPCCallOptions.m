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
#import "GRPCTransport.h"
#import "internal/GRPCCallOptions+Internal.h"

// The default values for the call options.
static NSString *const kDefaultServerAuthority = nil;
static const NSTimeInterval kDefaultTimeout = 0;
static const BOOL kDefaultFlowControlEnabled = NO;
static NSArray<id<GRPCInterceptorFactory>> *const kDefaultInterceptorFactories = nil;
static NSDictionary *const kDefaultInitialMetadata = nil;
static NSString *const kDefaultUserAgentPrefix = nil;
static const NSUInteger kDefaultResponseSizeLimit = 0;
static const GRPCCompressionAlgorithm kDefaultCompressionAlgorithm = GRPCCompressNone;
static const BOOL kDefaultRetryEnabled = YES;
static const NSTimeInterval kDefaultKeepaliveInterval = 0;
static const NSTimeInterval kDefaultKeepaliveTimeout = 0;
static const NSTimeInterval kDefaultConnectMinTimeout = 0;
static const NSTimeInterval kDefaultConnectInitialBackoff = 0;
static const NSTimeInterval kDefaultConnectMaxBackoff = 0;
static NSDictionary *const kDefaultAdditionalChannelArgs = nil;
static NSString *const kDefaultPEMRootCertificates = nil;
static NSString *const kDefaultPEMPrivateKey = nil;
static NSString *const kDefaultPEMCertificateChain = nil;
static NSString *const kDefaultOauth2AccessToken = nil;
static const id<GRPCAuthorizationProtocol> kDefaultAuthTokenProvider = nil;
static const GRPCTransportType kDefaultTransportType = GRPCTransportTypeChttp2BoringSSL;
static const GRPCTransportID kDefaultTransport = NULL;
static NSString *const kDefaultHostNameOverride = nil;
static const id kDefaultLogContext = nil;
static NSString *const kDefaultChannelPoolDomain = nil;
static const NSUInteger kDefaultChannelID = 0;

// Check if two objects are equal. Returns YES if both are nil;
static BOOL areObjectsEqual(id obj1, id obj2) {
  if (obj1 == obj2) {
    return YES;
  }
  if (obj1 == nil || obj2 == nil) {
    return NO;
  }
  return [obj1 isEqual:obj2];
}

@implementation GRPCCallOptions {
 @protected
  NSString *_serverAuthority;
  NSTimeInterval _timeout;
  BOOL _flowControlEnabled;
  NSArray<id<GRPCInterceptorFactory>> *_interceptorFactories;
  NSString *_oauth2AccessToken;
  id<GRPCAuthorizationProtocol> _authTokenProvider;
  NSDictionary *_initialMetadata;
  NSString *_userAgentPrefix;
  NSUInteger _responseSizeLimit;
  GRPCCompressionAlgorithm _compressionAlgorithm;
  BOOL _retryEnabled;
  NSTimeInterval _keepaliveInterval;
  NSTimeInterval _keepaliveTimeout;
  NSTimeInterval _connectMinTimeout;
  NSTimeInterval _connectInitialBackoff;
  NSTimeInterval _connectMaxBackoff;
  NSDictionary *_additionalChannelArgs;
  NSString *_PEMRootCertificates;
  NSString *_PEMPrivateKey;
  NSString *_PEMCertificateChain;
  GRPCTransportType _transportType;
  GRPCTransportID _transport;
  NSString *_hostNameOverride;
  id<NSObject> _logContext;
  NSString *_channelPoolDomain;
  NSUInteger _channelID;
}

@synthesize serverAuthority = _serverAuthority;
@synthesize timeout = _timeout;
@synthesize flowControlEnabled = _flowControlEnabled;
@synthesize interceptorFactories = _interceptorFactories;
@synthesize oauth2AccessToken = _oauth2AccessToken;
@synthesize authTokenProvider = _authTokenProvider;
@synthesize initialMetadata = _initialMetadata;
@synthesize userAgentPrefix = _userAgentPrefix;
@synthesize responseSizeLimit = _responseSizeLimit;
@synthesize compressionAlgorithm = _compressionAlgorithm;
@synthesize retryEnabled = _retryEnabled;
@synthesize keepaliveInterval = _keepaliveInterval;
@synthesize keepaliveTimeout = _keepaliveTimeout;
@synthesize connectMinTimeout = _connectMinTimeout;
@synthesize connectInitialBackoff = _connectInitialBackoff;
@synthesize connectMaxBackoff = _connectMaxBackoff;
@synthesize additionalChannelArgs = _additionalChannelArgs;
@synthesize PEMRootCertificates = _PEMRootCertificates;
@synthesize PEMPrivateKey = _PEMPrivateKey;
@synthesize PEMCertificateChain = _PEMCertificateChain;
@synthesize transportType = _transportType;
@synthesize transport = _transport;
@synthesize hostNameOverride = _hostNameOverride;
@synthesize logContext = _logContext;
@synthesize channelPoolDomain = _channelPoolDomain;
@synthesize channelID = _channelID;

- (instancetype)init {
  return [self initWithServerAuthority:kDefaultServerAuthority
                               timeout:kDefaultTimeout
                    flowControlEnabled:kDefaultFlowControlEnabled
                  interceptorFactories:kDefaultInterceptorFactories
                     oauth2AccessToken:kDefaultOauth2AccessToken
                     authTokenProvider:kDefaultAuthTokenProvider
                       initialMetadata:kDefaultInitialMetadata
                       userAgentPrefix:kDefaultUserAgentPrefix
                     responseSizeLimit:kDefaultResponseSizeLimit
                  compressionAlgorithm:kDefaultCompressionAlgorithm
                          retryEnabled:kDefaultRetryEnabled
                     keepaliveInterval:kDefaultKeepaliveInterval
                      keepaliveTimeout:kDefaultKeepaliveTimeout
                     connectMinTimeout:kDefaultConnectMinTimeout
                 connectInitialBackoff:kDefaultConnectInitialBackoff
                     connectMaxBackoff:kDefaultConnectMaxBackoff
                 additionalChannelArgs:kDefaultAdditionalChannelArgs
                   PEMRootCertificates:kDefaultPEMRootCertificates
                         PEMPrivateKey:kDefaultPEMPrivateKey
                   PEMCertificateChain:kDefaultPEMCertificateChain
                         transportType:kDefaultTransportType
                             transport:kDefaultTransport
                      hostNameOverride:kDefaultHostNameOverride
                            logContext:kDefaultLogContext
                     channelPoolDomain:kDefaultChannelPoolDomain
                             channelID:kDefaultChannelID];
}

- (instancetype)initWithServerAuthority:(NSString *)serverAuthority
                                timeout:(NSTimeInterval)timeout
                     flowControlEnabled:(BOOL)flowControlEnabled
                   interceptorFactories:(NSArray<id<GRPCInterceptorFactory>> *)interceptorFactories
                      oauth2AccessToken:(NSString *)oauth2AccessToken
                      authTokenProvider:(id<GRPCAuthorizationProtocol>)authTokenProvider
                        initialMetadata:(NSDictionary *)initialMetadata
                        userAgentPrefix:(NSString *)userAgentPrefix
                      responseSizeLimit:(NSUInteger)responseSizeLimit
                   compressionAlgorithm:(GRPCCompressionAlgorithm)compressionAlgorithm
                           retryEnabled:(BOOL)retryEnabled
                      keepaliveInterval:(NSTimeInterval)keepaliveInterval
                       keepaliveTimeout:(NSTimeInterval)keepaliveTimeout
                      connectMinTimeout:(NSTimeInterval)connectMinTimeout
                  connectInitialBackoff:(NSTimeInterval)connectInitialBackoff
                      connectMaxBackoff:(NSTimeInterval)connectMaxBackoff
                  additionalChannelArgs:(NSDictionary *)additionalChannelArgs
                    PEMRootCertificates:(NSString *)PEMRootCertificates
                          PEMPrivateKey:(NSString *)PEMPrivateKey
                    PEMCertificateChain:(NSString *)PEMCertificateChain
                          transportType:(GRPCTransportType)transportType
                              transport:(GRPCTransportID)transport
                       hostNameOverride:(NSString *)hostNameOverride
                             logContext:(id)logContext
                      channelPoolDomain:(NSString *)channelPoolDomain
                              channelID:(NSUInteger)channelID {
  if ((self = [super init])) {
    _serverAuthority = [serverAuthority copy];
    _timeout = timeout < 0 ? 0 : timeout;
    _flowControlEnabled = flowControlEnabled;
    _interceptorFactories = interceptorFactories;
    _oauth2AccessToken = [oauth2AccessToken copy];
    _authTokenProvider = authTokenProvider;
    _initialMetadata = initialMetadata == nil
                           ? nil
                           : [[NSDictionary alloc] initWithDictionary:initialMetadata
                                                            copyItems:YES];
    _userAgentPrefix = [userAgentPrefix copy];
    _responseSizeLimit = responseSizeLimit;
    _compressionAlgorithm = compressionAlgorithm;
    _retryEnabled = retryEnabled;
    _keepaliveInterval = keepaliveInterval < 0 ? 0 : keepaliveInterval;
    _keepaliveTimeout = keepaliveTimeout < 0 ? 0 : keepaliveTimeout;
    _connectMinTimeout = connectMinTimeout < 0 ? 0 : connectMinTimeout;
    _connectInitialBackoff = connectInitialBackoff < 0 ? 0 : connectInitialBackoff;
    _connectMaxBackoff = connectMaxBackoff < 0 ? 0 : connectMaxBackoff;
    _additionalChannelArgs = additionalChannelArgs == nil
                                 ? nil
                                 : [[NSDictionary alloc] initWithDictionary:additionalChannelArgs
                                                                  copyItems:YES];
    _PEMRootCertificates = [PEMRootCertificates copy];
    _PEMPrivateKey = [PEMPrivateKey copy];
    _PEMCertificateChain = [PEMCertificateChain copy];
    _transportType = transportType;
    _transport = transport;
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
                                                 flowControlEnabled:_flowControlEnabled
                                               interceptorFactories:_interceptorFactories
                                                  oauth2AccessToken:_oauth2AccessToken
                                                  authTokenProvider:_authTokenProvider
                                                    initialMetadata:_initialMetadata
                                                    userAgentPrefix:_userAgentPrefix
                                                  responseSizeLimit:_responseSizeLimit
                                               compressionAlgorithm:_compressionAlgorithm
                                                       retryEnabled:_retryEnabled
                                                  keepaliveInterval:_keepaliveInterval
                                                   keepaliveTimeout:_keepaliveTimeout
                                                  connectMinTimeout:_connectMinTimeout
                                              connectInitialBackoff:_connectInitialBackoff
                                                  connectMaxBackoff:_connectMaxBackoff
                                              additionalChannelArgs:_additionalChannelArgs
                                                PEMRootCertificates:_PEMRootCertificates
                                                      PEMPrivateKey:_PEMPrivateKey
                                                PEMCertificateChain:_PEMCertificateChain
                                                      transportType:_transportType
                                                          transport:_transport
                                                   hostNameOverride:_hostNameOverride
                                                         logContext:_logContext
                                                  channelPoolDomain:_channelPoolDomain
                                                          channelID:_channelID];
  return newOptions;
}

- (nonnull id)mutableCopyWithZone:(NSZone *)zone {
  GRPCMutableCallOptions *newOptions = [[GRPCMutableCallOptions allocWithZone:zone]
      initWithServerAuthority:[_serverAuthority copy]
                      timeout:_timeout
           flowControlEnabled:_flowControlEnabled
         interceptorFactories:_interceptorFactories
            oauth2AccessToken:[_oauth2AccessToken copy]
            authTokenProvider:_authTokenProvider
              initialMetadata:[[NSDictionary alloc] initWithDictionary:_initialMetadata
                                                             copyItems:YES]
              userAgentPrefix:[_userAgentPrefix copy]
            responseSizeLimit:_responseSizeLimit
         compressionAlgorithm:_compressionAlgorithm
                 retryEnabled:_retryEnabled
            keepaliveInterval:_keepaliveInterval
             keepaliveTimeout:_keepaliveTimeout
            connectMinTimeout:_connectMinTimeout
        connectInitialBackoff:_connectInitialBackoff
            connectMaxBackoff:_connectMaxBackoff
        additionalChannelArgs:[[NSDictionary alloc] initWithDictionary:_additionalChannelArgs
                                                             copyItems:YES]
          PEMRootCertificates:[_PEMRootCertificates copy]
                PEMPrivateKey:[_PEMPrivateKey copy]
          PEMCertificateChain:[_PEMCertificateChain copy]
                transportType:_transportType
                    transport:_transport
             hostNameOverride:[_hostNameOverride copy]
                   logContext:_logContext
            channelPoolDomain:[_channelPoolDomain copy]
                    channelID:_channelID];
  return newOptions;
}

- (BOOL)hasChannelOptionsEqualTo:(GRPCCallOptions *)callOptions {
  if (callOptions == nil) return NO;
  if (!areObjectsEqual(callOptions.userAgentPrefix, _userAgentPrefix)) return NO;
  if (!(callOptions.responseSizeLimit == _responseSizeLimit)) return NO;
  if (!(callOptions.compressionAlgorithm == _compressionAlgorithm)) return NO;
  if (!(callOptions.retryEnabled == _retryEnabled)) return NO;
  if (!(callOptions.keepaliveInterval == _keepaliveInterval)) return NO;
  if (!(callOptions.keepaliveTimeout == _keepaliveTimeout)) return NO;
  if (!(callOptions.connectMinTimeout == _connectMinTimeout)) return NO;
  if (!(callOptions.connectInitialBackoff == _connectInitialBackoff)) return NO;
  if (!(callOptions.connectMaxBackoff == _connectMaxBackoff)) return NO;
  if (!areObjectsEqual(callOptions.additionalChannelArgs, _additionalChannelArgs)) return NO;
  if (!areObjectsEqual(callOptions.PEMRootCertificates, _PEMRootCertificates)) return NO;
  if (!areObjectsEqual(callOptions.PEMPrivateKey, _PEMPrivateKey)) return NO;
  if (!areObjectsEqual(callOptions.PEMCertificateChain, _PEMCertificateChain)) return NO;
  if (!areObjectsEqual(callOptions.hostNameOverride, _hostNameOverride)) return NO;
  if (!(callOptions.transportType == _transportType)) return NO;
  if (!(TransportIDIsEqual(callOptions.transport, _transport))) return NO;
  if (!areObjectsEqual(callOptions.logContext, _logContext)) return NO;
  if (!areObjectsEqual(callOptions.channelPoolDomain, _channelPoolDomain)) return NO;
  if (!(callOptions.channelID == _channelID)) return NO;

  return YES;
}

- (NSUInteger)channelOptionsHash {
  NSUInteger result = 0;
  result ^= _userAgentPrefix.hash;
  result ^= _responseSizeLimit;
  result ^= _compressionAlgorithm;
  result ^= _retryEnabled;
  result ^= (unsigned int)(_keepaliveInterval * 1000);
  result ^= (unsigned int)(_keepaliveTimeout * 1000);
  result ^= (unsigned int)(_connectMinTimeout * 1000);
  result ^= (unsigned int)(_connectInitialBackoff * 1000);
  result ^= (unsigned int)(_connectMaxBackoff * 1000);
  result ^= _additionalChannelArgs.hash;
  result ^= _PEMRootCertificates.hash;
  result ^= _PEMPrivateKey.hash;
  result ^= _PEMCertificateChain.hash;
  result ^= _hostNameOverride.hash;
  result ^= _transportType;
  result ^= TransportIDHash(_transport);
  result ^= _logContext.hash;
  result ^= _channelPoolDomain.hash;
  result ^= _channelID;

  return result;
}

@end

@implementation GRPCMutableCallOptions

@dynamic serverAuthority;
@dynamic timeout;
@dynamic flowControlEnabled;
@dynamic interceptorFactories;
@dynamic oauth2AccessToken;
@dynamic authTokenProvider;
@dynamic initialMetadata;
@dynamic userAgentPrefix;
@dynamic responseSizeLimit;
@dynamic compressionAlgorithm;
@dynamic retryEnabled;
@dynamic keepaliveInterval;
@dynamic keepaliveTimeout;
@dynamic connectMinTimeout;
@dynamic connectInitialBackoff;
@dynamic connectMaxBackoff;
@dynamic additionalChannelArgs;
@dynamic PEMRootCertificates;
@dynamic PEMPrivateKey;
@dynamic PEMCertificateChain;
@dynamic transportType;
@dynamic transport;
@dynamic hostNameOverride;
@dynamic logContext;
@dynamic channelPoolDomain;
@dynamic channelID;

- (instancetype)init {
  return [self initWithServerAuthority:kDefaultServerAuthority
                               timeout:kDefaultTimeout
                    flowControlEnabled:kDefaultFlowControlEnabled
                  interceptorFactories:kDefaultInterceptorFactories
                     oauth2AccessToken:kDefaultOauth2AccessToken
                     authTokenProvider:kDefaultAuthTokenProvider
                       initialMetadata:kDefaultInitialMetadata
                       userAgentPrefix:kDefaultUserAgentPrefix
                     responseSizeLimit:kDefaultResponseSizeLimit
                  compressionAlgorithm:kDefaultCompressionAlgorithm
                          retryEnabled:kDefaultRetryEnabled
                     keepaliveInterval:kDefaultKeepaliveInterval
                      keepaliveTimeout:kDefaultKeepaliveTimeout
                     connectMinTimeout:kDefaultConnectMinTimeout
                 connectInitialBackoff:kDefaultConnectInitialBackoff
                     connectMaxBackoff:kDefaultConnectMaxBackoff
                 additionalChannelArgs:kDefaultAdditionalChannelArgs
                   PEMRootCertificates:kDefaultPEMRootCertificates
                         PEMPrivateKey:kDefaultPEMPrivateKey
                   PEMCertificateChain:kDefaultPEMCertificateChain
                         transportType:kDefaultTransportType
                             transport:kDefaultTransport
                      hostNameOverride:kDefaultHostNameOverride
                            logContext:kDefaultLogContext
                     channelPoolDomain:kDefaultChannelPoolDomain
                             channelID:kDefaultChannelID];
}

- (nonnull id)copyWithZone:(NSZone *)zone {
  GRPCCallOptions *newOptions =
      [[GRPCCallOptions allocWithZone:zone] initWithServerAuthority:_serverAuthority
                                                            timeout:_timeout
                                                 flowControlEnabled:_flowControlEnabled
                                               interceptorFactories:_interceptorFactories
                                                  oauth2AccessToken:_oauth2AccessToken
                                                  authTokenProvider:_authTokenProvider
                                                    initialMetadata:_initialMetadata
                                                    userAgentPrefix:_userAgentPrefix
                                                  responseSizeLimit:_responseSizeLimit
                                               compressionAlgorithm:_compressionAlgorithm
                                                       retryEnabled:_retryEnabled
                                                  keepaliveInterval:_keepaliveInterval
                                                   keepaliveTimeout:_keepaliveTimeout
                                                  connectMinTimeout:_connectMinTimeout
                                              connectInitialBackoff:_connectInitialBackoff
                                                  connectMaxBackoff:_connectMaxBackoff
                                              additionalChannelArgs:_additionalChannelArgs
                                                PEMRootCertificates:_PEMRootCertificates
                                                      PEMPrivateKey:_PEMPrivateKey
                                                PEMCertificateChain:_PEMCertificateChain
                                                      transportType:_transportType
                                                          transport:_transport
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
           flowControlEnabled:_flowControlEnabled
         interceptorFactories:_interceptorFactories
            oauth2AccessToken:_oauth2AccessToken
            authTokenProvider:_authTokenProvider
              initialMetadata:_initialMetadata
              userAgentPrefix:_userAgentPrefix
            responseSizeLimit:_responseSizeLimit
         compressionAlgorithm:_compressionAlgorithm
                 retryEnabled:_retryEnabled
            keepaliveInterval:_keepaliveInterval
             keepaliveTimeout:_keepaliveTimeout
            connectMinTimeout:_connectMinTimeout
        connectInitialBackoff:_connectInitialBackoff
            connectMaxBackoff:_connectMaxBackoff
        additionalChannelArgs:[_additionalChannelArgs copy]
          PEMRootCertificates:_PEMRootCertificates
                PEMPrivateKey:_PEMPrivateKey
          PEMCertificateChain:_PEMCertificateChain
                transportType:_transportType
                    transport:_transport
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

- (void)setFlowControlEnabled:(BOOL)flowControlEnabled {
  _flowControlEnabled = flowControlEnabled;
}

- (void)setInterceptorFactories:(NSArray<id<GRPCInterceptorFactory>> *)interceptorFactories {
  _interceptorFactories = interceptorFactories;
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

- (void)setRetryEnabled:(BOOL)retryEnabled {
  _retryEnabled = retryEnabled;
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
    _connectMinTimeout = 0;
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
  _additionalChannelArgs = [[NSDictionary alloc] initWithDictionary:additionalChannelArgs
                                                          copyItems:YES];
}

- (void)setPEMRootCertificates:(NSString *)PEMRootCertificates {
  _PEMRootCertificates = [PEMRootCertificates copy];
}

- (void)setPEMPrivateKey:(NSString *)PEMPrivateKey {
  _PEMPrivateKey = [PEMPrivateKey copy];
}

- (void)setPEMCertificateChain:(NSString *)PEMCertificateChain {
  _PEMCertificateChain = [PEMCertificateChain copy];
}

- (void)setTransportType:(GRPCTransportType)transportType {
  _transportType = transportType;
}

- (void)setTransport:(GRPCTransportID)transport {
  _transport = transport;
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
