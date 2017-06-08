/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_CENSUS_WINDOW_STATS_H
#define GRPC_CORE_EXT_CENSUS_WINDOW_STATS_H

#include <grpc/support/time.h>

/* Keep rolling sums of a user-defined statistic (containing a number of
   measurements) over a a number of time intervals ("windows"). For example,
   you can use a window_stats object to answer questions such as
   "Approximately how many RPCs/s did I receive over the past minute, and
   approximately how many bytes did I send out over that period?".

   The type of data to record, and the time intervals to keep are specified
   when creating the object via a call to census_window_stats_create().

   A window's interval is divided into one or more "buckets"; the interval
   must be divisible by the number of buckets. Internally, these buckets
   control the granularity of window_stats' measurements. Increasing the
   number of buckets lets the object respond more quickly to changes in the
   overall rate of data added into the object, at the cost of additional
   memory usage.

   Here's some code which keeps one minute/hour measurements for two values
   (latency in seconds and bytes transferred), with each interval divided into
   4 buckets.

    typedef struct my_stat {
      double latency;
      int bytes;
    } my_stat;

    void add_my_stat(void* base, const void* addme) {
      my_stat* b = (my_stat*)base;
      const my_stat* a = (const my_stat*)addme;
      b->latency += a->latency;
      b->bytes += a->bytes;
    }

    void add_proportion_my_stat(double p, void* base, const void* addme) {
      (my_stat*)result->latency += p * (const my_stat*)base->latency;
      (my_stat*)result->bytes += p * (const my_stat*)base->bytes;
    }

    #define kNumIntervals 2
    #define kMinInterval 0
    #define kHourInterval 1
    #define kNumBuckets 4

    const struct census_window_stats_stat_info kMyStatInfo
        = { sizeof(my_stat), NULL, add_my_stat, add_proportion_my_stat };
    gpr_timespec intervals[kNumIntervals] = {{60, 0}, {3600, 0}};
    my_stat stat;
    my_stat sums[kNumIntervals];
    census_window_stats_sums result[kNumIntervals];
    struct census_window_stats* stats
        = census_window_stats_create(kNumIntervals, intervals, kNumBuckets,
                                     &kMyStatInfo);
    // Record a new event, taking 15.3ms, transferring 1784 bytes.
    stat.latency = 0.153;
    stat.bytes = 1784;
    census_window_stats_add(stats, gpr_now(GPR_CLOCK_REALTIME), &stat);
    // Get sums and print them out
    result[kMinInterval].statistic = &sums[kMinInterval];
    result[kHourInterval].statistic = &sums[kHourInterval];
    census_window_stats_get_sums(stats, gpr_now(GPR_CLOCK_REALTIME), result);
    printf("%d events/min, average time %gs, average bytes %g\n",
           result[kMinInterval].count,
           (my_stat*)result[kMinInterval].statistic->latency /
             result[kMinInterval].count,
           (my_stat*)result[kMinInterval].statistic->bytes /
             result[kMinInterval].count
          );
    printf("%d events/hr, average time %gs, average bytes %g\n",
           result[kHourInterval].count,
           (my_stat*)result[kHourInterval].statistic->latency /
             result[kHourInterval].count,
           (my_stat*)result[kHourInterval].statistic->bytes /
             result[kHourInterval].count
          );
*/

/* Opaque structure for representing window_stats object */
struct census_window_stats;

/* Information provided by API user on the information they want to record */
typedef struct census_window_stats_stat_info {
  /* Number of bytes in user-defined object. */
  size_t stat_size;
  /* Function to initialize a user-defined statistics object. If this is set
   * to NULL, then the object will be zero-initialized. */
  void (*stat_initialize)(void *stat);
  /* Function to add one user-defined statistics object ('addme') to 'base' */
  void (*stat_add)(void *base, const void *addme);
  /* As for previous function, but only add a proportion 'p'. This API will
     currently only use 'p' values in the range [0,1], but other values are
     possible in the future, and should be supported. */
  void (*stat_add_proportion)(double p, void *base, const void *addme);
} census_window_stats_stat_info;

/* Create a new window_stats object. 'nintervals' is the number of
   'intervals', and must be >=1. 'granularity' is the number of buckets, with
   a larger number using more memory, but providing greater accuracy of
   results. 'granularity should be > 2. We also require that each interval be
   at least 10 * 'granularity' nanoseconds in size. 'stat_info' contains
   information about the statistic to be gathered. Intervals greater than ~192
   years will be treated as essentially infinite in size. This function will
   GPR_ASSERT() if the object cannot be created or any of the parameters have
   invalid values. This function is thread-safe. */
struct census_window_stats *census_window_stats_create(
    int nintervals, const gpr_timespec intervals[], int granularity,
    const census_window_stats_stat_info *stat_info);

/* Add a new measurement (in 'stat_value'), as of a given time ('when').
   This function is thread-compatible. */
void census_window_stats_add(struct census_window_stats *wstats,
                             const gpr_timespec when, const void *stat_value);

/* Structure used to record a single intervals sum for a given statistic */
typedef struct census_window_stats_sum {
  /* Total count of samples. Note that because some internal interpolation
     is performed, the count of samples returned for each interval may not be an
     integral value. */
  double count;
  /* Sum for statistic */
  void *statistic;
} census_window_stats_sums;

/* Retrieve a set of all values stored in a window_stats object 'wstats'. The
   number of 'sums' MUST be the same as the number 'nintervals' used in
   census_window_stats_create(). This function is thread-compatible. */
void census_window_stats_get_sums(const struct census_window_stats *wstats,
                                  const gpr_timespec when,
                                  struct census_window_stats_sum sums[]);

/* Destroy a window_stats object. Once this function has been called, the
   object will no longer be usable from any of the above functions (and
   calling them will most likely result in a NULL-pointer dereference or
   assertion failure). This function is thread-compatible. */
void census_window_stats_destroy(struct census_window_stats *wstats);

#endif /* GRPC_CORE_EXT_CENSUS_WINDOW_STATS_H */
