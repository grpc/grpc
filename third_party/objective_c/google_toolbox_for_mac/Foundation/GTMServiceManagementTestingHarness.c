//
//  GTMServiceManagementTestingHarness.c
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

#include "GTMServiceManagement.h"

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMServiceManagement
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

int main(int argc, const char** argv) {
#if MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_4
  CFErrorRef error = NULL;
  CFDictionaryRef dict = GTMSMCopyJobCheckInDictionary(&error);
  if (!dict) {
    CFShow(error);
  } else {
    CFRelease(dict);
  }
#endif //  if MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_4
  return 0;
}

#pragma clang diagnostic pop
