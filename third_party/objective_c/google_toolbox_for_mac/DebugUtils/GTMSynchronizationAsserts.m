//
//  GTMSyncAsserts.m
//
//  Copyright 2016 Google Inc.
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

#import "GTMSynchronizationAsserts.h"

#if DEBUG

@implementation GTMSyncMonitorInternal

- (instancetype)initWithSynchronizationObject:(id)object
                               allowRecursive:(BOOL)allowRecursive
                                 functionName:(const char *)functionName {
  self = [super init];
  if (self) {
    // In the thread's dictionary, we keep a counted set of the names
    // of functions that are synchronizing on the object.
    Class threadKey = [GTMSyncMonitorInternal class];
    _objectKey = [NSValue valueWithNonretainedObject:object];
    _functionName = functionName;

    NSMutableDictionary *threadDict = [NSThread currentThread].threadDictionary;
    NSMutableDictionary *counters = threadDict[threadKey];
    if (counters == nil) {
      counters = [NSMutableDictionary dictionary];
      threadDict[(id)threadKey] = counters;
    }
    NSCountedSet *functionNamesCounter = counters[_objectKey];
    NSUInteger numberOfSyncingFunctions = functionNamesCounter.count;

    if (!allowRecursive) {
      BOOL isTopLevelSyncScope = (numberOfSyncingFunctions == 0);
      NSArray *stack = [NSThread callStackSymbols];
      _GTMDevAssert(isTopLevelSyncScope,
                    @"*** Recursive sync on %@ at %s; previous sync at %@\n%@",
                    [object class], functionName, functionNamesCounter.allObjects,
                    [stack subarrayWithRange:NSMakeRange(1, stack.count - 1)]);
    }

    if (!functionNamesCounter) {
      functionNamesCounter = [NSCountedSet set];
      counters[_objectKey] = functionNamesCounter;
    }
    [functionNamesCounter addObject:@(functionName)];
  }
  return self;
}

- (void)dealloc {
  Class threadKey = [GTMSyncMonitorInternal class];

  NSMutableDictionary *threadDict = [NSThread currentThread].threadDictionary;
  NSMutableDictionary *counters = threadDict[threadKey];
  NSCountedSet *functionNamesCounter = counters[_objectKey];
  NSString *functionNameStr = @(_functionName);
  NSUInteger numberOfSyncsByThisFunction = [functionNamesCounter countForObject:functionNameStr];
  NSArray *stack = [NSThread callStackSymbols];
  _GTMDevAssert(numberOfSyncsByThisFunction > 0, @"Sync not found on %@ at %s\n%@",
                [_objectKey.nonretainedObjectValue class], _functionName,
                [stack subarrayWithRange:NSMakeRange(1, stack.count - 1)]);
  [functionNamesCounter removeObject:functionNameStr];
  if (functionNamesCounter.count == 0) {
    [counters removeObjectForKey:_objectKey];
  }
}

+ (NSArray *)functionsHoldingSynchronizationOnObject:(id)object {
  Class threadKey = [GTMSyncMonitorInternal class];
  NSValue *localObjectKey = [NSValue valueWithNonretainedObject:object];

  NSMutableDictionary *threadDict = [NSThread currentThread].threadDictionary;
  NSMutableDictionary *counters = threadDict[threadKey];
  NSCountedSet *functionNamesCounter = counters[localObjectKey];
  return functionNamesCounter.count > 0 ? functionNamesCounter.allObjects : nil;
}

@end

#endif  // DEBUG
