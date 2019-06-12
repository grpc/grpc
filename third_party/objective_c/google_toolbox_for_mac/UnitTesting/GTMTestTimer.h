//
//  GTMTestTimer.h
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

#import <Foundation/Foundation.h>
#import "GTMDefines.h"
#import <mach/mach_time.h>

// GTMTestTimer is done in straight inline C to avoid obj-c calling overhead.
// It is for doing test timings at very high precision.
// Test Timers have standard CoreFoundation Retain/Release rules.
// Test Timers are not thread safe. Test Timers do NOT check their arguments
// for NULL. You will crash if you pass a NULL argument in.

typedef struct GTMTestTimer {
  mach_timebase_info_data_t time_base_info_;
  bool running_;
  uint64_t start_;
  uint64_t split_;
  uint64_t elapsed_;
  NSUInteger iterations_;
  NSUInteger retainCount_;
} GTMTestTimer;

// Create a test timer
GTM_INLINE GTMTestTimer *GTMTestTimerCreate(void) {
  GTMTestTimer *t = (GTMTestTimer *)calloc(sizeof(GTMTestTimer), 1);
  if (t) {
    if (mach_timebase_info(&t->time_base_info_) == KERN_SUCCESS) {
      t->retainCount_ = 1;
    } else {
      // COV_NF_START
      free(t);
      t = NULL;
      // COV_NF_END
    }
  }
  return t;
}

// Retain a timer
GTM_INLINE void GTMTestTimerRetain(GTMTestTimer *t) {
  t->retainCount_ += 1;
}

// Release a timer. When release count hits zero, we free it.
GTM_INLINE void GTMTestTimerRelease(GTMTestTimer *t) {
  t->retainCount_ -= 1;
  if (t->retainCount_ == 0) {
    free(t);
  }
}

// Starts a timer timing. Specifically starts a new split. If the timer is
// currently running, it resets the start time of the current split.
GTM_INLINE void GTMTestTimerStart(GTMTestTimer *t) {
  t->start_ = mach_absolute_time();
  t->running_ = true;
}

// Stops a timer and returns split time (time from last start) in nanoseconds.
GTM_INLINE uint64_t GTMTestTimerStop(GTMTestTimer *t) {
  uint64_t now = mach_absolute_time();
  t->running_ = false;
  ++t->iterations_;
  t->split_ = now - t->start_;
  t->elapsed_ += t->split_;
  t->start_ = 0;
  return t->split_;
}

// returns the current timer elapsed time (combined value of all splits, plus
// current split if the timer is running) in nanoseconds.
GTM_INLINE double GTMTestTimerGetNanoseconds(GTMTestTimer *t) {
  uint64_t total = t->elapsed_;
  if (t->running_) {
    total += mach_absolute_time() - t->start_;
  }
  return (double)(total * t->time_base_info_.numer
                  / t->time_base_info_.denom);
}

// Returns the current timer elapsed time (combined value of all splits, plus
// current split if the timer is running) in seconds.
GTM_INLINE double GTMTestTimerGetSeconds(GTMTestTimer *t) {
  return GTMTestTimerGetNanoseconds(t) * 0.000000001;
}

// Returns the current timer elapsed time (combined value of all splits, plus
// current split if the timer is running) in milliseconds.
GTM_INLINE double GTMTestTimerGetMilliseconds(GTMTestTimer *t) {
  return GTMTestTimerGetNanoseconds(t) * 0.000001;
}

// Returns the current timer elapsed time (combined value of all splits, plus
// current split if the timer is running) in microseconds.
GTM_INLINE double GTMTestTimerGetMicroseconds(GTMTestTimer *t) {
  return GTMTestTimerGetNanoseconds(t) * 0.001;
}

// Returns the number of splits (start-stop) cycles recorded.
// GTMTestTimerGetSeconds()/GTMTestTimerGetIterations() gives you an average
// of all your splits.
GTM_INLINE NSUInteger GTMTestTimerGetIterations(GTMTestTimer *t) {
  return t->iterations_;
}

// Returns true if the timer is running.
GTM_INLINE bool GTMTestTimerIsRunning(GTMTestTimer *t) {
  return t->running_;
}
