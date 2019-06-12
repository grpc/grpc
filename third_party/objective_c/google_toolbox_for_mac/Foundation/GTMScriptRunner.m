//
//  GTMScriptRunner.m
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

#import <unistd.h>
#import <fcntl.h>
#import <sys/select.h>
#import <sys/time.h>  // Must be include for select() if using modules.

#import "GTMScriptRunner.h"
#import "GTMDefines.h"

static BOOL LaunchNSTaskCatchingExceptions(NSTask *task);

@interface GTMScriptRunner (PrivateMethods)
- (NSTask *)interpreterTaskWithAdditionalArgs:(NSArray *)args;
@end

@implementation GTMScriptRunner

+ (GTMScriptRunner *)runner {
  return [[[self alloc] init] autorelease];
}

+ (GTMScriptRunner *)runnerWithBash {
  return [self runnerWithInterpreter:@"/bin/bash"];
}

+ (GTMScriptRunner *)runnerWithPerl {
  return [self runnerWithInterpreter:@"/usr/bin/perl"];
}

+ (GTMScriptRunner *)runnerWithPython {
  return [self runnerWithInterpreter:@"/usr/bin/python"];
}

+ (GTMScriptRunner *)runnerWithInterpreter:(NSString *)interp {
  return [self runnerWithInterpreter:interp withArgs:nil];
}

+ (GTMScriptRunner *)runnerWithInterpreter:(NSString *)interp withArgs:(NSArray *)args {
  return [[[self alloc] initWithInterpreter:interp withArgs:args] autorelease];
}

- (id)init {
  return [self initWithInterpreter:nil];
}

- (id)initWithInterpreter:(NSString *)interp {
  return [self initWithInterpreter:interp withArgs:nil];
}

- (id)initWithInterpreter:(NSString *)interp withArgs:(NSArray *)args {
  if ((self = [super init])) {
    trimsWhitespace_ = YES;
    interpreter_ = [interp copy];
    interpreterArgs_ = [args retain];
    if (!interpreter_) {
      interpreter_ = @"/bin/sh";
    }
  }
  return self;
}

- (void)dealloc {
  [environment_ release];
  [interpreter_ release];
  [interpreterArgs_ release];
  [super dealloc];
}

- (NSString *)description {
  return [NSString stringWithFormat:@"%@<%p>{ interpreter = '%@', args = %@, environment = %@ }",
          [self class], self, interpreter_, interpreterArgs_, environment_];
}

- (NSString *)run:(NSString *)cmds {
  return [self run:cmds standardError:nil];
}

- (NSString *)run:(NSString *)cmds standardError:(NSString **)err {
  if (!cmds) return nil;

  // Convert input to data
  NSData *inputData = nil;
  if ([cmds length]) {
    inputData = [cmds dataUsingEncoding:NSUTF8StringEncoding];
    if (![inputData length]) {
      return nil;
    }
  }

  NSTask *task = [self interpreterTaskWithAdditionalArgs:nil];
  NSFileHandle *toTask = [[task standardInput] fileHandleForWriting];
  NSFileHandle *fromTask = [[task standardOutput] fileHandleForReading];
  NSFileHandle *errTask = [[task standardError] fileHandleForReading];

  if (!LaunchNSTaskCatchingExceptions(task)) {
    return nil;
  }

  // We're reading an writing to child task via pipes, which is full of
  // deadlock dangers. We use non-blocking IO and select() to handle.
  // Note that error handling below isn't quite right since
  // [task terminate] may not always kill the child. But we want to keep
  // this simple.

  // Setup for select()
  size_t inputOffset = 0;
  int toFD = -1;
  int fromFD = -1;
  int errFD = -1;
  int selectMaxFD = -1;
  fd_set fdToReadSet, fdToWriteSet;
  FD_ZERO(&fdToReadSet);
  FD_ZERO(&fdToWriteSet);
  if ([inputData length]) {
    toFD = [toTask fileDescriptor];
    FD_SET(toFD, &fdToWriteSet);
    selectMaxFD = MAX(toFD, selectMaxFD);
    int flags = fcntl(toFD, F_GETFL);
    if ((flags == -1) ||
        (fcntl(toFD, F_SETFL, flags | O_NONBLOCK) == -1)) {
      [task terminate];
      return nil;
    }
  } else {
    [toTask closeFile];
  }
  fromFD = [fromTask fileDescriptor];
  FD_SET(fromFD, &fdToReadSet);
  selectMaxFD = MAX(fromFD, selectMaxFD);
  errFD = [errTask fileDescriptor];
  FD_SET(errFD, &fdToReadSet);
  selectMaxFD = MAX(errFD, selectMaxFD);

  // Convert to string only at the end, so we don't get partial UTF8 sequences.
  NSMutableData *mutableOut = [NSMutableData data];
  NSMutableData *mutableErr = [NSMutableData data];

  // Communicate till we've removed everything from the select() or timeout
  while (([inputData length] && FD_ISSET(toFD, &fdToWriteSet)) ||
         ((fromFD != -1) && FD_ISSET(fromFD, &fdToReadSet)) ||
         ((errFD != -1) && FD_ISSET(errFD, &fdToReadSet))) {
    // select() on a modifiable copy, we use originals to track state
    fd_set selectReadSet;
    FD_COPY(&fdToReadSet, &selectReadSet);
    fd_set selectWriteSet;
    FD_COPY(&fdToWriteSet, &selectWriteSet);
    int selectResult = select(selectMaxFD + 1, &selectReadSet, &selectWriteSet,
                              NULL, NULL);
    if (selectResult < 0) {
      if ((errno == EAGAIN) || (errno == EINTR)) {
        continue;  // No change to |fdToReadSet| or |fdToWriteSet|
      } else {
        [task terminate];
        return nil;
      }
    }
    // STDIN
    if ([inputData length] && FD_ISSET(toFD, &selectWriteSet)) {
      // Use a multiple of PIPE_BUF so that we exercise the non-blocking
      // aspect of this IO.
      size_t writeSize = PIPE_BUF * 4;
      if (([inputData length] - inputOffset) < writeSize) {
        writeSize = [inputData length] - inputOffset;
      }
      if (writeSize > 0) {
        // We are non-blocking, so as much as the pipe will take will be
        // written.
        ssize_t writtenSize = 0;
        do {
          writtenSize = write(toFD, (char *)[inputData bytes] + inputOffset,
                              writeSize);
        } while ((writtenSize) < 0 && (errno == EINTR));
        if ((writtenSize < 0) && (errno != EAGAIN)) {
          [task terminate];
          return nil;
        }
        inputOffset += writeSize;
      }
      if (inputOffset >= [inputData length]) {
        FD_CLR(toFD, &fdToWriteSet);
        [toTask closeFile];
      }
    }
    // STDOUT
    if ((fromFD != -1) && FD_ISSET(fromFD, &selectReadSet)) {
      char readBuf[1024];
      ssize_t readSize = 0;
      do {
        readSize = read(fromFD, readBuf, 1024);
      } while (readSize < 0 && ((errno == EAGAIN) || (errno == EINTR)));
      if (readSize < 0) {
          [task terminate];
          return nil;
      } else if (readSize == 0) {
        FD_CLR(fromFD, &fdToReadSet);  // Hit EOF
      } else {
        [mutableOut appendBytes:readBuf length:readSize];
      }
    }
    // STDERR
    if ((errFD != -1) && FD_ISSET(errFD, &selectReadSet)) {
      char readBuf[1024];
      ssize_t readSize = 0;
      do {
        readSize = read(errFD, readBuf, 1024);
      } while (readSize < 0 && ((errno == EAGAIN) || (errno == EINTR)));
      if (readSize < 0) {
          [task terminate];
          return nil;
      } else if (readSize == 0) {
        FD_CLR(errFD, &fdToReadSet);  // Hit EOF
      } else {
        [mutableErr appendBytes:readBuf length:readSize];
      }
    }
  }
  // All filehandles closed, wait.
  [task waitUntilExit];

  NSString *outString = [[[NSString alloc] initWithData:mutableOut
                                               encoding:NSUTF8StringEncoding]
                            autorelease];
  NSString *errString = [[[NSString alloc] initWithData:mutableErr
                                               encoding:NSUTF8StringEncoding]
                            autorelease];;
  if (trimsWhitespace_) {
    NSCharacterSet *set = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    outString = [outString stringByTrimmingCharactersInSet:set];
    if (err) {
      errString = [errString stringByTrimmingCharactersInSet:set];
    }
  }

  // let folks test for nil instead of @""
  if ([outString length] < 1) {
    outString = nil;
  }

  // Handle returning standard error if |err| is not nil
  if (err) {
    // let folks test for nil instead of @""
    if ([errString length] < 1) {
      *err = nil;
    } else {
      *err = errString;
    }
  }

  return outString;
}

- (NSString *)runScript:(NSString *)path {
  return [self runScript:path withArgs:nil];
}

- (NSString *)runScript:(NSString *)path withArgs:(NSArray *)args {
  return [self runScript:path withArgs:args standardError:nil];
}

- (NSString *)runScript:(NSString *)path withArgs:(NSArray *)args standardError:(NSString **)err {
  if (!path) return nil;

  NSArray *scriptPlusArgs = [[NSArray arrayWithObject:path] arrayByAddingObjectsFromArray:args];
  NSTask *task = [self interpreterTaskWithAdditionalArgs:scriptPlusArgs];
  NSFileHandle *fromTask = [[task standardOutput] fileHandleForReading];

  if (!LaunchNSTaskCatchingExceptions(task)) {
    return nil;
  }

  NSData *outData = [fromTask readDataToEndOfFile];
  NSString *output = [[[NSString alloc] initWithData:outData
                                            encoding:NSUTF8StringEncoding] autorelease];

  // Handle returning standard error if |err| is not nil
  if (err) {
    NSFileHandle *stderror = [[task standardError] fileHandleForReading];
    NSData *errData = [stderror readDataToEndOfFile];
    *err = [[[NSString alloc] initWithData:errData
                                  encoding:NSUTF8StringEncoding] autorelease];
    if (trimsWhitespace_) {
      *err = [*err stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    }

    // let folks test for nil instead of @""
    if ([*err length] < 1) {
      *err = nil;
    }
  }

  [task terminate];

  if (trimsWhitespace_) {
    output = [output stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  }

  // let folks test for nil instead of @""
  if ([output length] < 1) {
    output = nil;
  }

  return output;
}

- (NSDictionary *)environment {
  return environment_;
}

- (void)setEnvironment:(NSDictionary *)newEnv {
  [environment_ autorelease];
  environment_ = [newEnv retain];
}

- (BOOL)trimsWhitespace {
  return trimsWhitespace_;
}

- (void)setTrimsWhitespace:(BOOL)trim {
  trimsWhitespace_ = trim;
}

@end


@implementation GTMScriptRunner (PrivateMethods)

- (NSTask *)interpreterTaskWithAdditionalArgs:(NSArray *)args {
  NSTask *task = [[[NSTask alloc] init] autorelease];
  [task setLaunchPath:interpreter_];
  [task setStandardInput:[NSPipe pipe]];
  [task setStandardOutput:[NSPipe pipe]];
  [task setStandardError:[NSPipe pipe]];

  // If |environment_| is nil, then use an empty dictionary, otherwise use
  // environment_ exactly.
  [task setEnvironment:(environment_
                        ? environment_
                        : [NSDictionary dictionary])];

  // Build args to interpreter.  The format is:
  //   interp [args-to-interp] [script-name [args-to-script]]
  NSArray *allArgs = nil;
  if (interpreterArgs_) {
    allArgs = interpreterArgs_;
  }
  if (args) {
    allArgs = allArgs ? [allArgs arrayByAddingObjectsFromArray:args] : args;
  }
  if (allArgs){
    [task setArguments:allArgs];
  }

  return task;
}

@end

static BOOL LaunchNSTaskCatchingExceptions(NSTask *task) {
  BOOL isOK = YES;
  @try {
    [task launch];
  } @catch (id ex) {
    isOK = NO;
    _GTMDevLog(@"Failed to launch interpreter '%@' due to: %@",
               [task launchPath], ex);
  }
  return isOK;
}
