//
//  GTMSenTestCase.m
//
//  Copyright 2007-2008 Google Inc.
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

#import "GTMSenTestCase.h"

#import <unistd.h>
#import <objc/message.h>

#if GTM_IPHONE_SDK
#import <UIKit/UIKit.h>
#endif  // GTM_IPHONE_SDK

#if GTM_IPHONE_SDK && !GTM_USING_XCTEST
#import <stdarg.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-implementations"

@interface NSException (GTMSenTestPrivateAdditions)
+ (NSException *)failureInFile:(NSString *)filename
                        atLine:(int)lineNumber
                        reason:(NSString *)reason;
@end

@implementation NSException (GTMSenTestPrivateAdditions)
+ (NSException *)failureInFile:(NSString *)filename
                        atLine:(int)lineNumber
                        reason:(NSString *)reason {
  NSDictionary *userInfo =
    [NSDictionary dictionaryWithObjectsAndKeys:
     [NSNumber numberWithInteger:lineNumber], SenTestLineNumberKey,
     filename, SenTestFilenameKey,
     nil];

  return [self exceptionWithName:SenTestFailureException
                          reason:reason
                        userInfo:userInfo];
}
@end

@implementation NSException (GTMSenTestAdditions)

+ (NSException *)failureInFile:(NSString *)filename
                        atLine:(int)lineNumber
               withDescription:(NSString *)formatString, ... {

  NSString *testDescription = @"";
  if (formatString) {
    va_list vl;
    va_start(vl, formatString);
    testDescription =
      [[[NSString alloc] initWithFormat:formatString arguments:vl] autorelease];
    va_end(vl);
  }

  NSString *reason = testDescription;

  return [self failureInFile:filename atLine:lineNumber reason:reason];
}

+ (NSException *)failureInCondition:(NSString *)condition
                             isTrue:(BOOL)isTrue
                             inFile:(NSString *)filename
                             atLine:(int)lineNumber
                    withDescription:(NSString *)formatString, ... {

  NSString *testDescription = @"";
  if (formatString) {
    va_list vl;
    va_start(vl, formatString);
    testDescription =
      [[[NSString alloc] initWithFormat:formatString arguments:vl] autorelease];
    va_end(vl);
  }

  NSString *reason = [NSString stringWithFormat:@"'%@' should be %s. %@",
                      condition, isTrue ? "false" : "true", testDescription];

  return [self failureInFile:filename atLine:lineNumber reason:reason];
}

+ (NSException *)failureInEqualityBetweenObject:(id)left
                                      andObject:(id)right
                                         inFile:(NSString *)filename
                                         atLine:(int)lineNumber
                                withDescription:(NSString *)formatString, ... {

  NSString *testDescription = @"";
  if (formatString) {
    va_list vl;
    va_start(vl, formatString);
    testDescription =
      [[[NSString alloc] initWithFormat:formatString arguments:vl] autorelease];
    va_end(vl);
  }

  NSString *reason =
    [NSString stringWithFormat:@"'%@' should be equal to '%@'. %@",
     [left description], [right description], testDescription];

  return [self failureInFile:filename atLine:lineNumber reason:reason];
}

+ (NSException *)failureInEqualityBetweenValue:(NSValue *)left
                                      andValue:(NSValue *)right
                                  withAccuracy:(NSValue *)accuracy
                                        inFile:(NSString *)filename
                                        atLine:(int)lineNumber
                               withDescription:(NSString *)formatString, ... {

  NSString *testDescription = @"";
  if (formatString) {
    va_list vl;
    va_start(vl, formatString);
    testDescription =
      [[[NSString alloc] initWithFormat:formatString arguments:vl] autorelease];
    va_end(vl);
  }

  NSString *reason;
  if (accuracy) {
    reason =
      [NSString stringWithFormat:@"'%@' should be equal to '%@'. %@",
       left, right, testDescription];
  } else {
    reason =
      [NSString stringWithFormat:@"'%@' should be equal to '%@' +/-'%@'. %@",
       left, right, accuracy, testDescription];
  }

  return [self failureInFile:filename atLine:lineNumber reason:reason];
}

+ (NSException *)failureInRaise:(NSString *)expression
                         inFile:(NSString *)filename
                         atLine:(int)lineNumber
                withDescription:(NSString *)formatString, ... {

  NSString *testDescription = @"";
  if (formatString) {
    va_list vl;
    va_start(vl, formatString);
    testDescription =
      [[[NSString alloc] initWithFormat:formatString arguments:vl] autorelease];
    va_end(vl);
  }

  NSString *reason = [NSString stringWithFormat:@"'%@' should raise. %@",
                      expression, testDescription];

  return [self failureInFile:filename atLine:lineNumber reason:reason];
}

+ (NSException *)failureInRaise:(NSString *)expression
                      exception:(NSException *)exception
                         inFile:(NSString *)filename
                         atLine:(int)lineNumber
                withDescription:(NSString *)formatString, ... {

  NSString *testDescription = @"";
  if (formatString) {
    va_list vl;
    va_start(vl, formatString);
    testDescription =
      [[[NSString alloc] initWithFormat:formatString arguments:vl] autorelease];
    va_end(vl);
  }

  NSString *reason;
  if ([[exception name] isEqualToString:SenTestFailureException]) {
    // it's our exception, assume it has the right description on it.
    reason = [exception reason];
  } else {
    // not one of our exception, use the exceptions reason and our description
    reason = [NSString stringWithFormat:@"'%@' raised '%@'. %@",
              expression, [exception reason], testDescription];
  }

  return [self failureInFile:filename atLine:lineNumber reason:reason];
}

@end

NSString *STComposeString(NSString *formatString, ...) {
  NSString *reason = @"";
  if (formatString) {
    va_list vl;
    va_start(vl, formatString);
    reason =
      [[[NSString alloc] initWithFormat:formatString arguments:vl] autorelease];
    va_end(vl);
  }
  return reason;
}

NSString *const SenTestFailureException = @"SenTestFailureException";
NSString *const SenTestFilenameKey = @"SenTestFilenameKey";
NSString *const SenTestLineNumberKey = @"SenTestLineNumberKey";

@interface SenTestCase (SenTestCasePrivate)
// our method of logging errors
+ (void)printException:(NSException *)exception fromTestName:(NSString *)name;
@end

@implementation SenTestCase
+ (id)testCaseWithInvocation:(NSInvocation *)anInvocation {
  return [[[self alloc] initWithInvocation:anInvocation] autorelease];
}

- (id)initWithInvocation:(NSInvocation *)anInvocation {
  if ((self = [super init])) {
    invocation_ = [anInvocation retain];
  }
  return self;
}

- (void)dealloc {
  [invocation_ release];
  [super dealloc];
}

- (void)failWithException:(NSException*)exception {
  [exception raise];
}

- (void)setUp {
}

- (void)performTest {
  @try {
    [self invokeTest];
  } @catch (NSException *exception) {
    [[self class] printException:exception
                    fromTestName:NSStringFromSelector([self selector])];
    [exception raise];
  }
}

- (NSInvocation *)invocation {
  return invocation_;
}

- (SEL)selector {
  return [invocation_ selector];
}

+ (void)printException:(NSException *)exception fromTestName:(NSString *)name {
  NSDictionary *userInfo = [exception userInfo];
  NSString *filename = [userInfo objectForKey:SenTestFilenameKey];
  NSNumber *lineNumber = [userInfo objectForKey:SenTestLineNumberKey];
  NSString *className = NSStringFromClass([self class]);
  if ([filename length] == 0) {
    filename = @"Unknown.m";
  }
  fprintf(stderr, "%s:%ld: error: -[%s %s] : %s\n",
          [filename UTF8String],
          (long)[lineNumber integerValue],
          [className UTF8String],
          [name UTF8String],
          [[exception reason] UTF8String]);
  fflush(stderr);
}

- (void)invokeTest {
  NSException *e = nil;
  @try {
    // Wrap things in autorelease pools because they may
    // have an STMacro in their dealloc which may get called
    // when the pool is cleaned up
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    // We don't log exceptions here, instead we let the person that called
    // this log the exception.  This ensures they are only logged once but the
    // outer layers get the exceptions to report counts, etc.
    @try {
      [self setUp];
      @try {
        NSInvocation *invocation = [self invocation];
#if GTM_IPHONE_SIMULATOR
        // We don't call [invocation invokeWithTarget:self]; because of
        // Radar 8081169: NSInvalidArgumentException can't be caught
        // It turns out that on iOS4 (and 3.2) exceptions thrown inside an
        // [invocation invoke] on the simulator cannot be caught.
        // http://openradar.appspot.com/8081169
        ((void (*)(id, SEL))objc_msgSend)(self, [invocation selector]);
#else
        [invocation invokeWithTarget:self];
#endif
      } @catch (NSException *exception) {
        e = [exception retain];
      }
      [self tearDown];
    } @catch (NSException *exception) {
      e = [exception retain];
    }
    [pool release];
  } @catch (NSException *exception) {
    e = [exception retain];
  }
  if (e) {
    [e autorelease];
    [e raise];
  }
}

- (void)tearDown {
}

- (NSString *)description {
  // This matches the description OCUnit would return to you
  return [NSString stringWithFormat:@"-[%@ %@]", [self class],
          NSStringFromSelector([self selector])];
}

// Used for sorting methods below
static NSInteger MethodSort(id a, id b, void *context) {
  NSInvocation *invocationA = a;
  NSInvocation *invocationB = b;
  const char *nameA = sel_getName([invocationA selector]);
  const char *nameB = sel_getName([invocationB selector]);
  return strcmp(nameA, nameB);
}


+ (NSArray *)testInvocations {
  NSMutableArray *invocations = nil;
  // Need to walk all the way up the parent classes collecting methods (in case
  // a test is a subclass of another test).
  Class senTestCaseClass = [SenTestCase class];
  for (Class currentClass = self;
       currentClass && (currentClass != senTestCaseClass);
       currentClass = class_getSuperclass(currentClass)) {
    unsigned int methodCount;
    Method *methods = class_copyMethodList(currentClass, &methodCount);
    if (methods) {
      // This handles disposing of methods for us even if an exception should fly.
      [NSData dataWithBytesNoCopy:methods
                           length:sizeof(Method) * methodCount];
      if (!invocations) {
        invocations = [NSMutableArray arrayWithCapacity:methodCount];
      }
      for (size_t i = 0; i < methodCount; ++i) {
        Method currMethod = methods[i];
        SEL sel = method_getName(currMethod);
        char *returnType = NULL;
        const char *name = sel_getName(sel);
        // If it starts with test, takes 2 args (target and sel) and returns
        // void run it.
        if (strstr(name, "test") == name) {
          returnType = method_copyReturnType(currMethod);
          if (returnType) {
            // This handles disposing of returnType for us even if an
            // exception should fly. Length +1 for the terminator, not that
            // the length really matters here, as we never reference inside
            // the data block.
            [NSData dataWithBytesNoCopy:returnType
                                 length:strlen(returnType) + 1];
          }
        }
        // TODO: If a test class is a subclass of another, and they reuse the
        // same selector name (ie-subclass overrides it), this current loop
        // and test here will cause cause it to get invoked twice.  To fix this
        // the selector would have to be checked against all the ones already
        // added, so it only gets done once.
        if (returnType  // True if name starts with "test"
            && strcmp(returnType, @encode(void)) == 0
            && method_getNumberOfArguments(currMethod) == 2) {
          NSMethodSignature *sig = [self instanceMethodSignatureForSelector:sel];
          NSInvocation *invocation
            = [NSInvocation invocationWithMethodSignature:sig];
          [invocation setSelector:sel];
          [invocations addObject:invocation];
        }
      }
    }
  }
  // Match SenTestKit and run everything in alphbetical order.
  [invocations sortUsingFunction:&MethodSort context:nil];
  return invocations;
}

@end

#pragma clang diagnostic pop

#endif  // GTM_IPHONE_SDK && !GTM_USING_XCTEST

#pragma clang diagnostic push
#if !GTM_USING_XCTEST
// Turn off the deprecated warning when GTMTestCase is still based on SenTest.
#pragma clang diagnostic ignored "-Wdeprecated-implementations"
#endif  // !GTM_USING_XCTEST

@implementation GTMTestCase

+ (BOOL)isAbstractTestCase {
  NSString *name = NSStringFromClass(self);
  return [name rangeOfString:@"AbstractTest"].location != NSNotFound;
}

+ (NSArray *)testInvocations {
  NSArray *invocations = @[];
  if (![self isAbstractTestCase]) {
    invocations = [super testInvocations];
  }
  return invocations;
}

@end

#pragma clang diagnostic pop
