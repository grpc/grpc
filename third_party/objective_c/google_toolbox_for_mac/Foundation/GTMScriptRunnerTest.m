//
//  GTMScriptRunnerTest.m
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

#import <sys/types.h>
#import <unistd.h>
#import "GTMSenTestCase.h"
#import "GTMScriptRunner.h"

@interface GTMScriptRunnerTest : GTMTestCase {
 @private
  NSString *shScript_;
  NSString *perlScript_;
  NSString *shOutputScript_;
}
@end

@interface GTMScriptRunnerTest (PrivateMethods)
- (void)helperTestBourneShellUsingScriptRunner:(GTMScriptRunner *)sr;
@end

@implementation GTMScriptRunnerTest

- (void)setUp {
  shScript_ = [NSString stringWithFormat:@"/tmp/script_runner_unittest_%d_%d_sh", geteuid(), getpid()];
  [@"#!/bin/sh\n"
   @"i=1\n"
   @"if [ -n \"$1\" ]; then\n"
   @"  i=$1\n"
   @"fi\n"
   @"echo $i\n"
   writeToFile:shScript_ atomically:YES encoding:NSUTF8StringEncoding error:nil];

  perlScript_ = [NSString stringWithFormat:@"/tmp/script_runner_unittest_%d_%d_pl", geteuid(), getpid()];
  [@"#!/usr/bin/perl\n"
   @"use strict;\n"
   @"my $i = 1;\n"
   @"if (defined $ARGV[0]) {\n"
   @"  $i = $ARGV[0];\n"
   @"}\n"
   @"print \"$i\n\"\n"
   writeToFile:perlScript_ atomically:YES encoding:NSUTF8StringEncoding error:nil];

  shOutputScript_ = [NSString stringWithFormat:@"/tmp/script_runner_unittest_err_%d_%d_sh", geteuid(), getpid()];
  [@"#!/bin/sh\n"
   @"if [ \"err\" = \"$1\" ]; then\n"
   @"  echo \" on err \" > /dev/stderr\n"
   @"else\n"
   @"  echo \" on out \"\n"
   @"fi\n"
   writeToFile:shOutputScript_ atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

- (void)tearDown {
  const char *path = [shScript_ fileSystemRepresentation];
  if (path) {
    unlink(path);
  }
  path = [perlScript_ fileSystemRepresentation];
  if (path) {
    unlink(path);
  }
  path = [shOutputScript_ fileSystemRepresentation];
  if (path) {
    unlink(path);
  }
}

- (void)testShCommands {
  GTMScriptRunner *sr = [GTMScriptRunner runner];
  [self helperTestBourneShellUsingScriptRunner:sr];
}

- (void)testBashCommands {
  GTMScriptRunner *sr = [GTMScriptRunner runnerWithBash];
  [self helperTestBourneShellUsingScriptRunner:sr];
}

- (void)testZshCommands {
  GTMScriptRunner *sr = [GTMScriptRunner runnerWithInterpreter:@"/bin/zsh"];
  [self helperTestBourneShellUsingScriptRunner:sr];
}

- (void)testBcCommands {
  GTMScriptRunner *sr = [GTMScriptRunner runnerWithInterpreter:@"/usr/bin/bc"
                                                    withArgs:[NSArray arrayWithObject:@"-lq"]];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *output = nil;

  // Simple expression (NOTE that bc requires that commands end with a newline)
  output = [sr run:@"1 + 2\n"];
  XCTAssertEqualObjects(output, @"3", @"output should equal '3'");

  // Simple expression with variables and multiple statements
  output = [sr run:@"i=1; i+2\n"];
  XCTAssertEqualObjects(output, @"3", @"output should equal '3'");

  // Simple expression with base conversion
  output = [sr run:@"obase=2; 2^5\n"];
  XCTAssertEqualObjects(output, @"100000", @"output should equal '100000'");

  // Simple expression with sine and cosine functions
  output = [sr run:@"scale=3;s(0)+c(0)\n"];
  XCTAssertEqualObjects(output, @"1.000", @"output should equal '1.000'");
}

- (void)testPerlCommands {
  GTMScriptRunner *sr = [GTMScriptRunner runnerWithPerl];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *output = nil;

  // Simple print
  output = [sr run:@"print 'hi'"];
  XCTAssertEqualObjects(output, @"hi", @"output should equal 'hi'");

  // Simple print x4
  output = [sr run:@"print 'A'x4"];
  XCTAssertEqualObjects(output, @"AAAA", @"output should equal 'AAAA'");

  // Simple perl-y stuff
  output = [sr run:@"my $i=0; until ($i++==41){} print $i"];
  XCTAssertEqualObjects(output, @"42", @"output should equal '42'");
}

- (void)testPythonCommands {
  GTMScriptRunner *sr = [GTMScriptRunner runnerWithPython];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *output = nil;

  // Simple print
  output = [sr run:@"print 'hi'"];
  XCTAssertEqualObjects(output, @"hi", @"output should equal 'hi'");

  // Simple python expression
  output = [sr run:@"print '-'.join(['a', 'b', 'c'])"];
  XCTAssertEqualObjects(output, @"a-b-c", @"output should equal 'a-b-c'");
}

- (void)testBashScript {
  GTMScriptRunner *sr = [GTMScriptRunner runnerWithBash];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *output = nil;

  // Simple sh script
  output = [sr runScript:shScript_];
  XCTAssertEqualObjects(output, @"1", @"output should equal '1'");

  // Simple sh script with 1 command line argument
  output = [sr runScript:shScript_ withArgs:[NSArray arrayWithObject:@"2"]];
  XCTAssertEqualObjects(output, @"2", @"output should equal '2'");
}

- (void)testPerlScript {
  GTMScriptRunner *sr = [GTMScriptRunner runnerWithPerl];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *output = nil;

  // Simple Perl script
  output = [sr runScript:perlScript_];
  XCTAssertEqualObjects(output, @"1", @"output should equal '1'");

  // Simple perl script with 1 command line argument
  output = [sr runScript:perlScript_ withArgs:[NSArray arrayWithObject:@"2"]];
  XCTAssertEqualObjects(output, @"2", @"output should equal '2'");
}

- (void)testEnvironment {
  GTMScriptRunner *sr = [GTMScriptRunner runner];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *output = nil;
  NSString *error = nil;
  XCTAssertNil([sr environment], @"should start w/ empty env");

  output = [sr run:@"/usr/bin/env | wc -l" standardError:&error];
  int numVars = [output intValue];
  XCTAssertGreaterThan(numVars, 0,
                       @"numVars should be positive. StdErr %@", error);
  // By default the environment is wiped clean, however shells often add a few
  // of their own env vars after things have been wiped. For example, sh will
  // add about 3 env vars (PWD, _, and SHLVL).
  XCTAssertLessThan(numVars, 5, @"Our env should be almost empty");

  NSDictionary *newEnv = [NSDictionary dictionaryWithObject:@"bar"
                                                     forKey:@"foo"];
  [sr setEnvironment:newEnv];
  output = [sr run:@"/usr/bin/env | wc -l" standardError:&error];
  XCTAssertEqual([output intValue], numVars + 1,
      @"should have one more env var now. StdErr %@", error);

  [sr setEnvironment:nil];
  output = [sr run:@"/usr/bin/env | wc -l" standardError:&error];
  XCTAssertEqual([output intValue], numVars,
       @"should be back down to %d vars. StdErr:%@", numVars, error);
}

- (void)testDescription {
  // make sure description doesn't choke
  GTMScriptRunner *sr = [GTMScriptRunner runner];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  XCTAssertGreaterThan([[sr description] length], (NSUInteger)10,
                       @"expected a description of at least 10 chars");
}

- (void)testRunCommandOutputHandling {
  // Test whitespace trimming & stdout vs. stderr w/ run command api

  GTMScriptRunner *sr = [GTMScriptRunner runnerWithBash];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *output = nil;
  NSString *err = nil;

  // w/o whitespace trimming
  {
    [sr setTrimsWhitespace:NO];
    XCTAssertFalse([sr trimsWhitespace], @"setTrimsWhitespace to NO failed");

    // test stdout
    output = [sr run:@"echo \" on out \"" standardError:&err];
    XCTAssertEqualObjects(output, @" on out \n", @"failed to get stdout output");
    XCTAssertNil(err, @"stderr should have been empty");

    // test stderr
    output = [sr run:@"echo \" on err \" > /dev/stderr" standardError:&err];
    XCTAssertNil(output, @"stdout should have been empty");
    XCTAssertEqualObjects(err, @" on err \n");
  }

  // w/ whitespace trimming
  {
    [sr setTrimsWhitespace:YES];
    XCTAssertTrue([sr trimsWhitespace], @"setTrimsWhitespace to YES failed");

    // test stdout
    output = [sr run:@"echo \" on out \"" standardError:&err];
    XCTAssertEqualObjects(output, @"on out", @"failed to get stdout output");
    XCTAssertNil(err, @"stderr should have been empty");

    // test stderr
    output = [sr run:@"echo \" on err \" > /dev/stderr" standardError:&err];
    XCTAssertNil(output, @"stdout should have been empty");
    XCTAssertEqualObjects(err, @"on err");
  }
}

- (void)testScriptOutputHandling {
  // Test whitespace trimming & stdout vs. stderr w/ script api

  GTMScriptRunner *sr = [GTMScriptRunner runner];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *output = nil;
  NSString *err = nil;

  // w/o whitespace trimming
  {
    [sr setTrimsWhitespace:NO];
    XCTAssertFalse([sr trimsWhitespace], @"setTrimsWhitespace to NO failed");

    // test stdout
    output = [sr runScript:shOutputScript_
                  withArgs:[NSArray arrayWithObject:@"out"]
             standardError:&err];
    XCTAssertEqualObjects(output, @" on out \n");
    XCTAssertNil(err, @"stderr should have been empty");

    // test stderr
    output = [sr runScript:shOutputScript_
                  withArgs:[NSArray arrayWithObject:@"err"]
             standardError:&err];
    XCTAssertNil(output, @"stdout should have been empty");
    XCTAssertEqualObjects(err, @" on err \n");
  }

  // w/ whitespace trimming
  {
    [sr setTrimsWhitespace:YES];
    XCTAssertTrue([sr trimsWhitespace], @"setTrimsWhitespace to YES failed");

    // test stdout
    output = [sr runScript:shOutputScript_
                  withArgs:[NSArray arrayWithObject:@"out"]
             standardError:&err];
    XCTAssertEqualObjects(output, @"on out");
    XCTAssertNil(err, @"stderr should have been empty");

    // test stderr
    output = [sr runScript:shOutputScript_
                  withArgs:[NSArray arrayWithObject:@"err"]
             standardError:&err];
    XCTAssertNil(output, @"stdout should have been empty");
    XCTAssertEqualObjects(err, @"on err");
  }
}

- (void)testBadRunCommandInput {
  GTMScriptRunner *sr = [GTMScriptRunner runner];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *err = nil;

  XCTAssertNil([sr run:nil standardError:&err]);
  XCTAssertNil(err);
}

- (void)testBadScriptInput {
  GTMScriptRunner *sr = [GTMScriptRunner runner];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *err = nil;

  XCTAssertNil([sr runScript:nil withArgs:nil standardError:&err]);
  XCTAssertNil(err);
  XCTAssertNil([sr runScript:@"/path/that/does/not/exists/foo/bar/baz"
                    withArgs:nil standardError:&err]);
  XCTAssertNotNil(err,
                  @"should have gotten something about the path not existing");
}

- (void)testBadCmdInterpreter {
  GTMScriptRunner *sr =
    [GTMScriptRunner runnerWithInterpreter:@"/path/that/does/not/exists/interpreter"];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *err = nil;

  XCTAssertNil([sr run:nil standardError:&err]);
  XCTAssertNil(err);
  XCTAssertNil([sr run:@"ls /" standardError:&err]);
  XCTAssertNil(err);
}

- (void)testBadScriptInterpreter {
  GTMScriptRunner *sr =
    [GTMScriptRunner runnerWithInterpreter:@"/path/that/does/not/exists/interpreter"];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *err = nil;

  XCTAssertNil([sr runScript:shScript_ withArgs:nil standardError:&err]);
  XCTAssertNil(err);
  XCTAssertNil([sr runScript:@"/path/that/does/not/exists/foo/bar/baz"
                    withArgs:nil standardError:&err]);
  XCTAssertNil(err);
}

- (void)testLargeOutput {
  // These tests cover the issues found in
  //   http://code.google.com/p/google-toolbox-for-mac/issues/detail?id=25

  GTMScriptRunner *sr = [GTMScriptRunner runnerWithPython];
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *output = nil, *err = nil, *cmd = nil;

  #define GENERATOR_FORMAT_STR \
    @"import sys\n" \
    @"block  = '.' * 512\n" \
    @"for x in [%@]:\n" \
    @"  to_where = x[0]\n" \
    @"  how_many = int(x[1:])\n" \
    @"  for x in xrange(0, how_many):\n" \
    @"    if to_where in [ 'o', 'b' ]:\n" \
    @"      sys.stdout.write(block)\n" \
    @"    if to_where in [ 'e', 'b' ]:\n" \
    @"      sys.stderr.write(block)\n"

  // Make sure we get both blocks
  cmd = [NSString stringWithFormat:GENERATOR_FORMAT_STR, @"'b1'"];
  XCTAssertNotNil(cmd);
  output = [sr run:cmd standardError:&err];
  XCTAssertEqual([output length], (NSUInteger)512);
  XCTAssertEqual([err length], (NSUInteger)512);

  // Test a large amount of data on only one connections at a time.
  cmd = [NSString stringWithFormat:GENERATOR_FORMAT_STR, @"'b1', 'o200'"];
  XCTAssertNotNil(cmd);
  output = [sr run:cmd standardError:&err];
  XCTAssertEqual([output length], (NSUInteger)(512 + 512*200));
  XCTAssertEqual([err length], (NSUInteger)512);

  cmd = [NSString stringWithFormat:GENERATOR_FORMAT_STR, @"'b1', 'e200'"];
  XCTAssertNotNil(cmd);
  output = [sr run:cmd standardError:&err];
  XCTAssertEqual([output length], (NSUInteger)512);
  XCTAssertEqual([err length], (NSUInteger)(512 + 512*200));

  // Now send a large amount down both to make sure we spool it all in.
  cmd = [NSString stringWithFormat:GENERATOR_FORMAT_STR, @"'b200'"];
  XCTAssertNotNil(cmd);
  output = [sr run:cmd standardError:&err];
  XCTAssertEqual([output length], (NSUInteger)(512*200));
  XCTAssertEqual([err length], (NSUInteger)(512*200));
}


@end

@implementation GTMScriptRunnerTest (PrivateMethods)

- (void)helperTestBourneShellUsingScriptRunner:(GTMScriptRunner *)sr {
  XCTAssertNotNil(sr, @"Script runner must not be nil");
  NSString *output = nil;

  // Simple command
  output = [sr run:@"ls /etc/passwd"];
  XCTAssertEqualObjects(output, @"/etc/passwd", @"output should equal '/etc/passwd'");

  // Simple command pipe-line
  output = [sr run:@"ls /etc/ | grep cups | tail -1"];
  XCTAssertEqualObjects(output, @"cups", @"output should equal 'cups'");

  // Simple pipe-line with quotes and awk variables
  output = [sr run:@"ps jaxww | awk '{print $2}' | sort -nr | tail -2 | head -1"];
  XCTAssertEqualObjects(output, @"1", @"output should equal '1'");

  // Simple shell loop with variables
  output = [sr run:@"i=0; while [ $i -lt 100 ]; do i=$((i+1)); done; echo $i"];
  XCTAssertEqualObjects(output, @"100", @"output should equal '100'");

  // Simple command with newlines
  output = [sr run:@"i=1\necho $i"];
  XCTAssertEqualObjects(output, @"1", @"output should equal '1'");

  // Simple full shell script
  output = [sr run:@"#!/bin/sh\ni=1\necho $i\n"];
  XCTAssertEqualObjects(output, @"1", @"output should equal '1'");

  NSString *err = nil;

  // Test getting standard error with no stdout
  output = [sr run:@"ls /etc/does-not-exist" standardError:&err];
  XCTAssertNil(output, @"output should be nil due to expected error");
  XCTAssertEqualObjects(err, @"ls: /etc/does-not-exist: No such file or directory", @"");

  // Test getting standard output along with some standard error
  output = [sr run:@"ls /etc/does-not-exist /etc/passwd" standardError:&err];
  XCTAssertEqualObjects(output, @"/etc/passwd", @"");
  XCTAssertEqualObjects(err, @"ls: /etc/does-not-exist: No such file or directory", @"");
}

@end
