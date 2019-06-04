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

#import "CacheInterceptor.h"

@implementation RequestCacheEntry {
 @protected
  NSString *_path;
  id<NSObject> _message;
}

@synthesize path = _path;
@synthesize message = _message;

- (instancetype)initWithPath:(NSString *)path message:(id)message {
  if ((self = [super init])) {
    _path = [path copy];
    _message = [message copy];
  }
  return self;
}

- (id)copyWithZone:(NSZone *)zone {
  return [[RequestCacheEntry allocWithZone:zone] initWithPath:_path message:_message];
}

- (BOOL)isEqual:(id)object {
  if ([self class] != [object class]) return NO;
  RequestCacheEntry *rhs = (RequestCacheEntry *)object;
  return ([_path isEqualToString:rhs.path] && [_message isEqual:rhs.message]);
}

- (NSUInteger)hash {
  return _path.hash ^ _message.hash;
}

@end

@implementation MutableRequestCacheEntry

@dynamic path;
@dynamic message;

- (void)setPath:(NSString *)path {
  _path = [path copy];
}

- (void)setMessage:(id)message {
  _message = [message copy];
}

@end

@implementation ResponseCacheEntry {
 @protected
  NSDate *_deadline;
  NSDictionary *_headers;
  id _message;
  NSDictionary *_trailers;
}

@synthesize deadline = _deadline;
@synthesize headers = _headers;
@synthesize message = _message;
@synthesize trailers = _trailers;

- (instancetype)initWithDeadline:(NSDate *)deadline
                         headers:(NSDictionary *)headers
                         message:(id)message
                        trailers:(NSDictionary *)trailers {
  if (([super init])) {
    _deadline = [deadline copy];
    _headers = [[NSDictionary alloc] initWithDictionary:headers copyItems:YES];
    _message = [message copy];
    _trailers = [[NSDictionary alloc] initWithDictionary:trailers copyItems:YES];
  }
  return self;
}

- (id)copyWithZone:(NSZone *)zone {
  return [[ResponseCacheEntry allocWithZone:zone] initWithDeadline:_deadline
                                                           headers:_headers
                                                           message:_message
                                                          trailers:_trailers];
}

@end

@implementation MutableResponseCacheEntry

@dynamic deadline;
@dynamic headers;
@dynamic message;
@dynamic trailers;

- (void)setDeadline:(NSDate *)deadline {
  _deadline = [deadline copy];
}

- (void)setHeaders:(NSDictionary *)headers {
  _headers = [[NSDictionary alloc] initWithDictionary:headers copyItems:YES];
}

- (void)setMessage:(id)message {
  _message = [message copy];
}

- (void)setTrailers:(NSDictionary *)trailers {
  _trailers = [[NSDictionary alloc] initWithDictionary:trailers copyItems:YES];
}

@end

@implementation CacheContext {
  NSCache<RequestCacheEntry *, ResponseCacheEntry *> *_cache;
}

- (instancetype)init {
  if ((self = [super init])) {
    _cache = [[NSCache alloc] init];
  }
  return self;
}

- (GRPCInterceptor *)createInterceptorWithManager:(GRPCInterceptorManager *)interceptorManager {
  return [[CacheInterceptor alloc] initWithInterceptorManager:interceptorManager cacheContext:self];
}

- (ResponseCacheEntry *)getCachedResponseForRequest:(RequestCacheEntry *)request {
  ResponseCacheEntry *response = nil;
  @synchronized(self) {
    response = [_cache objectForKey:request];
    if ([response.deadline timeIntervalSinceNow] < 0) {
      [_cache removeObjectForKey:request];
      response = nil;
    }
  }
  return response;
}

- (void)setCachedResponse:(ResponseCacheEntry *)response forRequest:(RequestCacheEntry *)request {
  @synchronized(self) {
    [_cache setObject:response forKey:request];
  }
}

@end

@implementation CacheInterceptor {
  GRPCInterceptorManager *_manager;
  CacheContext *_context;
  dispatch_queue_t _dispatchQueue;

  BOOL _cacheable;
  BOOL _writeMessageSeen;
  BOOL _readMessageSeen;
  GRPCCallOptions *_callOptions;
  GRPCRequestOptions *_requestOptions;
  id _requestMessage;
  MutableRequestCacheEntry *_request;
  MutableResponseCacheEntry *_response;
}

- (dispatch_queue_t)requestDispatchQueue {
  return _dispatchQueue;
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

- (instancetype)initWithInterceptorManager:(GRPCInterceptorManager *_Nonnull)intercepterManager
                              cacheContext:(CacheContext *_Nonnull)cacheContext {
  if ((self = [super initWithInterceptorManager:intercepterManager
                           requestDispatchQueue:dispatch_get_main_queue()
                          responseDispatchQueue:dispatch_get_main_queue()])) {
    _manager = intercepterManager;
    _context = cacheContext;
    _dispatchQueue = dispatch_queue_create(NULL, DISPATCH_QUEUE_SERIAL);

    _cacheable = YES;
    _writeMessageSeen = NO;
    _readMessageSeen = NO;
    _request = nil;
    _response = nil;
  }
  return self;
}

- (void)startWithRequestOptions:(GRPCRequestOptions *)requestOptions
                    callOptions:(GRPCCallOptions *)callOptions {
  if (requestOptions.safety != GRPCCallSafetyCacheableRequest) {
    _cacheable = NO;
    [_manager startNextInterceptorWithRequest:requestOptions callOptions:callOptions];
  } else {
    _requestOptions = [requestOptions copy];
    _callOptions = [callOptions copy];
  }
}

- (void)writeData:(id)data {
  if (!_cacheable) {
    [_manager writeNextInterceptorWithData:data];
  } else {
    NSAssert(!_writeMessageSeen, @"CacheInterceptor does not support streaming call");
    if (_writeMessageSeen) {
      NSLog(@"CacheInterceptor does not support streaming call");
    }
    _writeMessageSeen = YES;
    _requestMessage = [data copy];
  }
}

- (void)finish {
  if (!_cacheable) {
    [_manager finishNextInterceptor];
  } else {
    _request = [[MutableRequestCacheEntry alloc] init];
    _request.path = _requestOptions.path;
    _request.message = [_requestMessage copy];
    _response = [[_context getCachedResponseForRequest:_request] copy];
    if (!_response) {
      [_manager startNextInterceptorWithRequest:_requestOptions callOptions:_callOptions];
      [_manager writeNextInterceptorWithData:_requestMessage];
      [_manager finishNextInterceptor];
    } else {
      [_manager forwardPreviousInterceptorWithInitialMetadata:_response.headers];
      [_manager forwardPreviousInterceptorWithData:_response.message];
      [_manager forwardPreviousInterceptorCloseWithTrailingMetadata:_response.trailers error:nil];
      [_manager shutDown];
    }
  }
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  if (_cacheable) {
    NSDate *deadline = nil;
    for (NSString *key in initialMetadata) {
      if ([key.lowercaseString isEqualToString:@"cache-control"]) {
        NSArray *cacheControls = [initialMetadata[key] componentsSeparatedByString:@","];
        for (NSString *option in cacheControls) {
          NSString *trimmedOption =
              [option stringByTrimmingCharactersInSet:[NSCharacterSet
                                                          characterSetWithCharactersInString:@" "]];
          if ([trimmedOption.lowercaseString isEqualToString:@"no-cache"] ||
              [trimmedOption.lowercaseString isEqualToString:@"no-store"] ||
              [trimmedOption.lowercaseString isEqualToString:@"no-transform"]) {
            _cacheable = NO;
            break;
          } else if ([trimmedOption.lowercaseString hasPrefix:@"max-age="]) {
            NSArray<NSString *> *components = [trimmedOption componentsSeparatedByString:@"="];
            if (components.count == 2) {
              NSUInteger maxAge = components[1].intValue;
              deadline = [NSDate dateWithTimeIntervalSinceNow:maxAge];
            }
          }
        }
      }
    }
    if (_cacheable) {
      _response = [[MutableResponseCacheEntry alloc] init];
      _response.headers = [initialMetadata copy];
      _response.deadline = deadline;
    }
  }
  [_manager forwardPreviousInterceptorWithInitialMetadata:initialMetadata];
}

- (void)didReceiveData:(id)data {
  if (_cacheable) {
    NSAssert(!_readMessageSeen, @"CacheInterceptor does not support streaming call");
    if (_readMessageSeen) {
      NSLog(@"CacheInterceptor does not support streaming call");
    }
    _readMessageSeen = YES;
    _response.message = [data copy];
  }
  [_manager forwardPreviousInterceptorWithData:data];
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  if (error == nil && _cacheable) {
    _response.trailers = [trailingMetadata copy];
    [_context setCachedResponse:_response forRequest:_request];
    NSLog(@"Write cache for %@", _request);
  }
  [_manager forwardPreviousInterceptorCloseWithTrailingMetadata:trailingMetadata error:error];
  [_manager shutDown];
}

@end
