/*
 *
 * Copyright 2022 gRPC authors.
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

#import <XCTest/XCTest.h>

#import <GRPCClient/GRPCCallOptions.h>

static NSString *const kGRPCCallOptionsTestServerAuthority = @"dummy";
static NSTimeInterval kGRPCCallOptionsTestTimeout = 11;
static BOOL kGRPCCallOptionsTestFlowControl = YES;
static NSString *const kGRPCCallOptionsTestOAuth2Token = @"token";
static NSString *const kGRPCCallOptionsTestUserAgentPrefix = @"test_prefix";
static NSString *const kGRPCCallOptionsTestUserAgentSuffix = @"test_suffix";
static NSUInteger kGRPCCallOptionsTestResponseSizeLimit = 81;
static GRPCCompressionAlgorithm kGRPCCallOptionsTestCompressionAlgorithm = GRPCCompressDeflate;
static BOOL kGRPCCallOptionsTestRetryEnabled = YES;
static NSTimeInterval kGRPCCallOptionsTestMaxRetryInterval = 101;
static NSTimeInterval kGRPCCallOptionsTestMinRetryInterval = 23;
static double kGRPCCallOptionsTestRetryFactor = 2.12;
static NSTimeInterval kGRPCCallOptionsTestKeepAliveTimeout = 120;
static NSTimeInterval kGRPCCallOptionsTestKeepAliveInterval = 60;
static NSTimeInterval kGRPCCallOptionsTestConnectMaxBackoff = 180;
static NSTimeInterval kGRPCCallOptionsTestConnectMinTimeout = 210;
static NSTimeInterval kGRPCCallOptionsTestConnectInitialBackoff = 12;
static NSString *const kGRPCCallOptionsTestPEMPrivateKey = @"dummy_key";
static NSString *const kGRPCCallOptionsTestPEMRootCertificates = @"dummy_cert";
static NSString *const kGRPCCallOptionsTestPEMCertificateChain = @"dummy_chain";
static GRPCTransportType kGRPCCallOptionsTestTransportType = GRPCTransportTypeChttp2BoringSSL;
static GRPCTransportID kGRPCCallOptionsTestTransportID = "dummy_transport";
static NSString *const kGRPCCallOptionsTestHostNameOverride = @"localhost";
static NSString *const kGRPCCallOptionsTestChannelPoolDomain = @"dummy_domain";
static NSUInteger kGRPCCallOptionsTestChannelID = 111;

@interface GRPCCallOptionsTests : XCTestCase

@end

@implementation GRPCCallOptionsTests

/** Verify a valid immutable copy can be created from GRPCCallOptions. */
- (void)testCreateImmutableCopyFromImmutable {
  GRPCCallOptions *opt = [[GRPCCallOptions alloc] init];
  GRPCCallOptions *subject = [opt copy];
  XCTAssertTrue([subject isKindOfClass:[GRPCCallOptions class]]);
}

/** Verify a valid mutable copy can be created from GRPCCallOptions. */
- (void)testCreateMutableCopyFromImmutable {
  GRPCCallOptions *opt = [[GRPCCallOptions alloc] init];
  GRPCMutableCallOptions *subject = [opt mutableCopy];
  XCTAssertTrue([subject isKindOfClass:[GRPCMutableCallOptions class]]);
}

/** Verify a valid mutable copy can be created from GRPCMutableCallOptions. */
- (void)testCreateMutableCopyFromMutable {
  GRPCMutableCallOptions *opt = [[GRPCMutableCallOptions alloc] init];
  GRPCMutableCallOptions *subject = [opt mutableCopy];
  XCTAssertTrue([subject isKindOfClass:[GRPCMutableCallOptions class]]);
}

/** Verify a valid immutable copy can be created from GRPCMutableCallOptions. */
- (void)testCreateImmutableCopyFromMutable {
  GRPCMutableCallOptions *mutableOpt = [[GRPCMutableCallOptions alloc] init];
  GRPCCallOptions *subject = [mutableOpt copy];
  XCTAssertTrue([subject isKindOfClass:[GRPCCallOptions class]]);
}

/** Verify property values are copied when copy from mutable options. */
- (void)testCopyFromMutableCallOptions {
  GRPCMutableCallOptions *mutableOpt = [self testMutableCallOptions];
  XCTAssertTrue([self isEqualToTestCallOptions:[mutableOpt copy]]);
  XCTAssertTrue([self isEqualToTestCallOptions:[mutableOpt mutableCopy]]);
}

/** Verify property values are copied when copy from immutable options */
- (void)testCopyFromImmutableCallOptions {
  GRPCCallOptions *opt = [[self testMutableCallOptions] copy];
  XCTAssertTrue([self isEqualToTestCallOptions:[opt copy]]);
  XCTAssertTrue([self isEqualToTestCallOptions:[opt mutableCopy]]);
}

#pragma mark - Private

- (GRPCMutableCallOptions *)testMutableCallOptions {
  GRPCMutableCallOptions *mutableOpt = [[GRPCMutableCallOptions alloc] init];
  mutableOpt.serverAuthority = kGRPCCallOptionsTestServerAuthority;
  mutableOpt.timeout = kGRPCCallOptionsTestTimeout;
  mutableOpt.flowControlEnabled = kGRPCCallOptionsTestFlowControl;
  mutableOpt.oauth2AccessToken = kGRPCCallOptionsTestOAuth2Token;
  mutableOpt.initialMetadata = @{@"key" : @"value"};
  mutableOpt.userAgentPrefix = kGRPCCallOptionsTestUserAgentPrefix;
  mutableOpt.userAgentSuffix = kGRPCCallOptionsTestUserAgentSuffix;
  mutableOpt.responseSizeLimit = kGRPCCallOptionsTestResponseSizeLimit;
  mutableOpt.compressionAlgorithm = kGRPCCallOptionsTestCompressionAlgorithm;
  mutableOpt.retryEnabled = kGRPCCallOptionsTestRetryEnabled;
  mutableOpt.maxRetryInterval = kGRPCCallOptionsTestMaxRetryInterval;
  mutableOpt.minRetryInterval = kGRPCCallOptionsTestMinRetryInterval;
  mutableOpt.retryFactor = kGRPCCallOptionsTestRetryFactor;
  mutableOpt.keepaliveTimeout = kGRPCCallOptionsTestKeepAliveTimeout;
  mutableOpt.keepaliveInterval = kGRPCCallOptionsTestKeepAliveInterval;
  mutableOpt.connectMaxBackoff = kGRPCCallOptionsTestConnectMaxBackoff;
  mutableOpt.connectMinTimeout = kGRPCCallOptionsTestConnectMinTimeout;
  mutableOpt.connectInitialBackoff = kGRPCCallOptionsTestConnectInitialBackoff;
  mutableOpt.additionalChannelArgs = @{@"extra_key" : @"extra_value"};
  mutableOpt.PEMPrivateKey = kGRPCCallOptionsTestPEMPrivateKey;
  mutableOpt.PEMRootCertificates = kGRPCCallOptionsTestPEMRootCertificates;
  mutableOpt.PEMCertificateChain = kGRPCCallOptionsTestPEMCertificateChain;
  mutableOpt.transportType = kGRPCCallOptionsTestTransportType;
  mutableOpt.transport = kGRPCCallOptionsTestTransportID;
  mutableOpt.hostNameOverride = kGRPCCallOptionsTestHostNameOverride;
  mutableOpt.channelPoolDomain = kGRPCCallOptionsTestChannelPoolDomain;
  mutableOpt.channelID = kGRPCCallOptionsTestChannelID;
  return mutableOpt;
}

- (BOOL)isEqualToTestCallOptions:(GRPCCallOptions *)callOpt {
  return [callOpt.serverAuthority isEqualToString:kGRPCCallOptionsTestServerAuthority] &&
         callOpt.timeout == kGRPCCallOptionsTestTimeout &&
         callOpt.flowControlEnabled == kGRPCCallOptionsTestFlowControl &&
         [callOpt.oauth2AccessToken isEqualToString:kGRPCCallOptionsTestOAuth2Token] &&
         [callOpt.initialMetadata isEqualToDictionary:@{@"key" : @"value"}] &&
         [callOpt.userAgentPrefix isEqualToString:kGRPCCallOptionsTestUserAgentPrefix] &&
         [callOpt.userAgentSuffix isEqualToString:kGRPCCallOptionsTestUserAgentSuffix] &&
         callOpt.responseSizeLimit == kGRPCCallOptionsTestResponseSizeLimit &&
         callOpt.compressionAlgorithm == kGRPCCallOptionsTestCompressionAlgorithm &&
         callOpt.retryEnabled == kGRPCCallOptionsTestRetryEnabled &&
         callOpt.maxRetryInterval == kGRPCCallOptionsTestMaxRetryInterval &&
         callOpt.minRetryInterval == kGRPCCallOptionsTestMinRetryInterval &&
         callOpt.retryFactor == kGRPCCallOptionsTestRetryFactor &&
         callOpt.keepaliveTimeout == kGRPCCallOptionsTestKeepAliveTimeout &&
         callOpt.keepaliveInterval == kGRPCCallOptionsTestKeepAliveInterval &&
         callOpt.connectMaxBackoff == kGRPCCallOptionsTestConnectMaxBackoff &&
         callOpt.connectMinTimeout == kGRPCCallOptionsTestConnectMinTimeout &&
         callOpt.connectInitialBackoff == kGRPCCallOptionsTestConnectInitialBackoff &&
         [callOpt.additionalChannelArgs isEqualToDictionary:@{@"extra_key" : @"extra_value"}] &&
         [callOpt.PEMPrivateKey isEqualToString:kGRPCCallOptionsTestPEMPrivateKey] &&
         [callOpt.PEMCertificateChain isEqualToString:kGRPCCallOptionsTestPEMCertificateChain] &&
         [callOpt.PEMRootCertificates isEqualToString:kGRPCCallOptionsTestPEMRootCertificates] &&
         callOpt.transportType == kGRPCCallOptionsTestTransportType &&
         callOpt.transport == kGRPCCallOptionsTestTransportID &&
         [callOpt.hostNameOverride isEqualToString:kGRPCCallOptionsTestHostNameOverride] &&
         [callOpt.channelPoolDomain isEqualToString:kGRPCCallOptionsTestChannelPoolDomain] &&
         callOpt.channelID == kGRPCCallOptionsTestChannelID;
}

@end
