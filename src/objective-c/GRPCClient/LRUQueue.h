//
//  Queue.m
//  InterceptorSample
//
//  Created by Tony Lu on 6/12/19.
//  Copyright © 2019 gRPC. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "CacheInterceptor.h"

@protocol LRUQueue

- (NSUInteger)size;
- (void)enqueue:(nonnull RequestCacheEntry *)entry;
- (void)updateUse:(nonnull RequestCacheEntry *)entry;
- (nullable RequestCacheEntry *)evict;

@end

@interface ArrayQueue : NSObject<LRUQueue>

@end

@interface Node : NSObject

@property(nullable, readwrite) Node *prev;
@property(nullable, readwrite) __weak Node *next;
@property(nonnull, readonly) RequestCacheEntry *entry;
- (nullable instancetype)initWithPrevNode:(nullable Node *)prev
                                 nextNode:(nullable Node *)next
                            forCacheEntry:(nonnull RequestCacheEntry *)entry;

@end

@interface LinkedListQueue : NSObject<LRUQueue>

@end
