//
//  GTMTimeUtils.m
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

#import "GTMTimeUtils.h"

#include <sys/sysctl.h>

#import "GTMDefines.h"

NSTimeInterval GTMTimeValToNSTimeInterval(struct timeval time) {
  return time.tv_sec + (time.tv_usec / (double)USEC_PER_SEC);
}

struct timeval GTMBootTimeRelativeTo1970(void) {
  struct timeval bootTime = { 0, 0 };
  int mib[2] = { CTL_KERN, KERN_BOOTTIME };
  size_t bootTimeSize = sizeof(bootTime);
  if (sysctl(mib, 2, &bootTime, &bootTimeSize, NULL, 0) != 0) {
    _GTMDevAssert(errno == 0, @"sysctl error - %d", errno);
    struct timeval invalid = { 0, 0 };
    return invalid;
  }
  return bootTime;
}

struct timeval GTMAppLaunchTimeRelativeTo1970(void) {
  id_t pid = getpid();
  int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
  const size_t mibSize = sizeof(mib) / sizeof(mib[0]);
  size_t infoSize = 0;

  // Get initial size of KERN_PROC data structure.
  if (sysctl(mib, mibSize, NULL, &infoSize, NULL, 0) != 0) {
    _GTMDevAssert(errno == 0, @"sysctl error - %d", errno);
    struct timeval invalid = { 0, 0 };
    return invalid;
  }
  struct kinfo_proc info;
  if (sysctl(mib, mibSize, &info, &infoSize, NULL, 0) != 0) {
    _GTMDevAssert(errno == 0, @"sysctl error - %d", errno);
    struct timeval invalid = { 0, 0 };
    return invalid;
  }
  return info.kp_proc.p_starttime;
}

NSDate *GTMAppLaunchDate() {
  NSTimeInterval ti =
      GTMTimeValToNSTimeInterval(GTMAppLaunchTimeRelativeTo1970());
  return [NSDate dateWithTimeIntervalSince1970:ti];
}

NSDate *GTMBootDate() {
  NSTimeInterval ti =
      GTMTimeValToNSTimeInterval(GTMBootTimeRelativeTo1970());
  return [NSDate dateWithTimeIntervalSince1970:ti];
}
