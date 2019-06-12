//
//  GTMStackTrace.h
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
#import "GTMDefines.h"

#ifdef __cplusplus
extern "C" {
#endif

struct GTMAddressDescriptor {
  const void *address;  // address
  const char *symbol;  // nearest symbol to address
  const char *class_name;  // if it is an obj-c method, the method's class
  BOOL is_class_method;  // if it is an obj-c method, type of method
  const char *filename;  // file that the method came from.
};

// Returns a string containing a nicely formatted stack trace.
//
// This function gets the stack trace for the current thread. It will
// be from the caller of GTMStackTrace upwards to the top the calling stack.
// Typically this function will be used along with some logging,
// as in the following:
//
//   MyAppLogger(@"Should never get here:\n%@", GTMStackTrace());
//
// Here is a sample stack trace returned from this function:
//
// #0  0x00002d92 D ()  [/Users/me/./StackLog]
// #1  0x00002e45 C ()  [/Users/me/./StackLog]
// #2  0x00002e53 B ()  [/Users/me/./StackLog]
// #3  0x00002e61 A ()  [/Users/me/./StackLog]
// #4  0x00002e6f main ()  [/Users/me/./StackLog]
// #5  0x00002692 tart ()  [/Users/me/./StackLog]
// #6  0x000025b9 tart ()  [/Users/me/./StackLog]
//

NSString *GTMStackTrace(void);

// Returns a string containing a nicely formatted stack trace from the
// exception.  Only available on 10.5 or later, uses
// -[NSException callStackReturnAddresses].
//
NSString *GTMStackTraceFromException(NSException *e);

// Returns an array of GTMAddressDescriptors from the current thread's stack.
// *** You should probably use GTMStackTrace() instead of this function ***
// However, if you actually want all the PCs with symbols, this is the way
// to get them. There is no memory allocations done, so no clean up is required
// except for the caller to free outDescs if they allocated it themselves.
// This will include PCs of GTMStaceTrace and its inner utility functions that
// you may want to strip out.
//
// Args:
//   outDescs - an array of "struct GTMAddressDescriptor" pointers corresponding
//              to the program counters found on the current thread's stack.
//   count - the number of entries in the outDescs array
//
// Returns:
//   The number of program counters actually added to outPcs.
//
NSUInteger GTMGetStackAddressDescriptors(struct GTMAddressDescriptor outDescs[],
                                         NSUInteger count);

#ifdef __cplusplus
}
#endif
