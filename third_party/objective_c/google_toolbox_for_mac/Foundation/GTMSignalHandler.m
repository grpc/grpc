//
//  GTMSignalHandler.m
//
//  Copyright 2008 Google Inc.
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

#import "GTMSignalHandler.h"
#import "GTMDefines.h"

#import <dispatch/dispatch.h>
#import "GTMDebugSelectorValidation.h"

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMSignalHandler
#pragma clang diagnostic ignored "-Wdeprecated-implementations"

// Simplifying assumption: No more than one handler for a particular signal is
// alive at a time.  When the second signal is registered, kqueue just updates
// the info about the first signal, which makes -dealloc time complicated (what
// happens when handler1(SIGUSR1) is released before handler2(SIGUSR1)?).  This
// could be solved by having one kqueue per signal, or keeping a list of
// handlers interested in a particular signal, but not really worth it for apps
// that register the handlers at startup and don't change them.

@implementation GTMSignalHandler

-(id)init {
  // Folks shouldn't call init directly, so they get what they deserve.
  _GTMDevLog(@"Don't call init, use "
             @"initWithSignal:target:action:");
  return [self initWithSignal:0 target:nil action:NULL];
}

- (id)initWithSignal:(int)signo
              target:(id)target
              action:(SEL)action {

  if ((self = [super init])) {

    if (signo == 0) {
      [self release];
      return nil;
    }

    GTMAssertSelectorNilOrImplementedWithArguments(target,
                                                   action,
                                                   @encode(int),
                                                   NULL);

    // We're handling this signal via libdispatch, so turn off the usual signal
    // handling.
    if (signal(signo, SIG_IGN) == SIG_ERR) {
      _GTMDevLog(@"could not ignore signal %d.  Errno %d", signo, errno);  // COV_NF_LINE
    }

    if (action != NULL) {
      signalSource_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,
                                             signo,
                                             0,
                                             dispatch_get_main_queue());
      dispatch_source_set_event_handler(signalSource_, ^(void) {
        NSMethodSignature *methodSig
            = [target methodSignatureForSelector:action];
        _GTMDevAssert(methodSig != nil, @"failed to get the signature?");
        NSInvocation *invocation
            = [NSInvocation invocationWithMethodSignature:methodSig];
        [invocation setTarget:target];
        [invocation setSelector:action];
        [invocation setArgument:(void*)&signo atIndex:2];
        [invocation invoke];
      });
      dispatch_resume(signalSource_);
    }
  }
  return self;
}

- (void)dealloc {
  [self invalidate];
  [super dealloc];
}


- (void)invalidate {
  if (signalSource_) {
    dispatch_source_cancel(signalSource_);
    dispatch_release(signalSource_);
    signalSource_ = NULL;
  }
}

@end

#pragma clang diagnostic pop
