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

/* A very fast in-memory log, optimized for multiple writers. */

#ifndef GRPC_CORE_EXT_CENSUS_MLOG_H
#define GRPC_CORE_EXT_CENSUS_MLOG_H

#include <grpc/support/port_platform.h>
#include <stddef.h>

/* Maximum record size, in bytes. */
#define CENSUS_LOG_2_MAX_RECORD_SIZE 14 /* 2^14 = 16KB */
#define CENSUS_LOG_MAX_RECORD_SIZE (1 << CENSUS_LOG_2_MAX_RECORD_SIZE)

/* Initialize the statistics logging subsystem with the given log size. A log
   size of 0 will result in the smallest possible log for the platform
   (approximately CENSUS_LOG_MAX_RECORD_SIZE * gpr_cpu_num_cores()). If
   discard_old_records is non-zero, then new records will displace older ones
   when the log is full. This function must be called before any other
   census_log functions.
*/
void census_log_initialize(size_t size_in_mb, int discard_old_records);

/* Shutdown the logging subsystem. Caller must ensure that:
   - no in progress or future call to any census_log functions
   - no incomplete records
*/
void census_log_shutdown(void);

/* Allocates and returns a 'size' bytes record and marks it in use. A
   subsequent census_log_end_write() marks the record complete. The
   'bytes_written' census_log_end_write() argument must be <=
   'size'. Returns NULL if out-of-space AND:
       - log is configured to keep old records OR
       - all blocks are pinned by incomplete records.
*/
void* census_log_start_write(size_t size);

void census_log_end_write(void* record, size_t bytes_written);

void census_log_init_reader(void);

/* census_log_read_next() iterates over blocks with data and for each block
   returns a pointer to the first unread byte. The number of bytes that can be
   read are returned in 'bytes_available'. Reader is expected to read all
   available data. Reading the data consumes it i.e. it cannot be read again.
   census_log_read_next() returns NULL if the end is reached i.e last block
   is read. census_log_init_reader() starts the iteration or aborts the
   current iteration.
*/
const void* census_log_read_next(size_t* bytes_available);

/* Returns estimated remaining space across all blocks, in bytes. If log is
   configured to discard old records, returns total log space. Otherwise,
   returns space available in empty blocks (partially filled blocks are
   treated as full).
*/
size_t census_log_remaining_space(void);

/* Returns the number of times grpc_stats_log_start_write() failed due to
   out-of-space. */
int64_t census_log_out_of_space_count(void);

#endif /* GRPC_CORE_EXT_CENSUS_MLOG_H */
