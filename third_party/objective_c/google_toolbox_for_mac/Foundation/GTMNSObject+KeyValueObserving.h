//
//  GTMNSObject+KeyValueObserving.h
//
//  Copyright 2009 Google Inc.
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

//
//  MAKVONotificationCenter.h
//  MAKVONotificationCenter
//
//  Created by Michael Ash on 10/15/08.
//

// This code is based on code by Michael Ash.
// Please see his excellent writeup at
// http://www.mikeash.com/?page=pyblog/key-value-observing-done-right.html
// You may also be interested in this writeup:
// http://www.dribin.org/dave/blog/archives/2008/09/24/proper_kvo_usage/
// and the discussion on cocoa-dev that is linked to at the end of it.

#import <Foundation/Foundation.h>

// If you read the articles above you will see that doing KVO correctly
// is actually pretty tricky, and that Apple's documentation may not be
// completely clear as to how things should be used. Use the methods below
// to make things a little easier instead of the stock addObserver,
// removeObserver methods.
// Selector should have the following signature:
// - (void)observeNotification:(GTMKeyValueChangeNotification *)notification
@interface NSObject (GTMKeyValueObservingAdditions)

// Use this instead of [NSObject addObserver:forKeyPath:options:context:]
- (void)gtm_addObserver:(id)observer
             forKeyPath:(NSString *)keyPath
               selector:(SEL)selector
               userInfo:(id)userInfo
                options:(NSKeyValueObservingOptions)options;
// Use this instead of [NSObject removeObserver:forKeyPath:]
- (void)gtm_removeObserver:(id)observer
                forKeyPath:(NSString *)keyPath
                  selector:(SEL)selector;

// Use this to have |self| stop observing all keypaths on all objects.
- (void)gtm_stopObservingAllKeyPaths;

@end

// This is the class that is sent to your notification selector as an
// argument.
@interface GTMKeyValueChangeNotification : NSObject <NSCopying> {
 @private
  NSString *keyPath_;
  id object_;
  id userInfo_;
  NSDictionary *change_;
}

- (NSString *)keyPath;
- (id)object;
- (id)userInfo;
- (NSDictionary *)change;
@end
