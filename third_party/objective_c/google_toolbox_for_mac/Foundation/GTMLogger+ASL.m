//
//  GTMLogger+ASL.m
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

#import "GTMLogger+ASL.h"
#import "GTMDefines.h"


@implementation GTMLogger (GTMLoggerASLAdditions)

+ (id)standardLoggerWithASL {
  id me = [self standardLogger];
  [me setWriter:[[[GTMLogASLWriter alloc] init] autorelease]];
  [me setFormatter:[[[GTMLogASLFormatter alloc] init] autorelease]];
  return me;
}

@end


@implementation GTMLogASLWriter

+ (id)aslWriter {
  return [[[self alloc] initWithClientClass:nil facility:nil] autorelease];
}

+ (id)aslWriterWithFacility:(NSString *)facility {
  return [[[self alloc] initWithClientClass:nil facility:facility] autorelease];
}

- (id)init {
  return [self initWithClientClass:nil facility:nil];
}

- (id)initWithClientClass:(Class)clientClass facility:(NSString *)facility {
  if ((self = [super init])) {
    aslClientClass_ = clientClass;
    if (aslClientClass_ == nil) {
      aslClientClass_ = [GTMLoggerASLClient class];
    }
    facility_ = [facility copy];
  }
  return self;
}

- (void)dealloc {
  [facility_ release];
  [super dealloc];
}

- (void)logMessage:(NSString *)msg level:(GTMLoggerLevel)level {
  // Because |facility_| is an argument to asl_open() we must store a separate
  // one for each facility in thread-local storage.
  static NSString *const kASLClientKey = @"GTMLoggerASLClient";
  NSString *key = kASLClientKey;
  if (facility_) {
    key = [NSString stringWithFormat:@"GTMLoggerASLClient-%@", facility_];
  }

  // Lookup the ASL client in the thread-local storage dictionary
  NSMutableDictionary *tls = [[NSThread currentThread] threadDictionary];
  GTMLoggerASLClient *client = [tls objectForKey:key];

  // If the ASL client wasn't found (e.g., the first call from this thread),
  // then create it and store it in the thread-local storage dictionary
  if (client == nil) {
    client = [[[aslClientClass_ alloc] initWithFacility:facility_] autorelease];
    [tls setObject:client forKey:key];
  }

  // Map the GTMLoggerLevel level to an ASL level.
  int aslLevel = ASL_LEVEL_INFO;
  switch (level) {
    case kGTMLoggerLevelUnknown:
    case kGTMLoggerLevelDebug:
    case kGTMLoggerLevelInfo:
      aslLevel = ASL_LEVEL_NOTICE;
      break;
    case kGTMLoggerLevelError:
      aslLevel = ASL_LEVEL_ERR;
      break;
    case kGTMLoggerLevelAssert:
      aslLevel = ASL_LEVEL_ALERT;
      break;
  }

  [client log:msg level:aslLevel];
}

@end  // GTMLogASLWriter


@implementation GTMLogASLFormatter

#if !defined(__clang__) && (__GNUC__*10+__GNUC_MINOR__ >= 42)
// Some versions of GCC (4.2 and below AFAIK) aren't great about supporting
// -Wmissing-format-attribute
// when the function is anything more complex than foo(NSString *fmt, ...).
// You see the error inside the function when you turn ... into va_args and
// attempt to call another function (like vsprintf for example).
// So we just shut off the warning for this function.
#pragma GCC diagnostic ignored "-Wmissing-format-attribute"
#endif  // !__clang__

- (NSString *)stringForFunc:(NSString *)func
                 withFormat:(NSString *)fmt
                     valist:(va_list)args
                      level:(GTMLoggerLevel)level {
  return [NSString stringWithFormat:@"%@ %@",
           [self prettyNameForFunc:func],
           // |super| has guard for nil |fmt| and |args|
           [super stringForFunc:func withFormat:fmt valist:args level:level]];
}

#if !defined(__clang__) && (__GNUC__*10+__GNUC_MINOR__ >= 42)
#pragma GCC diagnostic error "-Wmissing-format-attribute"
#endif  // !__clang__

@end  // GTMLogASLFormatter


@implementation GTMLoggerASLClient

- (id)init {
  return [self initWithFacility:nil];
}

- (id)initWithFacility:(NSString *)facility {
  if ((self = [super init])) {
    client_ = asl_open(NULL, [facility UTF8String], 0);
    if (client_ == NULL) {
      // COV_NF_START - no real way to test this
      [self release];
      return nil;
      // COV_NF_END
    }
#if TARGET_OS_IPHONE
    // On iOS we need to flag the messages as available for read so
    // asl_search() can see our own output.
    msgOptions_ = asl_new(ASL_TYPE_MSG);
    if ((msgOptions_ == NULL) ||
        (asl_set(msgOptions_,
                 ASL_KEY_READ_UID,
                 [[NSString stringWithFormat:@"%d", getuid()] UTF8String]) != 0)) {
      // COV_NF_START - no real way to test this
      [self release];
      return nil;
      // COV_NF_END
    }
#endif
  }
  return self;
}

- (void)dealloc {
  if (msgOptions_ != NULL) asl_free(msgOptions_);
  if (client_ != NULL) asl_close(client_);
  [super dealloc];
}

// We don't test this one line because we don't want to pollute actual system
// logs with test messages.
// COV_NF_START
- (void)log:(NSString *)msg level:(int)level {
  asl_log(client_, msgOptions_, level, "%s", [msg UTF8String]);
}
// COV_NF_END

@end  // GTMLoggerASLClient
