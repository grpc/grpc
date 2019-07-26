/**
 * Safety remark of a gRPC method as defined in RFC 2616 Section 9.1
 */
typedef NS_ENUM(NSUInteger, GRPCCallSafety) {
  /** Signal that there is no guarantees on how the call affects the server state. */
  GRPCCallSafetyDefault = 0,
  /** Signal that the call is idempotent. gRPC is free to use PUT verb. */
  GRPCCallSafetyIdempotentRequest = 1,
  /**
   * Signal that the call is cacheable and will not affect server state. gRPC is free to use GET
   * verb.
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

