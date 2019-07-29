/*
 *
 * Copyright 2019 gRPC authors.
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

/**
 * Safety remark of a gRPC method as defined in RFC 2616 Section 9.1
 */
typedef NS_ENUM(NSUInteger, GRPCCallSafety) {
  /**
   * Signal that there is no guarantees on how the call affects the server
   * state.
   */
  GRPCCallSafetyDefault = 0,
  /** Signal that the call is idempotent. gRPC is free to use PUT verb. */
  GRPCCallSafetyIdempotentRequest = 1,
  /**
   * Signal that the call is cacheable and will not affect server state. gRPC is
   * free to use GET verb.
   */
  GRPCCallSafetyCacheableRequest = 2,
};

// Compression algorithm to be used by a gRPC call
typedef NS_ENUM(NSUInteger, GRPCCompressionAlgorithm) {
  GRPCCompressNone = 0,
  GRPCCompressDeflate,
  GRPCCompressGzip,
  GRPCStreamCompressGzip,
};

// GRPCCompressAlgorithm is deprecated; use GRPCCompressionAlgorithm
typedef GRPCCompressionAlgorithm GRPCCompressAlgorithm;

/** The transport to be used by a gRPC call */
typedef NS_ENUM(NSUInteger, GRPCTransportType) {
  GRPCTransportTypeDefault = 0,
  /** gRPC internal HTTP/2 stack with BoringSSL */
  GRPCTransportTypeChttp2BoringSSL = 0,
  /** Cronet stack */
  GRPCTransportTypeCronet,
  /** Insecure channel. FOR TEST ONLY! */
  GRPCTransportTypeInsecure,
};
