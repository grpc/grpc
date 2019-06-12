//
//  GTMServiceManagement.h
//
//  Copyright 2010 Google Inc.
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

#include "GTMDefines.h"

#if MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_4

#include <launch.h>
#include <CoreFoundation/CoreFoundation.h>

GTM_EXTERN_C_BEGIN

// Done in C as opposed to Objective-C as lots of services may not want
// to bring in Obj-C libraries.

// For rough documentation on these methods please see
// <ServiceManagement/ServiceManagement.h> from the 10.6 sdk.
// This reimplements some of the ServiceManagement framework on 10.5.
// Caller takes ownership of error if necessary.

Boolean GTMSMJobSubmit(CFDictionaryRef job, CFErrorRef *error) __OSX_AVAILABLE_BUT_DEPRECATED_MSG(__MAC_10_6, __MAC_10_10, __IPHONE_3_0, __IPHONE_8_0, "Replace with XPC.");
Boolean GTMSMJobRemove(CFStringRef jobLabel, CFErrorRef *error) __OSX_AVAILABLE_BUT_DEPRECATED_MSG(__MAC_10_6, __MAC_10_10, __IPHONE_3_0, __IPHONE_8_0, "Replace with XPC.");

// Caller takes ownership of the returned type.
// Note that the dictionary returned will have 0 for machports.
// To get a machport, use bootstrap_look_up, or NSMachBootstrapServer.
CFDictionaryRef GTMSMJobCopyDictionary(CFStringRef jobLabel) __OSX_AVAILABLE_BUT_DEPRECATED_MSG(__MAC_10_6, __MAC_10_10, __IPHONE_3_0, __IPHONE_8_0, "Replace with XPC.");

// This one is conspiciously absent from the ServiceManagement framework.
// Performs a check-in for the running process and returns its dictionary with
// the appropriate sockets and machports filled in.
// Caller  takes ownership of the returned type.
CFDictionaryRef GTMSMCopyJobCheckInDictionary(CFErrorRef *error) __OSX_AVAILABLE_BUT_DEPRECATED_MSG(__MAC_10_6, __MAC_10_10, __IPHONE_3_0, __IPHONE_8_0, "Replace with XPC.");

// The official ServiceManagement version returns an array of job dictionaries.
// This returns a dictionary of job dictionaries where the key is the label
// of the job, and the value is the dictionary for the job of that label.
// Caller takes ownership of the returned type.
CFDictionaryRef GTMSMCopyAllJobDictionaries(void) __OSX_AVAILABLE_BUT_DEPRECATED_MSG(__MAC_10_6, __MAC_10_10, __IPHONE_3_0, __IPHONE_8_0, "Replace with XPC.");


// Convert a CFType (and any of it's subitems) into a launch_data_t.
// Caller takes ownership of the returned type if it isn't added to a launch
// data container type.
launch_data_t GTMLaunchDataCreateFromCFType(CFTypeRef cf_type_ref,
                                            CFErrorRef *error) __OSX_AVAILABLE_BUT_DEPRECATED_MSG(__MAC_10_6, __MAC_10_10, __IPHONE_3_0, __IPHONE_8_0, "Replace with XPC.");

// Convert a launch_data_t (and any of it's subitems) into a CFType.
// If |convert_non_standard_objects| is true, file descriptors and machports
// will be included in the returned dictionary, otherwise they will be ignored.
// Caller is takes ownership of the returned type.
CFTypeRef GTMCFTypeCreateFromLaunchData(launch_data_t ldata,
                                        bool convert_non_standard_objects,
                                        CFErrorRef *error) __OSX_AVAILABLE_BUT_DEPRECATED_MSG(__MAC_10_6, __MAC_10_10, __IPHONE_3_0, __IPHONE_8_0, "Replace with XPC.");

GTM_EXTERN_C_END

#endif //  if MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_4
