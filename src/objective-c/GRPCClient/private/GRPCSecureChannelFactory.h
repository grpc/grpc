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
#import "GRPCChannelFactory.h"

@class GRPCChannel;

NS_ASSUME_NONNULL_BEGIN

@interface GRPCSecureChannelFactory : NSObject<GRPCChannelFactory>

+ (nullable instancetype)factoryWithPEMRootCertificates:(nullable NSString *)rootCerts
                                             privateKey:(nullable NSString *)privateKey
                                              certChain:(nullable NSString *)certChain
                                                  error:(NSError **)errorPtr;

- (nullable grpc_channel *)createChannelWithHost:(NSString *)host
                                     channelArgs:(nullable NSDictionary *)args;

- (nullable instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
