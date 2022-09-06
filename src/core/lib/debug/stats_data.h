/*
 * Copyright 2017 gRPC authors.
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
 */

/*
 * Automatically generated by tools/codegen/core/gen_stats_data.py
 */

#ifndef GRPC_CORE_LIB_DEBUG_STATS_DATA_H
#define GRPC_CORE_LIB_DEBUG_STATS_DATA_H

#include <grpc/support/port_platform.h>

// IWYU pragma: private, include "src/core/lib/debug/stats.h"

typedef enum {
  GRPC_STATS_COUNTER_CLIENT_CALLS_CREATED,
  GRPC_STATS_COUNTER_SERVER_CALLS_CREATED,
  GRPC_STATS_COUNTER_CLIENT_CHANNELS_CREATED,
  GRPC_STATS_COUNTER_CLIENT_SUBCHANNELS_CREATED,
  GRPC_STATS_COUNTER_SERVER_CHANNELS_CREATED,
  GRPC_STATS_COUNTER_SYSCALL_WRITE,
  GRPC_STATS_COUNTER_SYSCALL_READ,
  GRPC_STATS_COUNTER_TCP_READ_ALLOC_8K,
  GRPC_STATS_COUNTER_TCP_READ_ALLOC_64K,
  GRPC_STATS_COUNTER_HTTP2_SETTINGS_WRITES,
  GRPC_STATS_COUNTER_HTTP2_PINGS_SENT,
  GRPC_STATS_COUNTER_HTTP2_WRITES_BEGUN,
  GRPC_STATS_COUNTER_HTTP2_TRANSPORT_STALLS,
  GRPC_STATS_COUNTER_HTTP2_STREAM_STALLS,
  GRPC_STATS_COUNTER_CQ_PLUCK_CREATES,
  GRPC_STATS_COUNTER_CQ_NEXT_CREATES,
  GRPC_STATS_COUNTER_CQ_CALLBACK_CREATES,
  GRPC_STATS_COUNTER_COUNT
} grpc_stats_counters;
extern const char* grpc_stats_counter_name[GRPC_STATS_COUNTER_COUNT];
extern const char* grpc_stats_counter_doc[GRPC_STATS_COUNTER_COUNT];
typedef enum {
  GRPC_STATS_HISTOGRAM_CALL_INITIAL_SIZE,
  GRPC_STATS_HISTOGRAM_TCP_WRITE_SIZE,
  GRPC_STATS_HISTOGRAM_TCP_WRITE_IOV_SIZE,
  GRPC_STATS_HISTOGRAM_TCP_READ_SIZE,
  GRPC_STATS_HISTOGRAM_TCP_READ_OFFER,
  GRPC_STATS_HISTOGRAM_TCP_READ_OFFER_IOV_SIZE,
  GRPC_STATS_HISTOGRAM_HTTP2_SEND_MESSAGE_SIZE,
  GRPC_STATS_HISTOGRAM_COUNT
} grpc_stats_histograms;
extern const char* grpc_stats_histogram_name[GRPC_STATS_HISTOGRAM_COUNT];
extern const char* grpc_stats_histogram_doc[GRPC_STATS_HISTOGRAM_COUNT];
typedef enum {
  GRPC_STATS_HISTOGRAM_CALL_INITIAL_SIZE_FIRST_SLOT = 0,
  GRPC_STATS_HISTOGRAM_CALL_INITIAL_SIZE_BUCKETS = 24,
  GRPC_STATS_HISTOGRAM_TCP_WRITE_SIZE_FIRST_SLOT = 24,
  GRPC_STATS_HISTOGRAM_TCP_WRITE_SIZE_BUCKETS = 20,
  GRPC_STATS_HISTOGRAM_TCP_WRITE_IOV_SIZE_FIRST_SLOT = 44,
  GRPC_STATS_HISTOGRAM_TCP_WRITE_IOV_SIZE_BUCKETS = 10,
  GRPC_STATS_HISTOGRAM_TCP_READ_SIZE_FIRST_SLOT = 54,
  GRPC_STATS_HISTOGRAM_TCP_READ_SIZE_BUCKETS = 20,
  GRPC_STATS_HISTOGRAM_TCP_READ_OFFER_FIRST_SLOT = 74,
  GRPC_STATS_HISTOGRAM_TCP_READ_OFFER_BUCKETS = 20,
  GRPC_STATS_HISTOGRAM_TCP_READ_OFFER_IOV_SIZE_FIRST_SLOT = 94,
  GRPC_STATS_HISTOGRAM_TCP_READ_OFFER_IOV_SIZE_BUCKETS = 10,
  GRPC_STATS_HISTOGRAM_HTTP2_SEND_MESSAGE_SIZE_FIRST_SLOT = 104,
  GRPC_STATS_HISTOGRAM_HTTP2_SEND_MESSAGE_SIZE_BUCKETS = 20,
  GRPC_STATS_HISTOGRAM_BUCKETS = 124
} grpc_stats_histogram_constants;
#define GRPC_STATS_INC_CLIENT_CALLS_CREATED() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_CLIENT_CALLS_CREATED)
#define GRPC_STATS_INC_SERVER_CALLS_CREATED() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_SERVER_CALLS_CREATED)
#define GRPC_STATS_INC_CLIENT_CHANNELS_CREATED() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_CLIENT_CHANNELS_CREATED)
#define GRPC_STATS_INC_CLIENT_SUBCHANNELS_CREATED() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_CLIENT_SUBCHANNELS_CREATED)
#define GRPC_STATS_INC_SERVER_CHANNELS_CREATED() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_SERVER_CHANNELS_CREATED)
#define GRPC_STATS_INC_SYSCALL_WRITE() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_SYSCALL_WRITE)
#define GRPC_STATS_INC_SYSCALL_READ() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_SYSCALL_READ)
#define GRPC_STATS_INC_TCP_READ_ALLOC_8K() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_TCP_READ_ALLOC_8K)
#define GRPC_STATS_INC_TCP_READ_ALLOC_64K() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_TCP_READ_ALLOC_64K)
#define GRPC_STATS_INC_HTTP2_SETTINGS_WRITES() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_HTTP2_SETTINGS_WRITES)
#define GRPC_STATS_INC_HTTP2_PINGS_SENT() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_HTTP2_PINGS_SENT)
#define GRPC_STATS_INC_HTTP2_WRITES_BEGUN() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_HTTP2_WRITES_BEGUN)
#define GRPC_STATS_INC_HTTP2_TRANSPORT_STALLS() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_HTTP2_TRANSPORT_STALLS)
#define GRPC_STATS_INC_HTTP2_STREAM_STALLS() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_HTTP2_STREAM_STALLS)
#define GRPC_STATS_INC_CQ_PLUCK_CREATES() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_CQ_PLUCK_CREATES)
#define GRPC_STATS_INC_CQ_NEXT_CREATES() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_CQ_NEXT_CREATES)
#define GRPC_STATS_INC_CQ_CALLBACK_CREATES() \
  GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_CQ_CALLBACK_CREATES)
#define GRPC_STATS_INC_CALL_INITIAL_SIZE(value) \
  GRPC_STATS_INC_HISTOGRAM(                     \
      GRPC_STATS_HISTOGRAM_CALL_INITIAL_SIZE,   \
      grpc_core::BucketForHistogramValue_32768_24(static_cast<int>(value)))
#define GRPC_STATS_INC_TCP_WRITE_SIZE(value) \
  GRPC_STATS_INC_HISTOGRAM(                  \
      GRPC_STATS_HISTOGRAM_TCP_WRITE_SIZE,   \
      grpc_core::BucketForHistogramValue_16777216_20(static_cast<int>(value)))
#define GRPC_STATS_INC_TCP_WRITE_IOV_SIZE(value) \
  GRPC_STATS_INC_HISTOGRAM(                      \
      GRPC_STATS_HISTOGRAM_TCP_WRITE_IOV_SIZE,   \
      grpc_core::BucketForHistogramValue_128_10(static_cast<int>(value)))
#define GRPC_STATS_INC_TCP_READ_SIZE(value) \
  GRPC_STATS_INC_HISTOGRAM(                 \
      GRPC_STATS_HISTOGRAM_TCP_READ_SIZE,   \
      grpc_core::BucketForHistogramValue_16777216_20(static_cast<int>(value)))
#define GRPC_STATS_INC_TCP_READ_OFFER(value) \
  GRPC_STATS_INC_HISTOGRAM(                  \
      GRPC_STATS_HISTOGRAM_TCP_READ_OFFER,   \
      grpc_core::BucketForHistogramValue_16777216_20(static_cast<int>(value)))
#define GRPC_STATS_INC_TCP_READ_OFFER_IOV_SIZE(value) \
  GRPC_STATS_INC_HISTOGRAM(                           \
      GRPC_STATS_HISTOGRAM_TCP_READ_OFFER_IOV_SIZE,   \
      grpc_core::BucketForHistogramValue_128_10(static_cast<int>(value)))
#define GRPC_STATS_INC_HTTP2_SEND_MESSAGE_SIZE(value) \
  GRPC_STATS_INC_HISTOGRAM(                           \
      GRPC_STATS_HISTOGRAM_HTTP2_SEND_MESSAGE_SIZE,   \
      grpc_core::BucketForHistogramValue_16777216_20(static_cast<int>(value)))
namespace grpc_core {
int BucketForHistogramValue_32768_24(int value);
int BucketForHistogramValue_16777216_20(int value);
int BucketForHistogramValue_128_10(int value);
}  // namespace grpc_core
extern const int grpc_stats_histo_buckets[7];
extern const int grpc_stats_histo_start[7];
extern const int* const grpc_stats_histo_bucket_boundaries[7];
extern int (*const grpc_stats_get_bucket[7])(int value);

#endif /* GRPC_CORE_LIB_DEBUG_STATS_DATA_H */
