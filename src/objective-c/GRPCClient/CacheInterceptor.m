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

/**
 * Queue protocol
 */
@protocol LRUQueue

- (NSUInteger)size;
- (void)enqueue:(nonnull RequestCacheEntry *)entry;
- (void)updateUse:(nonnull RequestCacheEntry *)entry;
- (nullable RequestCacheEntry *)evict;
- (nullable RequestCacheEntry *)popFront;

@end

/**
 * ArrayQueue
 */
@class RequestCacheEntry;
@interface ArrayQueue : NSObject<LRUQueue>

@end

@implementation ArrayQueue {
  NSMutableArray<RequestCacheEntry*> *_array;
}

- (nullable instancetype)init {
  if ((self = [super init])) {
    _array = [[NSMutableArray alloc] init];
    if (!_array) {
      self = nil;
    }
  }
  return self;
}

- (NSUInteger)size {
  return _array.count;
}

- (void)enqueue:(RequestCacheEntry *)entry {
  [_array addObject:entry];
}

- (void)updateUse:(RequestCacheEntry *)entry {
  // if the entry is not in the queue, return (cuz behavior undefined)
  if (![_array containsObject:entry]) { return; }
  NSUInteger index = [_array indexOfObject:entry];
  for (NSUInteger i = index; i < _array.count - 1; ++i) {
    _array[i] = _array[i + 1];
  }
  _array[_array.count - 1] = entry;
}

- (RequestCacheEntry *)evict {
  RequestCacheEntry *toEvict = [_array firstObject];
  [_array removeObjectAtIndex:0];
  return toEvict;
}

- (nullable RequestCacheEntry *)popFront {
  RequestCacheEntry *toPop = [_array lastObject];
  [_array removeLastObject];
  return toPop;
}

@end

/**
 * Node
 */
@interface Node : NSObject

@property(nullable, readwrite) Node *prev;
@property(nullable, readwrite) __weak Node *next;
@property(nonnull, readonly) RequestCacheEntry *entry;
- (nullable instancetype)initWithPrevNode:(nullable Node *)prev
                                 nextNode:(nullable Node *)next
                            forCacheEntry:(nonnull RequestCacheEntry *)entry;

@end

@implementation Node {
  RequestCacheEntry *_entry;
}

@synthesize entry = _entry;

- (nullable instancetype)initWithPrevNode:(Node *)prev
                                 nextNode:(Node *)next
                            forCacheEntry:(nonnull RequestCacheEntry *)entry{
  if ((self = [super init])) {
    self.prev = prev;
    self.next = next;
    _entry = entry;
  }
  return self;
}


@end

/**
 * LinkedListQueue
 */
@interface LinkedListQueue : NSObject<LRUQueue>

@end

@implementation LinkedListQueue {
  Node *_head;
  Node *_tail;
  NSUInteger _size;
  NSMutableDictionary<RequestCacheEntry *, Node*> *_map;
}

- (nullable instancetype)init {
  if ((self = [super init])) {
    _map = [[NSMutableDictionary alloc] init];
    if (!_map) {
      self = nil;
    } else {
      _head = _tail = nil;
      _size = 0;
    }
  }
  return self;
}

- (void)enqueue:(RequestCacheEntry *)entry {
  Node *node = [[Node alloc] initWithPrevNode:nil nextNode:_head forCacheEntry:entry];
  if (_head) {
    _head.prev = node;
  } else {
    _tail = node;
  }
  _head = node;
  [_map setObject:node forKey:entry];
  ++_size;
}

- (RequestCacheEntry *)evict {
  Node *evictNode = _tail;
  RequestCacheEntry *toEvict = nil;
  if (evictNode) {
    _tail = evictNode.prev;
    if (_tail) {
      _tail.next = nil;
    }
    toEvict = evictNode.entry;
    [_map removeObjectForKey:toEvict];
    --_size;
    if (_size == 0) { _head = nil; }
  }
  return toEvict;
}

- (nullable RequestCacheEntry *)popFront {
  Node *popNode = _head;
  RequestCacheEntry *toPop = nil;
  if (popNode) {
    _head = popNode.next;
    if (_head) {
      _head.prev = nil;
    }
    toPop = popNode.entry;
    [_map removeObjectForKey:toPop];
    --_size;
    if (_size == 0) { _tail = nil; }
  }
  return toPop;
}

- (NSUInteger)size {
  return _size;
}

- (void)updateUse:(RequestCacheEntry *)entry {
  Node *updateNode = [_map objectForKey:entry];
  if (updateNode == _head) { return; }
  if (!updateNode) { return; }
  if (updateNode.next) { updateNode.next.prev = updateNode.prev; }
  if (updateNode.prev) { updateNode.prev.next = updateNode.next; }
  if (updateNode == _tail) { _tail = updateNode.prev; }
  updateNode.next = _head;
  updateNode.prev = nil;
  if (_head) { _head.prev = updateNode; }
  _head = updateNode;
}



@end


/*******************************************
 * Cache Interceptor Implementation Begins *
 *******************************************/
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
  if (![object isKindOfClass:[self class]]) return NO;
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

/**
 * Cache Context
 */
@implementation CacheContext {
  NSMutableDictionary<RequestCacheEntry *, ResponseCacheEntry *> *_cache;
  id<LRUQueue> _queue;
  NSUInteger _maxCount; // cache size limit
  NSMutableDictionary<RequestCacheEntry *, ResponseCacheEntry *> *_staleEntries;
}

@synthesize staleEntries = _staleEntries;

- (instancetype)init {
  if ((self = [super init])) {
    _queue = [[ArrayQueue alloc] init]; // ArrayQueue or LinkedListQueue
    _cache = [[NSMutableDictionary alloc] init];
    _staleEntries = [[NSMutableDictionary alloc] init];
    _maxCount = 10; // defaults to 10
  }
  return self;
}

- (instancetype)initWithSize:(NSUInteger)size {
  if (!size) {
    return nil;
  }
  if (self = [self init]) {
    _maxCount = size;
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
    if (response) { // cache hit
      [_queue updateUse:request];
    }
    if ([response.deadline timeIntervalSinceNow] < 0) { // needs revalidation
      [_staleEntries setObject:response forKey:request];
      [_queue popFront];
      [_cache removeObjectForKey:request];
      response = nil;
    }
    NSAssert(_cache.count <= _maxCount, @"Number of cache exceeds set limit.");
  }
  return response;
}

- (void)setCachedResponse:(ResponseCacheEntry *)response forRequest:(RequestCacheEntry *)request {
  @synchronized(self) {
    if ([_cache objectForKey:request] != nil) { // cache hit
      [_queue updateUse:request];
    } else { // cache miss
      [_staleEntries removeObjectForKey:request];
      if ([_queue size] == _maxCount) {
        RequestCacheEntry *toEvict = [_queue evict];
        [_cache removeObjectForKey:toEvict];
      }
      [_queue enqueue:request];
    }
    [_cache setObject:response forKey:request];
    NSAssert(_cache.count <= _maxCount, @"Number of cache exceeds set limit.");
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

- (instancetype)initWithInterceptorManager:(GRPCInterceptorManager *)interceptorManager
                      requestDispatchQueue:(dispatch_queue_t)requestDispatchQueue
                     responseDispatchQueue:(dispatch_queue_t)responseDispatchQueue {
  self = [super initWithInterceptorManager:interceptorManager
                      requestDispatchQueue:requestDispatchQueue
                     responseDispatchQueue:responseDispatchQueue];
  return self;
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
      ResponseCacheEntry *staleResponse = nil;
      @synchronized (_context) {
        staleResponse = [_context.staleEntries objectForKey:_request];
      }
      if (staleResponse) { // stale response that needs revalidation
        GRPCMutableCallOptions *options = [_callOptions mutableCopy];
        NSMutableDictionary *metadata = [options.initialMetadata mutableCopy];
        NSString *eTag = [staleResponse.headers objectForKey:@"etag"];
        if (eTag) {
          [metadata setObject:eTag forKey:@"if-none-match"];
        } else {
          NSString *lastModifiedDate = [staleResponse.headers objectForKey:@"last-modified"];
          if (!lastModifiedDate) {
            lastModifiedDate = [staleResponse.headers objectForKey:@"date"];
          }
          if (lastModifiedDate) {
            [metadata setObject:lastModifiedDate forKey:@"if-modified-since"];
          }
        }
        options.initialMetadata = metadata;
        _callOptions = options;
      }
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
    if ([initialMetadata objectForKey:@"cache-control"]) {
      NSArray *cacheControls = [initialMetadata[@"cache-control"] componentsSeparatedByString:@","];
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
    if (!deadline) {
      deadline = [[NSDate alloc] init];
    }
    
    NSDictionary *metadata = nil;
    MutableResponseCacheEntry *response = nil;
    // Replacing entries with validated stale data
    if ([initialMetadata[@"status"] isEqualToString:@"304"]) {
      @synchronized (_context) {
        response = [_context.staleEntries objectForKey:_request];
      }
      NSMutableDictionary *updatedMetadata = [response.headers mutableCopy];
      for (NSString *key in initialMetadata) {
        [updatedMetadata setObject:initialMetadata[key] forKey:key];
      }
      metadata = updatedMetadata;
    } else {
      response = [[MutableResponseCacheEntry alloc] init];
      metadata = [initialMetadata copy];
    }
    
    if (_cacheable) {
      _response = response;
      _response.headers = metadata;
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
      _cacheable = NO;
    }
    _readMessageSeen = YES;
    
    // if 304 then no need to copy data (which is btw empty)
    if (![_response.headers[@"status"] isEqualToString:@"304"]) {
      _response.message = [data copy];
    }
  }
  [_manager forwardPreviousInterceptorWithData:data];
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  if (error == nil && _cacheable) {
    NSDictionary *metaData = nil;
    if ([_response.headers[@"status"] isEqualToString:@"304"]) {
      NSMutableDictionary *updatedMetadata = [_response.trailers mutableCopy];
      for (NSString *key in trailingMetadata) {
        [updatedMetadata setObject:trailingMetadata[key] forKey:key];
      }
      metaData = updatedMetadata;
    } else {
      metaData = [trailingMetadata copy];
    }
    _response.trailers = metaData;
    [_context setCachedResponse:_response forRequest:_request]; // where stale entry gets removed and cache updated
    NSLog(@"Write cache for %@", _request);
  }
  [_manager forwardPreviousInterceptorCloseWithTrailingMetadata:trailingMetadata error:error];
  [_manager shutDown];
}

@end
