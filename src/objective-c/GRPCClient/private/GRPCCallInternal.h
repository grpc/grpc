
#import <GRPCClient/GRPCInterceptor.h>

NS_ASSUME_NONNULL_BEGIN

@interface GRPCCall2Internal : NSObject<GRPCInterceptorInterface>

- (instancetype)init;

- (void)setResponseHandler:(id<GRPCResponseHandler>)responseHandler;

- (void)startWithRequestOptions:(GRPCRequestOptions *)requestOptions
                    callOptions:(nullable GRPCCallOptions *)callOptions;

- (void)writeData:(NSData *)data;

- (void)finish;

- (void)cancel;

- (void)receiveNextMessages:(NSUInteger)numberOfMessages;

@end

NS_ASSUME_NONNULL_END
