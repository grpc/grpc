//
//  GTMNSThread+Blocks.h
//
//  Copyright 2012 Google Inc.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not
//  use this file except in compliance with the License.  You may obtain a copy
//  of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
//  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
//  License for the specific language governing permissions and limitations under
//  the License.
//
// Based on http://www.informit.com/blogs/blog.aspx?uk=Ask-Big-Nerd-Ranch-Blocks-in-Objective-C

#import <Foundation/Foundation.h>
#import "GTMDefines.h"

// Extension to NSThread to work with blocks.

#if NS_BLOCKS_AVAILABLE

@interface NSThread (GTMBlocksAdditions)

// If self is not the current thread, the block will be called asynchronously
// and this method returns immediately.
// If self is the current thread, the block will be performed immediately, and
// then this method will return.
- (void)gtm_performBlock:(void (^)(void))block;

- (void)gtm_performWaitingUntilDone:(BOOL)waitDone block:(void (^)(void))block;
+ (void)gtm_performBlockInBackground:(void (^)(void))block;
@end

#endif  // NS_BLOCKS_AVAILABLE

// A simple thread that does nothing but runs a runloop.
// That means that it can handle performBlock and performSelector calls.
@interface GTMSimpleWorkerThread : NSThread

// If called from another thread, blocks until worker thread is done.
// If called from the worker thread it is equivalent to cancel and
// returns immediately.
// Note that "stop" will set the isCancelled on the thread.
- (void)stop;

@end
