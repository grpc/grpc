//
//  GTMScriptRunner.h
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

#import <Foundation/Foundation.h>

/// Encapsulates the interaction with an interpreter for running scripts.
// This class manages the interaction with some command-line interpreter (e.g.,
// a shell, perl, python) and allows you to run expressions through the
// interpreter, and even full scripts that reside in files on disk.  By default,
// the "/bin/sh" interpreter is used, but others may be explicitly specified.
// This can be a convenient way to run quick shell commands from Cocoa, or even
// interact with other shell tools such as "bc", or even "gdb".
//
// It's important to note that by default commands and scripts will have their
// environments erased before execution. You can control the environment they
// get with the -setEnvironment: method.
//
// The best way to show what this class does is to show some examples.
//
// Examples:
//
// GTMScriptRunner *sr = [GTMScriptRunner runner];
// NSString *output = [sr run:@"ls -l /dev/null"];
// /* output == "crw-rw-rw-   1 root  wheel    3,   2 Mar 22 10:35 /dev/null" */
//
// GTMScriptRunner *sr = [GTMScriptRunner runner];
// NSString *output = [sr runScript:@"/path/to/my/script.sh"];
// /* output == the standard output from the script*/
//
// GTMScriptRunner *sr = [GTMScriptRunner runnerWithPerl];
// NSString *output = [sr run:@"print 'A'x4"];
// /* output == "AAAA" */
//
// See the unit test file for more examples.
//
@interface GTMScriptRunner : NSObject {
 @private
  NSString *interpreter_;
  NSArray  *interpreterArgs_;
  NSDictionary *environment_;
  BOOL trimsWhitespace_;
}

// Convenience methods for returning autoreleased GTMScriptRunner instances, that
// are associated with the specified interpreter.  The default interpreter
// (returned from +runner is "/bin/sh").
+ (GTMScriptRunner *)runner;
+ (GTMScriptRunner *)runnerWithBash;
+ (GTMScriptRunner *)runnerWithPerl;
+ (GTMScriptRunner *)runnerWithPython;

// Returns an autoreleased GTMScriptRunner instance associated with the specified
// interpreter, and the given args.  The specified args are the arguments that
// should be applied to the interpreter itself, not scripts run through the
// interpreter.  For example, to start an interpreter using "perl -w", you could
// do:
//   [GTMScriptRunner runnerWithInterpreter:@"/usr/bin/perl"
//                                withArgs:[NSArray arrayWithObject:@"-w"]];
//
+ (GTMScriptRunner *)runnerWithInterpreter:(NSString *)interp;
+ (GTMScriptRunner *)runnerWithInterpreter:(NSString *)interp
                                  withArgs:(NSArray *)args;

// Returns a GTMScriptRunner associated with |interp|
- (id)initWithInterpreter:(NSString *)interp;

// Returns a GTMScriptRunner associated with |interp| and |args| applied to the
// specified interpreter.  This method is the designated initializer.
- (id)initWithInterpreter:(NSString *)interp withArgs:(NSArray *)args;

// Runs the specified command string by sending it through the interpreter's
// standard input.  The standard output is returned.  The standard error is
// discarded.
- (NSString *)run:(NSString *)cmds;
// Same as the previous method, except the standard error is returned in |err|
// if specified.
- (NSString *)run:(NSString *)cmds standardError:(NSString **)err;

// Runs the file at |path| using the interpreter.
- (NSString *)runScript:(NSString *)path;
// Runs the file at |path|, passing it |args| as arguments.
- (NSString *)runScript:(NSString *)path withArgs:(NSArray *)args;
// Same as above, except the standard error is returned in |err| if specified.
- (NSString *)runScript:(NSString *)path withArgs:(NSArray *)args
          standardError:(NSString **)err;

// Returns the environment dictionary to use for the inferior process that will
// run the interpreter. A return value of nil means that the interpreter's
// environment should be erased.
- (NSDictionary *)environment;

// Sets the environment dictionary to use for the interpreter process. See
// NSTask's -setEnvironment: documentation for details about the dictionary.
// Basically, it's just a dict of key/value pairs corresponding to environment
// keys and values. Setting a value of nil means that the environment should be
// erased before running the interpreter.
//
// *** The default is nil. ***
//
// By default, all interpreters will run with a clean environment. If you want
// the interpreter process to inherit your current environment you'll need to
// do the following:
//
// GTMScriptRunner *sr = [GTMScriptRunner runner];
// [sr setEnvironment:[[NSProcessInfo processInfo] environment]];
//
// SECURITY NOTE: That said, in general you should NOT do this because an
// attacker can modify the environment that would then get sent to your scripts.
// And if your binary is suid, then  you ABSOLUTELY should not do this.
//
- (void)setEnvironment:(NSDictionary *)newEnv;

// Sets (and returns) whether or not whitespace is automatically trimmed from
// the ends of the returned strings.  The default is YES, so trailing newlines
// will be removed.
- (BOOL)trimsWhitespace;
- (void)setTrimsWhitespace:(BOOL)trim;

@end
