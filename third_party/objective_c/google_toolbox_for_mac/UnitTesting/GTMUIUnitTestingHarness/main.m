//
//  main.m
//  GTMUnitTestingTest
//
//  Copyright 2006-2008 Google Inc.
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


#import <Cocoa/Cocoa.h>
#import "GTMFoundationUnitTestingUtilities.h"

int main(int argc, char *argv[]) {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  // Give ourselves a max of 10 minutes for the tests.  Sometimes (in automated
  // builds) the unittesting bundle fails to load which causes the app to keep
  // running forever.  This will force it to exit after a certain amount of time
  // instead of hanging running forever.
  [GTMFoundationUnitTestingUtilities installTestingTimeout:10*60.0];

  int result = NSApplicationMain(argc,  (const char **) argv);
  [pool drain];
  return result;
}
