//
//  GTMKeyValueAnimation.m
//  Copyright 2011 Google Inc.
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

#import "GTMKeyValueAnimation.h"


@implementation GTMKeyValueAnimation

- (id)initWithTarget:(id)target keyPath:(NSString*)keyPath {
  if ((self = [super init])) {
    target_ = [target retain];
    keyPath_ = [keyPath copy];
    [self setAnimationBlockingMode:NSAnimationNonblocking];
  }
  return self;
}

- (void)dealloc {
  [target_ release];
  [keyPath_ release];
  [super dealloc];
}

- (void)setCurrentProgress:(NSAnimationProgress)progress {
  [super setCurrentProgress:progress];
  [target_ setValue:[NSNumber numberWithDouble:[self currentValue]]
         forKeyPath:keyPath_];
}

- (id)target {
  return target_;
}

- (NSString *)keyPath {
  return keyPath_;
}

@end
