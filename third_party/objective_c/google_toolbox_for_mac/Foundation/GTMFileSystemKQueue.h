//
//  GTMFileSystemKQueue.h
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

#import <Foundation/Foundation.h>
#import "GTMDefines.h"

#import <sys/event.h>  // for kqueue() and kevent and the NOTE_* constants

//  Event constants
enum {
  kGTMFileSystemKQueueDeleteEvent = NOTE_DELETE,
  kGTMFileSystemKQueueWriteEvent = NOTE_WRITE,
  kGTMFileSystemKQueueExtendEvent = NOTE_EXTEND,
  kGTMFileSystemKQueueAttributeChangeEvent = NOTE_ATTRIB,
  kGTMFileSystemKQueueLinkChangeEvent = NOTE_LINK,
  kGTMFileSystemKQueueRenameEvent = NOTE_RENAME,
  kGTMFileSystemKQueueRevokeEvent = NOTE_REVOKE,
  kGTMFileSystemKQueueAllEvents = NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND |
                                  NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME |
                                  NOTE_REVOKE,
};
typedef unsigned int GTMFileSystemKQueueEvents;

// GTMFileSystemKQueue.
//
// This is a very simple, easy-to-use class for registering handlers that get
// called when a events happen to a given file system path.
//
// The default runloop for the first path kqueued is used for notification
// delivery, so keep that in mind when you're using this class.  This class
// explicitly does not handle arbitrary runloops and threading.
//
NS_CLASS_DEPRECATED(10_2, 10_6, 2_0, 4_0, "Use libdispatch with DISPATCH_SOURCE_TYPE_VNODE source.")
@interface GTMFileSystemKQueue : NSObject {
 @private
  NSString *path_;
  int fd_;
  GTMFileSystemKQueueEvents events_;
  BOOL acrossReplace_;
  GTM_WEAK id target_;
  SEL action_;
}

// |path| is the full path to monitor.  |events| is a combination of events
// listed above that you want notification of.  |acrossReplace| will cause this
// object to reattach when a the file is deleted & recreated or moved out of the
// way and a new one put in place.  |selector| should be of the signature:
//    - (void)fileSystemKQueue:(GTMFileSystemKQueue *)fskq
//                      events:(GTMFileSystemKQueueEvents)events;
// where the events can be one or more of the events listed above ORed together.
//
// NOTE: |acrossReplace| is not fool proof.  If the file is renamed/deleted,
// then the object will make one attempt at the time it gets the rename/delete
// to reopen the file.  If the new file has not been created, no more action is
// taken.  To handle the file coming into existance later, you need to monitor
// the directory in some other way.
- (id)initWithPath:(NSString *)path
         forEvents:(GTMFileSystemKQueueEvents)events
     acrossReplace:(BOOL)acrossReplace
            target:(id)target
            action:(SEL)action;

- (NSString *)path;

@end
