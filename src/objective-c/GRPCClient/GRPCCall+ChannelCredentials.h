/*
 *
 * Copyright 2016 gRPC authors.
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

#import "GRPCCall.h"

/** Helpers for setting TLS Trusted Roots, Client Certificates, and Private Key */
@interface GRPCCall (ChannelCredentials)

/**
 * Use the provided @c pemRootCert as the set of trusted root Certificate Authorities for @c host.
 */
+ (BOOL)setTLSPEMRootCerts:(nullable NSString *)pemRootCert
                   forHost:(nonnull NSString *)host
                     error:(NSError *_Nullable *_Nullable)errorPtr;
/**
 * Configures @c host with TLS/SSL Client Credentials and optionally trusted root Certificate
 * Authorities. If @c pemRootCerts is nil, the default CA Certificates bundled with gRPC will be
 * used.
 */
+ (BOOL)setTLSPEMRootCerts:(nullable NSString *)pemRootCerts
            withPrivateKey:(nullable NSString *)pemPrivateKey
             withCertChain:(nullable NSString *)pemCertChain
                   forHost:(nonnull NSString *)host
                     error:(NSError *_Nullable *_Nullable)errorPtr;

@end
