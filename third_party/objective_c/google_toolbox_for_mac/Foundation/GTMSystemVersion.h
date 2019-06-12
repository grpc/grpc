//
//  GTMSystemVersion.h
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

// A class for getting information about what system we are running on
NS_DEPRECATED(10_0, 10_10, 1_0, 8_0, "Use NSProcessInfo.operatingSystemVersion.")
@interface GTMSystemVersion : NSObject

// Returns the current system version major.minor.bugFix
+ (void)getMajor:(SInt32*)major minor:(SInt32*)minor bugFix:(SInt32*)bugFix;

// Returns the build number of the OS. Useful when looking for bug fixes
// in new OSes which all have a set system version.
// eg 10.5.5's build number is 9F33. Easy way to check the build number
// is to choose "About this Mac" from the Apple menu and click on the version
// number.
+ (NSString*)build;

+ (BOOL)isBuildLessThan:(NSString*)build;
+ (BOOL)isBuildLessThanOrEqualTo:(NSString*)build;
+ (BOOL)isBuildGreaterThan:(NSString*)build;
+ (BOOL)isBuildGreaterThanOrEqualTo:(NSString*)build;
+ (BOOL)isBuildEqualTo:(NSString *)build;

#if GTM_MACOS_SDK
// Returns YES if running on 10.3, NO otherwise.
+ (BOOL)isPanther;

// Returns YES if running on 10.4, NO otherwise.
+ (BOOL)isTiger;

// Returns YES if running on 10.5, NO otherwise.
+ (BOOL)isLeopard;

// Returns YES if running on 10.6, NO otherwise.
+ (BOOL)isSnowLeopard;

// Returns a YES/NO if the system is 10.3 or better
+ (BOOL)isPantherOrGreater;

// Returns a YES/NO if the system is 10.4 or better
+ (BOOL)isTigerOrGreater;

// Returns a YES/NO if the system is 10.5 or better
+ (BOOL)isLeopardOrGreater;

// Returns a YES/NO if the system is 10.6 or better
+ (BOOL)isSnowLeopardOrGreater;
#endif  // GTM_MACOS_SDK

// Returns one of the achitecture strings below. Note that this is the
// architecture that we are currently running as, not the hardware architecture.
+ (NSString *)runtimeArchitecture;
@end

// Architecture Strings
// TODO: Should probably break iPhone up into iPhone_ARM and iPhone_Simulator
//       but haven't found a need yet.
GTM_EXTERN NSString *const kGTMArch_iPhone;
GTM_EXTERN NSString *const kGTMArch_ppc;
GTM_EXTERN NSString *const kGTMArch_ppc64;
GTM_EXTERN NSString *const kGTMArch_x86_64;
GTM_EXTERN NSString *const kGTMArch_i386;
