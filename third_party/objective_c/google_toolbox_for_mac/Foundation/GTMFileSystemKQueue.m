//
//  GTMFileSystemKQueue.m
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

#import "GTMFileSystemKQueue.h"
#import <unistd.h>
#import "GTMDefines.h"
#import "GTMDebugSelectorValidation.h"
#import "GTMTypeCasting.h"

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMFileSystemKQueue
#pragma clang diagnostic ignored "-Wdeprecated-implementations"

// File descriptor for the kqueue that will hold all of our file system events.
static int gFileSystemKQueueFileDescriptor = 0;

// A wrapper around the kqueue file descriptor so we can put it into a
// runloop.
static CFSocketRef gRunLoopSocket = NULL;


@interface GTMFileSystemKQueue (PrivateMethods)
- (void)notify:(GTMFileSystemKQueueEvents)eventFFlags;
- (void)addFileDescriptorMonitor:(int)fd;
- (int)registerWithKQueue;
- (void)unregisterWithKQueue;
@end


@implementation GTMFileSystemKQueue

-(id)init {
  // Folks shouldn't call init directly, so they get what they deserve.
  _GTMDevLog(@"Don't call init, use "
              @"initWithPath:forEvents:acrossReplace:target:action:");
  return [self initWithPath:nil forEvents:0 acrossReplace:NO
                     target:nil action:nil];
}


- (id)initWithPath:(NSString *)path
         forEvents:(GTMFileSystemKQueueEvents)events
     acrossReplace:(BOOL)acrossReplace
            target:(id)target
            action:(SEL)action {

  if ((self = [super init])) {

    fd_ = -1;
    path_ = [path copy];
    events_ = events;
    acrossReplace_ = acrossReplace;

    target_ = target;  // Don't retain since target will most likely retain us.
    action_ = action;
    if ([path_ length] == 0 || !events_ || !target_ || !action_) {
      [self release];
      return nil;
    }

    // Make sure it imples what we expect
    GTMAssertSelectorNilOrImplementedWithArguments(target_,
                                                   action_,
                                                   @encode(GTMFileSystemKQueue*),
                                                   @encode(GTMFileSystemKQueueEvents),
                                                   NULL);

    fd_ = [self registerWithKQueue];
    if (fd_ < 0) {
      [self release];
      return nil;
    }
  }
  return self;
}

- (void)dealloc {
  [self unregisterWithKQueue];
  [path_ release];

  [super dealloc];
}

- (NSString *)path {
  return path_;
}

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMFileSystemKQueue
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

// Cribbed from Advanced Mac OS X Programming.
static void SocketCallBack(CFSocketRef socketref, CFSocketCallBackType type,
                           CFDataRef address, const void *data, void *info) {
  // We're using CFRunLoop calls here. Even when used on the main thread, they
  // don't trigger the draining of the main application's autorelease pool that
  // NSRunLoop provides. If we're used in a UI-less app, this means that
  // autoreleased objects would never go away, so we provide our own pool here.
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  // We want to read as many events as possible so loop on the kevent call
  // till the kqueue is empty.
  int events = -1;
  do {
    // We wouldn't be here if CFSocket didn't think there was data on
    // |gFileSystemKQueueFileDescriptor|. However, since this callback is tied
    // to the runloop, if [... unregisterWithKQueue] was called before a runloop
    // spin we may now be looking at an empty queue (remember,
    // |gFileSystemKQueueFileDescriptor| isn't a normal descriptor).

    // Try to consume one event with an immediate timeout.
    struct kevent event;
    const struct timespec noWait = { 0, 0 };
    events = kevent(gFileSystemKQueueFileDescriptor, NULL, 0, &event, 1, &noWait);

    if (events == 1) {
      GTMFileSystemKQueue *fskq = GTM_STATIC_CAST(GTMFileSystemKQueue,
                                                  event.udata);
      [fskq notify:event.fflags];
    } else if (events == -1) {
      _GTMDevLog(@"could not pick up kqueue event.  Errno %d", errno);  // COV_NF_LINE
    } else {
      // |events| is zero, either we've drained the kqueue or CFSocket was
      // notified and then the events went away before we had a chance to see
      // them.
    }
  } while (events > 0);

  [pool drain];
}

#pragma clang diagnostic pop

// Cribbed from Advanced Mac OS X Programming
- (void)addFileDescriptorMonitor:(int)fd {
  _GTMDevAssert(gRunLoopSocket == NULL, @"socket should be NULL at this point");

  CFSocketContext context = { 0, NULL, NULL, NULL, NULL };
  gRunLoopSocket = CFSocketCreateWithNative(kCFAllocatorDefault,
                                            fd,
                                            kCFSocketReadCallBack,
                                            SocketCallBack,
                                            &context);
  if (gRunLoopSocket == NULL) {
    _GTMDevLog(@"could not CFSocketCreateWithNative");  // COV_NF_LINE
    goto bailout;   // COV_NF_LINE
  }

  CFRunLoopSourceRef rls;
  rls = CFSocketCreateRunLoopSource(NULL, gRunLoopSocket, 0);
  if (rls == NULL) {
    _GTMDevLog(@"could not create a run loop source");  // COV_NF_LINE
    goto bailout;  // COV_NF_LINE
  }

  CFRunLoopAddSource(CFRunLoopGetCurrent(), rls,
                     kCFRunLoopDefaultMode);
  CFRelease(rls);

 bailout:
  return;

}

// Returns the FD we got in registering
- (int)registerWithKQueue {

  // Make sure we have our kqueue.
  if (gFileSystemKQueueFileDescriptor == 0) {
    gFileSystemKQueueFileDescriptor = kqueue();

    if (gFileSystemKQueueFileDescriptor == -1) {
      // COV_NF_START
      _GTMDevLog(@"could not make filesystem kqueue.  Errno %d", errno);
      return -1;
      // COV_NF_END
    }

    // Add the kqueue file descriptor to the runloop.
    [self addFileDescriptorMonitor:gFileSystemKQueueFileDescriptor];
  }

  int newFD = open([path_ fileSystemRepresentation], O_EVTONLY, 0);
  if (newFD >= 0) {

    // Add a new event for the file.
    struct kevent filter;
    EV_SET(&filter, newFD, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
           events_, 0, self);

    const struct timespec noWait = { 0, 0 };
    if (kevent(gFileSystemKQueueFileDescriptor, &filter, 1, NULL, 0, &noWait) == -1) {
      // COV_NF_START
      _GTMDevLog(@"could not add event for path %@.  Errno %d", path_, errno);
      close(newFD);
      newFD = -1;
      // COV_NF_END
    }
  }

  return newFD;
}

- (void)unregisterWithKQueue {
  // Short-circuit cases where we didn't actually register a kqueue event.
  if (fd_ < 0) return;

  struct kevent filter;
  EV_SET(&filter, fd_, EVFILT_VNODE, EV_DELETE, 0, 0, self);

  const struct timespec noWait = { 0, 0 };
  if (kevent(gFileSystemKQueueFileDescriptor, &filter, 1, NULL, 0, &noWait) != 0) {
    _GTMDevLog(@"could not remove event for path %@.  Errno %d", path_, errno);  // COV_NF_LINE
  }

  // Now close the file down
  close(fd_);
  fd_ = -1;
}


- (void)notify:(GTMFileSystemKQueueEvents)eventFFlags {

  // Some notifications get a little bit of overhead first

  if (eventFFlags & NOTE_REVOKE) {
    // COV_NF_START - no good way to do this in a unittest
    // Assume revoke means unmount and give up
    [self unregisterWithKQueue];
    // COV_NF_END
  }

  if (eventFFlags & NOTE_DELETE) {
    [self unregisterWithKQueue];
    if (acrossReplace_) {
      fd_ = [self registerWithKQueue];
    }
  }

  if (eventFFlags & NOTE_RENAME) {
    // If we're doing it across replace, we move to the new fd for the new file
    // that might have come onto the path.  if we aren't doing accross replace,
    // nothing to do, just stay on the file.
    if (acrossReplace_) {
      int newFD = [self registerWithKQueue];
      if (newFD >= 0) {
        [self unregisterWithKQueue];
        fd_ = newFD;
      }
    }
  }

  // Now, fire the selector
  NSMethodSignature *methodSig = [target_ methodSignatureForSelector:action_];
  _GTMDevAssert(methodSig != nil, @"failed to get the signature?");
  NSInvocation *invocation
    = [NSInvocation invocationWithMethodSignature:methodSig];
  [invocation setTarget:target_];
  [invocation setSelector:action_];
  [invocation setArgument:&self atIndex:2];
  [invocation setArgument:&eventFFlags atIndex:3];
  [invocation invoke];
}

@end

#pragma clang diagnostic pop
