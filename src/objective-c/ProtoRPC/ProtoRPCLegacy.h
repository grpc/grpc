#import <GRPCClient/GRPCCallLegacy.h>

@class GRPCProtoMethod;
@class GRXWriter;
@protocol GRXWriteable;

__attribute__((deprecated("Please use GRPCProtoCall."))) @interface ProtoRPC
    : GRPCCall

      /**
       * host parameter should not contain the scheme (http:// or https://), only the name or IP
       * addr and the port number, for example @"localhost:5050".
       */
      -
      (instancetype)initWithHost : (NSString *)host method
    : (GRPCProtoMethod *)method requestsWriter : (GRXWriter *)requestsWriter responseClass
    : (Class)responseClass responsesWriteable
    : (id<GRXWriteable>)responsesWriteable NS_DESIGNATED_INITIALIZER;

- (void)start;
@end

/**
 * This subclass is empty now. Eventually we'll remove ProtoRPC class
 * to avoid potential naming conflict
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    @interface GRPCProtoCall
    : ProtoRPC
#pragma clang diagnostic pop

      @end

          /**
           * Generate an NSError object that represents a failure in parsing a proto class. For gRPC
           * internal use only.
           */
          NSError *
          ErrorForBadProto(id proto, Class expectedClass, NSError *parsingError);
