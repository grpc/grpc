//
//  GTMTimeUtils.h
//
//  Copyright 2018 Google LLC
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

// Return the date that the app was launched.
NSDate *GTMAppLaunchDate(void);

// Return the date that the device was started. Note on the simulator that this
// returns the date that the computer was started, not the simulator.
NSDate *GTMBootDate(void);

// Convert a timeval to NSTimeInterval.
NSTimeInterval GTMTimeValToNSTimeInterval(struct timeval time);

// Timeval versions of the functions above if timevals are a more useful
// structure to work with.
struct timeval GTMBootTimeRelativeTo1970(void);
struct timeval GTMAppLaunchTimeRelativeTo1970(void);

