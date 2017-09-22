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

#include "src/core/ext/census/window_stats.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

/* typedefs make typing long names easier. Use cws (for census_window_stats) */
typedef census_window_stats_stat_info cws_stat_info;
typedef struct census_window_stats_sum cws_sum;

/* Each interval is composed of a number of buckets, which hold a count of
   entries and a single statistic */
typedef struct census_window_stats_bucket {
  int64_t count;
  void *statistic;
} cws_bucket;

/* Each interval has a set of buckets, and the variables needed to keep
   track of their current state */
typedef struct census_window_stats_interval_stats {
  /* The buckets. There will be 'granularity' + 1 of these. */
  cws_bucket *buckets;
  /* Index of the bucket containing the smallest time interval. */
  int bottom_bucket;
  /* The smallest time storable in the current window. */
  int64_t bottom;
  /* The largest time storable in the current window + 1ns */
  int64_t top;
  /* The width of each bucket in ns. */
  int64_t width;
} cws_interval_stats;

typedef struct census_window_stats {
  /* Number of intervals. */
  int nintervals;
  /* Number of buckets in each interval. 'granularity' + 1. */
  int nbuckets;
  /* Record of stat_info. */
  cws_stat_info stat_info;
  /* Stats for each interval. */
  cws_interval_stats *interval_stats;
  /* The time the newset stat was recorded. */
  int64_t newest_time;
} window_stats;

/* Calculate an actual bucket index from a logical index 'IDX'. Other
   parameters supply information on the interval struct and overall stats. */
#define BUCKET_IDX(IS, IDX, WSTATS) \
  ((IS->bottom_bucket + (IDX)) % WSTATS->nbuckets)

/* The maximum seconds value we can have in a valid timespec. More than this
   will result in overflow in timespec_to_ns(). This works out to ~292 years.
   TODO: consider using doubles instead of int64. */
static int64_t max_seconds = (GPR_INT64_MAX - GPR_NS_PER_SEC) / GPR_NS_PER_SEC;

static int64_t timespec_to_ns(const gpr_timespec ts) {
  if (ts.tv_sec > max_seconds) {
    return GPR_INT64_MAX - 1;
  }
  return ts.tv_sec * GPR_NS_PER_SEC + ts.tv_nsec;
}

static void cws_initialize_statistic(void *statistic,
                                     const cws_stat_info *stat_info) {
  if (stat_info->stat_initialize == NULL) {
    memset(statistic, 0, stat_info->stat_size);
  } else {
    stat_info->stat_initialize(statistic);
  }
}

/* Create and initialize a statistic */
static void *cws_create_statistic(const cws_stat_info *stat_info) {
  void *stat = gpr_malloc(stat_info->stat_size);
  cws_initialize_statistic(stat, stat_info);
  return stat;
}

window_stats *census_window_stats_create(int nintervals,
                                         const gpr_timespec intervals[],
                                         int granularity,
                                         const cws_stat_info *stat_info) {
  window_stats *ret;
  int i;
  /* validate inputs */
  GPR_ASSERT(nintervals > 0 && granularity > 2 && intervals != NULL &&
             stat_info != NULL);
  for (i = 0; i < nintervals; i++) {
    int64_t ns = timespec_to_ns(intervals[i]);
    GPR_ASSERT(intervals[i].tv_sec >= 0 && intervals[i].tv_nsec >= 0 &&
               intervals[i].tv_nsec < GPR_NS_PER_SEC && ns >= 100 &&
               granularity * 10 <= ns);
  }
  /* Allocate and initialize relevant data structures */
  ret = (window_stats *)gpr_malloc(sizeof(window_stats));
  ret->nintervals = nintervals;
  ret->nbuckets = granularity + 1;
  ret->stat_info = *stat_info;
  ret->interval_stats =
      (cws_interval_stats *)gpr_malloc(nintervals * sizeof(cws_interval_stats));
  for (i = 0; i < nintervals; i++) {
    int64_t size_ns = timespec_to_ns(intervals[i]);
    cws_interval_stats *is = ret->interval_stats + i;
    cws_bucket *buckets = is->buckets =
        (cws_bucket *)gpr_malloc(ret->nbuckets * sizeof(cws_bucket));
    int b;
    for (b = 0; b < ret->nbuckets; b++) {
      buckets[b].statistic = cws_create_statistic(stat_info);
      buckets[b].count = 0;
    }
    is->bottom_bucket = 0;
    is->bottom = 0;
    is->width = size_ns / granularity;
    /* Check for possible overflow issues, and maximize interval size if the
       user requested something large enough. */
    if ((GPR_INT64_MAX - is->width) > size_ns) {
      is->top = size_ns + is->width;
    } else {
      is->top = GPR_INT64_MAX;
      is->width = GPR_INT64_MAX / (granularity + 1);
    }
    /* If size doesn't divide evenly, we can have a width slightly too small;
       better to have it slightly large. */
    if ((size_ns - (granularity + 1) * is->width) > 0) {
      is->width += 1;
    }
  }
  ret->newest_time = 0;
  return ret;
}

/* When we try adding a measurement above the current interval range, we
   need to "shift" the buckets sufficiently to cover the new range. */
static void cws_shift_buckets(const window_stats *wstats,
                              cws_interval_stats *is, int64_t when_ns) {
  int i;
  /* number of bucket time widths to "shift" */
  int shift;
  /* number of buckets to clear */
  int nclear;
  GPR_ASSERT(when_ns >= is->top);
  /* number of bucket time widths to "shift" */
  shift = ((when_ns - is->top) / is->width) + 1;
  /* number of buckets to clear - limited by actual number of buckets */
  nclear = GPR_MIN(shift, wstats->nbuckets);
  for (i = 0; i < nclear; i++) {
    int b = BUCKET_IDX(is, i, wstats);
    is->buckets[b].count = 0;
    cws_initialize_statistic(is->buckets[b].statistic, &wstats->stat_info);
  }
  /* adjust top/bottom times and current bottom bucket */
  is->bottom_bucket = BUCKET_IDX(is, shift, wstats);
  is->top += shift * is->width;
  is->bottom += shift * is->width;
}

void census_window_stats_add(window_stats *wstats, const gpr_timespec when,
                             const void *stat_value) {
  int i;
  int64_t when_ns = timespec_to_ns(when);
  GPR_ASSERT(wstats->interval_stats != NULL);
  for (i = 0; i < wstats->nintervals; i++) {
    cws_interval_stats *is = wstats->interval_stats + i;
    cws_bucket *bucket;
    if (when_ns < is->bottom) { /* Below smallest time in interval: drop */
      continue;
    }
    if (when_ns >= is->top) { /* above limit: shift buckets */
      cws_shift_buckets(wstats, is, when_ns);
    }
    /* Add the stat. */
    GPR_ASSERT(is->bottom <= when_ns && when_ns < is->top);
    bucket = is->buckets +
             BUCKET_IDX(is, (when_ns - is->bottom) / is->width, wstats);
    bucket->count++;
    wstats->stat_info.stat_add(bucket->statistic, stat_value);
  }
  if (when_ns > wstats->newest_time) {
    wstats->newest_time = when_ns;
  }
}

/* Add a specific bucket contents to an accumulating total. */
static void cws_add_bucket_to_sum(cws_sum *sum, const cws_bucket *bucket,
                                  const cws_stat_info *stat_info) {
  sum->count += bucket->count;
  stat_info->stat_add(sum->statistic, bucket->statistic);
}

/* Add a proportion to an accumulating sum. */
static void cws_add_proportion_to_sum(double p, cws_sum *sum,
                                      const cws_bucket *bucket,
                                      const cws_stat_info *stat_info) {
  sum->count += p * bucket->count;
  stat_info->stat_add_proportion(p, sum->statistic, bucket->statistic);
}

void census_window_stats_get_sums(const window_stats *wstats,
                                  const gpr_timespec when, cws_sum sums[]) {
  int i;
  int64_t when_ns = timespec_to_ns(when);
  GPR_ASSERT(wstats->interval_stats != NULL);
  for (i = 0; i < wstats->nintervals; i++) {
    int when_bucket;
    int new_bucket;
    double last_proportion = 1.0;
    double bottom_proportion;
    cws_interval_stats *is = wstats->interval_stats + i;
    cws_sum *sum = sums + i;
    sum->count = 0;
    cws_initialize_statistic(sum->statistic, &wstats->stat_info);
    if (when_ns < is->bottom) {
      continue;
    }
    if (when_ns >= is->top) {
      cws_shift_buckets(wstats, is, when_ns);
    }
    /* Calculating the appropriate amount of which buckets to use can get
       complicated. Essentially there are two cases:
       1) if the "top" bucket (new_bucket, where the newest additions to the
       stats recorded are entered) corresponds to 'when', then we need
       to take a proportion of it - (if when < newest_time) or the full
       thing. We also (possibly) need to take a corresponding
       proportion of the bottom bucket.
       2) Other cases, we just take a straight proportion.
     */
    when_bucket = (when_ns - is->bottom) / is->width;
    new_bucket = (wstats->newest_time - is->bottom) / is->width;
    if (new_bucket == when_bucket) {
      int64_t bottom_bucket_time = is->bottom + when_bucket * is->width;
      if (when_ns < wstats->newest_time) {
        last_proportion = (double)(when_ns - bottom_bucket_time) /
                          (double)(wstats->newest_time - bottom_bucket_time);
        bottom_proportion =
            (double)(is->width - (when_ns - bottom_bucket_time)) / is->width;
      } else {
        bottom_proportion =
            (double)(is->width - (wstats->newest_time - bottom_bucket_time)) /
            is->width;
      }
    } else {
      last_proportion =
          (double)(when_ns + 1 - is->bottom - when_bucket * is->width) /
          is->width;
      bottom_proportion = 1.0 - last_proportion;
    }
    cws_add_proportion_to_sum(last_proportion, sum,
                              is->buckets + BUCKET_IDX(is, when_bucket, wstats),
                              &wstats->stat_info);
    if (when_bucket != 0) { /* last bucket isn't also bottom bucket */
      int b;
      /* Add all of "bottom" bucket if we are looking at a subset of the
         full interval, or a proportion if we are adding full interval. */
      cws_add_proportion_to_sum(
          (when_bucket == wstats->nbuckets - 1 ? bottom_proportion : 1.0), sum,
          is->buckets + is->bottom_bucket, &wstats->stat_info);
      /* Add all the remaining buckets (everything but top and bottom). */
      for (b = 1; b < when_bucket; b++) {
        cws_add_bucket_to_sum(sum, is->buckets + BUCKET_IDX(is, b, wstats),
                              &wstats->stat_info);
      }
    }
  }
}

void census_window_stats_destroy(window_stats *wstats) {
  int i;
  GPR_ASSERT(wstats->interval_stats != NULL);
  for (i = 0; i < wstats->nintervals; i++) {
    int b;
    for (b = 0; b < wstats->nbuckets; b++) {
      gpr_free(wstats->interval_stats[i].buckets[b].statistic);
    }
    gpr_free(wstats->interval_stats[i].buckets);
  }
  gpr_free(wstats->interval_stats);
  /* Ensure any use-after free triggers assert. */
  wstats->interval_stats = NULL;
  gpr_free(wstats);
}
