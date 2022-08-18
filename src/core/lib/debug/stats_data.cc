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

#include <grpc/support/port_platform.h>

#include "src/core/lib/debug/stats_data.h"

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"

const char* grpc_stats_counter_name[GRPC_STATS_COUNTER_COUNT] = {
    "client_calls_created",
    "server_calls_created",
    "client_channels_created",
    "client_subchannels_created",
    "server_channels_created",
    "syscall_poll",
    "pollset_kick",
    "pollset_kicked_without_poller",
    "pollset_kicked_again",
    "pollset_kick_wakeup_fd",
    "pollset_kick_wakeup_cv",
    "pollset_kick_own_thread",
    "histogram_slow_lookups",
    "syscall_write",
    "syscall_read",
    "tcp_read_alloc_8k",
    "tcp_read_alloc_64k",
    "http2_settings_writes",
    "http2_pings_sent",
    "http2_writes_begun",
    "http2_transport_stalls",
    "http2_stream_stalls",
};
const char* grpc_stats_counter_doc[GRPC_STATS_COUNTER_COUNT] = {
    "Number of client side calls created by this process",
    "Number of server side calls created by this process",
    "Number of client channels created",
    "Number of client subchannels created",
    "Number of server channels created",
    "Number of polling syscalls (epoll_wait, poll, etc) made by this process",
    "How many polling wakeups were performed by the process",
    "How many times was a polling wakeup requested without an active poller",
    "How many times was the same polling worker awoken repeatedly before "
    "waking up",
    "How many times was an eventfd used as the wakeup vector for a polling "
    "wakeup",
    "How many times was a condition variable used as the wakeup vector for a "
    "polling wakeup",
    "How many times could a polling wakeup be satisfied by keeping the waking "
    "thread awake?",
    "Number of times histogram increments went through the slow (binary "
    "search) path",
    "Number of write syscalls (or equivalent - eg sendmsg) made by this "
    "process",
    "Number of read syscalls (or equivalent - eg recvmsg) made by this process",
    "Number of 8k allocations by the TCP subsystem for reading",
    "Number of 64k allocations by the TCP subsystem for reading",
    "Number of settings frames sent",
    "Number of HTTP2 pings sent by process",
    "Number of HTTP2 writes initiated",
    "Number of times sending was completely stalled by the transport flow "
    "control window",
    "Number of times sending was completely stalled by the stream flow control "
    "window",
};
const char* grpc_stats_histogram_name[GRPC_STATS_HISTOGRAM_COUNT] = {
    "call_initial_size",       "poll_events_returned",    "tcp_write_size",
    "tcp_write_iov_size",      "tcp_read_size",           "tcp_read_offer",
    "tcp_read_offer_iov_size", "http2_send_message_size",
};
const char* grpc_stats_histogram_doc[GRPC_STATS_HISTOGRAM_COUNT] = {
    "Initial size of the grpc_call arena created at call start",
    "How many events are called for each syscall_poll",
    "Number of bytes offered to each syscall_write",
    "Number of byte segments offered to each syscall_write",
    "Number of bytes received by each syscall_read",
    "Number of bytes offered to each syscall_read",
    "Number of byte segments offered to each syscall_read",
    "Size of messages received by HTTP2 transport",
};
const int grpc_stats_table_0[65] = {
    0,      1,      2,      3,      4,     5,     7,     9,     11,    14,
    17,     21,     26,     32,     39,    47,    57,    68,    82,    98,
    117,    140,    167,    199,    238,   284,   339,   404,   482,   575,
    685,    816,    972,    1158,   1380,  1644,  1959,  2334,  2780,  3312,
    3945,   4699,   5597,   6667,   7941,  9459,  11267, 13420, 15984, 19038,
    22676,  27009,  32169,  38315,  45635, 54353, 64737, 77104, 91834, 109378,
    130273, 155159, 184799, 220100, 262144};
const uint8_t grpc_stats_table_1[124] = {
    0,  0,  0,  1,  1,  1,  2,  2,  3,  3,  3,  4,  4,  5,  5,  6,  6,  6,
    7,  7,  7,  8,  9,  9,  10, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15,
    15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 22, 23, 24,
    24, 25, 25, 26, 26, 26, 27, 27, 28, 29, 29, 30, 30, 30, 31, 31, 32, 33,
    33, 34, 34, 34, 35, 35, 36, 37, 37, 37, 38, 38, 39, 39, 40, 40, 41, 41,
    42, 42, 43, 43, 44, 44, 45, 45, 46, 46, 47, 47, 48, 48, 49, 49, 50, 50,
    51, 51, 52, 52, 53, 53, 54, 54, 55, 55, 56, 56, 57, 57, 58, 58};
const int grpc_stats_table_2[129] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,
    15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  30,
    32,  34,  36,  38,  40,  42,  44,  46,  48,  50,  52,  54,  56,  58,  60,
    63,  66,  69,  72,  75,  78,  81,  84,  87,  90,  94,  98,  102, 106, 110,
    114, 118, 122, 126, 131, 136, 141, 146, 151, 156, 162, 168, 174, 180, 186,
    192, 199, 206, 213, 220, 228, 236, 244, 252, 260, 269, 278, 287, 297, 307,
    317, 327, 338, 349, 360, 372, 384, 396, 409, 422, 436, 450, 464, 479, 494,
    510, 526, 543, 560, 578, 596, 615, 634, 654, 674, 695, 717, 739, 762, 785,
    809, 834, 859, 885, 912, 939, 967, 996, 1024};
const uint8_t grpc_stats_table_3[166] = {
    0,  0,  0,  1,  1,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,
    8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 16,
    17, 17, 18, 19, 19, 20, 21, 21, 22, 23, 23, 24, 25, 25, 26, 26, 27, 27, 28,
    28, 29, 29, 30, 30, 31, 31, 32, 32, 33, 33, 34, 34, 35, 36, 36, 37, 38, 39,
    40, 40, 41, 42, 42, 43, 44, 44, 45, 46, 46, 47, 48, 48, 49, 49, 50, 50, 51,
    51, 52, 52, 53, 53, 54, 54, 55, 56, 57, 58, 59, 59, 60, 61, 62, 63, 63, 64,
    65, 65, 66, 67, 67, 68, 69, 69, 70, 71, 71, 72, 72, 73, 73, 74, 75, 75, 76,
    76, 77, 78, 79, 79, 80, 81, 82, 83, 84, 85, 85, 86, 87, 88, 88, 89, 90, 90,
    91, 92, 92, 93, 94, 94, 95, 95, 96, 97, 97, 98, 98, 99};
const int grpc_stats_table_4[65] = {
    0,       1,       2,       3,       4,       6,       8,        11,
    15,      20,      26,      34,      44,      57,      73,       94,
    121,     155,     199,     255,     327,     419,     537,      688,
    881,     1128,    1444,    1848,    2365,    3026,    3872,     4954,
    6338,    8108,    10373,   13270,   16976,   21717,   27782,    35541,
    45467,   58165,   74409,   95189,   121772,  155778,  199281,   254933,
    326126,  417200,  533707,  682750,  873414,  1117323, 1429345,  1828502,
    2339127, 2992348, 3827987, 4896985, 6264509, 8013925, 10251880, 13114801,
    16777216};
const uint8_t grpc_stats_table_5[87] = {
    0,  0,  1,  1,  2,  3,  3,  4,  4,  5,  6,  6,  7,  8,  8,  9,  10, 11,
    11, 12, 13, 13, 14, 15, 15, 16, 17, 17, 18, 19, 20, 20, 21, 22, 22, 23,
    24, 25, 25, 26, 27, 27, 28, 29, 29, 30, 31, 31, 32, 33, 34, 34, 35, 36,
    36, 37, 38, 39, 39, 40, 41, 41, 42, 43, 44, 44, 45, 45, 46, 47, 48, 48,
    49, 50, 51, 51, 52, 53, 53, 54, 55, 56, 56, 57, 58, 58, 59};
const int grpc_stats_table_6[65] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,
    14,  16,  18,  20,  22,  24,  27,  30,  33,  36,  39,  43,  47,
    51,  56,  61,  66,  72,  78,  85,  92,  100, 109, 118, 128, 139,
    151, 164, 178, 193, 209, 226, 244, 264, 285, 308, 333, 359, 387,
    418, 451, 486, 524, 565, 609, 656, 707, 762, 821, 884, 952, 1024};
const uint8_t grpc_stats_table_7[102] = {
    0,  0,  0,  1,  1,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,
    6,  7,  7,  7,  8,  8,  9,  9,  10, 11, 11, 12, 12, 13, 13, 14, 14,
    14, 15, 15, 16, 16, 17, 17, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23,
    23, 24, 24, 24, 25, 26, 27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32,
    32, 33, 33, 34, 35, 35, 36, 37, 37, 38, 38, 39, 39, 40, 40, 41, 41,
    42, 42, 43, 44, 44, 45, 46, 46, 47, 48, 48, 49, 49, 50, 50, 51, 51};
void grpc_stats_inc_call_initial_size(int value) {
  value = grpc_core::Clamp(value, 0, 262144);
  if (value < 6) {
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_CALL_INITIAL_SIZE, value);
    return;
  }
  union {
    double dbl;
    uint64_t uint;
  } _val, _bkt;
  _val.dbl = value;
  if (_val.uint < 4651092515166879744ull) {
    int bucket =
        grpc_stats_table_1[((_val.uint - 4618441417868443648ull) >> 49)] + 6;
    _bkt.dbl = grpc_stats_table_0[bucket];
    bucket -= (_val.uint < _bkt.uint);
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_CALL_INITIAL_SIZE, bucket);
    return;
  }
  GRPC_STATS_INC_HISTOGRAM(
      GRPC_STATS_HISTOGRAM_CALL_INITIAL_SIZE,
      grpc_stats_histo_find_bucket_slow(value, grpc_stats_table_0, 64));
}
void grpc_stats_inc_poll_events_returned(int value) {
  value = grpc_core::Clamp(value, 0, 1024);
  if (value < 29) {
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_POLL_EVENTS_RETURNED, value);
    return;
  }
  union {
    double dbl;
    uint64_t uint;
  } _val, _bkt;
  _val.dbl = value;
  if (_val.uint < 4642789003353915392ull) {
    int bucket =
        grpc_stats_table_3[((_val.uint - 4628855992006737920ull) >> 47)] + 29;
    _bkt.dbl = grpc_stats_table_2[bucket];
    bucket -= (_val.uint < _bkt.uint);
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_POLL_EVENTS_RETURNED, bucket);
    return;
  }
  GRPC_STATS_INC_HISTOGRAM(
      GRPC_STATS_HISTOGRAM_POLL_EVENTS_RETURNED,
      grpc_stats_histo_find_bucket_slow(value, grpc_stats_table_2, 128));
}
void grpc_stats_inc_tcp_write_size(int value) {
  value = grpc_core::Clamp(value, 0, 16777216);
  if (value < 5) {
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_TCP_WRITE_SIZE, value);
    return;
  }
  union {
    double dbl;
    uint64_t uint;
  } _val, _bkt;
  _val.dbl = value;
  if (_val.uint < 4683743612465315840ull) {
    int bucket =
        grpc_stats_table_5[((_val.uint - 4617315517961601024ull) >> 50)] + 5;
    _bkt.dbl = grpc_stats_table_4[bucket];
    bucket -= (_val.uint < _bkt.uint);
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_TCP_WRITE_SIZE, bucket);
    return;
  }
  GRPC_STATS_INC_HISTOGRAM(
      GRPC_STATS_HISTOGRAM_TCP_WRITE_SIZE,
      grpc_stats_histo_find_bucket_slow(value, grpc_stats_table_4, 64));
}
void grpc_stats_inc_tcp_write_iov_size(int value) {
  value = grpc_core::Clamp(value, 0, 1024);
  if (value < 13) {
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_TCP_WRITE_IOV_SIZE, value);
    return;
  }
  union {
    double dbl;
    uint64_t uint;
  } _val, _bkt;
  _val.dbl = value;
  if (_val.uint < 4637863191261478912ull) {
    int bucket =
        grpc_stats_table_7[((_val.uint - 4623507967449235456ull) >> 48)] + 13;
    _bkt.dbl = grpc_stats_table_6[bucket];
    bucket -= (_val.uint < _bkt.uint);
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_TCP_WRITE_IOV_SIZE, bucket);
    return;
  }
  GRPC_STATS_INC_HISTOGRAM(
      GRPC_STATS_HISTOGRAM_TCP_WRITE_IOV_SIZE,
      grpc_stats_histo_find_bucket_slow(value, grpc_stats_table_6, 64));
}
void grpc_stats_inc_tcp_read_size(int value) {
  value = grpc_core::Clamp(value, 0, 16777216);
  if (value < 5) {
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_TCP_READ_SIZE, value);
    return;
  }
  union {
    double dbl;
    uint64_t uint;
  } _val, _bkt;
  _val.dbl = value;
  if (_val.uint < 4683743612465315840ull) {
    int bucket =
        grpc_stats_table_5[((_val.uint - 4617315517961601024ull) >> 50)] + 5;
    _bkt.dbl = grpc_stats_table_4[bucket];
    bucket -= (_val.uint < _bkt.uint);
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_TCP_READ_SIZE, bucket);
    return;
  }
  GRPC_STATS_INC_HISTOGRAM(
      GRPC_STATS_HISTOGRAM_TCP_READ_SIZE,
      grpc_stats_histo_find_bucket_slow(value, grpc_stats_table_4, 64));
}
void grpc_stats_inc_tcp_read_offer(int value) {
  value = grpc_core::Clamp(value, 0, 16777216);
  if (value < 5) {
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_TCP_READ_OFFER, value);
    return;
  }
  union {
    double dbl;
    uint64_t uint;
  } _val, _bkt;
  _val.dbl = value;
  if (_val.uint < 4683743612465315840ull) {
    int bucket =
        grpc_stats_table_5[((_val.uint - 4617315517961601024ull) >> 50)] + 5;
    _bkt.dbl = grpc_stats_table_4[bucket];
    bucket -= (_val.uint < _bkt.uint);
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_TCP_READ_OFFER, bucket);
    return;
  }
  GRPC_STATS_INC_HISTOGRAM(
      GRPC_STATS_HISTOGRAM_TCP_READ_OFFER,
      grpc_stats_histo_find_bucket_slow(value, grpc_stats_table_4, 64));
}
void grpc_stats_inc_tcp_read_offer_iov_size(int value) {
  value = grpc_core::Clamp(value, 0, 1024);
  if (value < 13) {
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_TCP_READ_OFFER_IOV_SIZE,
                             value);
    return;
  }
  union {
    double dbl;
    uint64_t uint;
  } _val, _bkt;
  _val.dbl = value;
  if (_val.uint < 4637863191261478912ull) {
    int bucket =
        grpc_stats_table_7[((_val.uint - 4623507967449235456ull) >> 48)] + 13;
    _bkt.dbl = grpc_stats_table_6[bucket];
    bucket -= (_val.uint < _bkt.uint);
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_TCP_READ_OFFER_IOV_SIZE,
                             bucket);
    return;
  }
  GRPC_STATS_INC_HISTOGRAM(
      GRPC_STATS_HISTOGRAM_TCP_READ_OFFER_IOV_SIZE,
      grpc_stats_histo_find_bucket_slow(value, grpc_stats_table_6, 64));
}
void grpc_stats_inc_http2_send_message_size(int value) {
  value = grpc_core::Clamp(value, 0, 16777216);
  if (value < 5) {
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_HTTP2_SEND_MESSAGE_SIZE,
                             value);
    return;
  }
  union {
    double dbl;
    uint64_t uint;
  } _val, _bkt;
  _val.dbl = value;
  if (_val.uint < 4683743612465315840ull) {
    int bucket =
        grpc_stats_table_5[((_val.uint - 4617315517961601024ull) >> 50)] + 5;
    _bkt.dbl = grpc_stats_table_4[bucket];
    bucket -= (_val.uint < _bkt.uint);
    GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_HTTP2_SEND_MESSAGE_SIZE,
                             bucket);
    return;
  }
  GRPC_STATS_INC_HISTOGRAM(
      GRPC_STATS_HISTOGRAM_HTTP2_SEND_MESSAGE_SIZE,
      grpc_stats_histo_find_bucket_slow(value, grpc_stats_table_4, 64));
}
const int grpc_stats_histo_buckets[8] = {64, 128, 64, 64, 64, 64, 64, 64};
const int grpc_stats_histo_start[8] = {0, 64, 192, 256, 320, 384, 448, 512};
const int* const grpc_stats_histo_bucket_boundaries[8] = {
    grpc_stats_table_0, grpc_stats_table_2, grpc_stats_table_4,
    grpc_stats_table_6, grpc_stats_table_4, grpc_stats_table_4,
    grpc_stats_table_6, grpc_stats_table_4};
void (*const grpc_stats_inc_histogram[8])(int x) = {
    grpc_stats_inc_call_initial_size,
    grpc_stats_inc_poll_events_returned,
    grpc_stats_inc_tcp_write_size,
    grpc_stats_inc_tcp_write_iov_size,
    grpc_stats_inc_tcp_read_size,
    grpc_stats_inc_tcp_read_offer,
    grpc_stats_inc_tcp_read_offer_iov_size,
    grpc_stats_inc_http2_send_message_size};
